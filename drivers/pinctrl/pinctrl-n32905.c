/*
 * Copyright (C) 2014 Michael P. Thompson, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/n329-gcr.h>
#include "core.h"

#define REG_GPIOA_OMD	0x00	/* R/W GPIO Port A Output Mode Enable */
#define REG_GPIOA_PUEN	0x04	/* R/W GPIO Port A Pull-up Resistor Enable */
#define REG_GPIOA_DOUT	0x08	/* R/W GPIO Port A Data Output Value */
#define REG_GPIOA_PIN	0x0C	/* R GPIO Port A Value */
#define REG_GPIOB_OMD	0x10	/* R/W GPIO Port B Output Mode Enable */
#define REG_GPIOB_PUEN	0x14	/* R/W GPIO Port B Pull-up Resistor Enable */
#define REG_GPIOB_DOUT	0x18	/* R/W GPIO Port B Data Output Value */
#define REG_GPIOB_PIN	0x1C	/* R GPIO Port B Value */
#define REG_GPIOC_OMD	0x20	/* R/W GPIO Port C Output Mode Enable */
#define REG_GPIOC_PUEN	0x24	/* R/W GPIO Port C Pull-up Resistor Enable */
#define REG_GPIOC_DOUT	0x28	/* R/W GPIO Port C Data Output Value */
#define REG_GPIOC_PIN	0x2C	/* R GPIO Port C Value */
#define REG_GPIOD_OMD	0x30	/* R/W GPIO Port D Output Mode Enable */
#define REG_GPIOD_PUEN	0x34	/* R/W GPIO Port D Pull-up Resistor Enable */
#define REG_GPIOD_DOUT	0x38	/* R/W GPIO Port D Data Output Value */
#define REG_GPIOD_PIN	0x3C	/* R GPIO Port D Value */
#define REG_GPIOE_OMD	0x40	/* R/W GPIO Port E Output Mode Enable */
#define REG_GPIOE_PUEN	0x44	/* R/W GPIO Port E Pull-up Resistor Enable */
#define REG_GPIOE_DOUT	0x48	/* R/W GPIO Port E Data Output Value */
#define REG_GPIOE_PIN	0x4C	/* R GPIO Port E Value */
#define REG_DBNCECON	0x70	/* R/W External Interrupt De-bounce Control */
#define REG_IRQSRCGPA	0x80	/* R/W GPIO Port A IRQ Source Grouping */
#define REG_IRQSRCGPB	0x84	/* R/W GPIO Port B IRQ Source Grouping */
#define REG_IRQSRCGPC	0x88	/* R/W GPIO Port C IRQ Source Grouping */
#define REG_IRQSRCGPD	0x8C	/* R/W GPIO Port D IRQ Source Grouping */
#define REG_IRQSRCGPE	0x90	/* R/W GPIO Port E IRQ Source Grouping */
#define REG_IRQENGPA	0xA0	/* R/W GPIO Port A Interrupt Enable */
#define REG_IRQENGPB	0xA4	/* R/W GPIO Port B Interrupt Enable */
#define REG_IRQENGPC	0xA8	/* R/W GPIO Port C Interrupt Enable */
#define REG_IRQENGPD	0xAC	/* R/W GPIO Port D Interrupt Enable */
#define REG_IRQENGPE	0xB0	/* R/W GPIO Port E Interrupt Enable */
#define REG_IRQLHSEL	0xC0	/* R/W Interrupt Latch Trigger Selection Register */
#define REG_IRQLHGPA	0xD0	/* R GPIO Port A Interrupt Latch Value */
#define REG_IRQLHGPB	0xD4	/* R GPIO Port B Interrupt Latch Value */
#define REG_IRQLHGPC	0xD8	/* R GPIO Port C Interrupt Latch Value */
#define REG_IRQLHGPD	0xDC	/* R GPIO Port D Interrupt Latch Value */
#define REG_IRQLHGPE	0xE0	/* R GPIO Port E Interrupt Latch Value */
#define REG_IRQTGSRC0	0xF0	/* R/C IRQ0~3 Trigger Source Indicator GPIO Port A and GPIO Port B */
#define REG_IRQTGSRC1	0xF4	/* R/C IRQ0~3 Trigger Source Indicator GPIO Port C and GPIO Port D */
#define REG_IRQTGSRC2	0xF8	/* R/C IRQ0~3 Trigger Source Indicator GPIO Port E */

/* Each GPIO pin can be mapped to one of four IRQ sources */
#define GPIO_IRQ_SRC_0	0
#define GPIO_IRQ_SRC_1	1
#define GPIO_IRQ_SRC_2	2
#define GPIO_IRQ_SRC_3	3

/* Number of register banks for the N32905 */
#define N32905_BANKS	5

#define N32905_PINCTRL_PIN(pin)	PINCTRL_PIN(pin, #pin)
#define PINID(bank, pin)	(((bank) << 4) + (pin))

/*
 * pinmux-id bit field definitions
 *
 * bank:	15..12	(4)
 * pin:		11..4	(8)
 * muxsel:	3..0	(4)
 */
#define MUXID_TO_PINID(m)	PINID((m) >> 12 & 0xf, (m) >> 4 & 0xff)
#define MUXID_TO_MUXSEL(m)	((m) & 0xf)

#define PINID_TO_BANK(p)	((p) >> 4)
#define PINID_TO_PIN(p)		((p) % 16)

#define BADPINID		0xffff

/*
 * pin config bit field definitions
 *
 * pull-up:	2..0	(2)
 *
 * MSB of each field is presence bit for the config.
 */
#define PULL_PRESENT		(1 << 1)
#define PULL_SHIFT		0
#define CONFIG_TO_PULL(c)	((c) >> PULL_SHIFT & 0x1)

#define SUFFIX_LEN		4

struct n32905_function {
	const char *name;
	const char **groups;
	unsigned ngroups;
};

struct n32905_group {
	const char *name;
	unsigned int *pins;
	unsigned npins;
	u8 *muxsel;
	u8 config;
};

struct n32905_pinctrl_soc_data {
	unsigned npins;
	const struct pinctrl_pin_desc *pins;
	unsigned nfunctions;
	struct n32905_function *functions;
	unsigned ngroups;
	struct n32905_group *groups;
};

/* Map each multifunction pin to an encoded pin id */
enum n32905_pin_enum {
	PINID_GPA00 = PINID(0, 0),
	PINID_GPA01 = PINID(0, 1),
	PINID_GPA02 = PINID(0, 2),
	PINID_GPA03 = PINID(0, 3),
	PINID_GPA04 = PINID(0, 4),
	PINID_GPA05 = PINID(0, 5),
	PINID_GPA06 = PINID(0, 6),
	PINID_GPA07 = PINID(0, 7),
	PINID_GPA08 = PINID(0, 8),
	PINID_GPA09 = PINID(0, 9),
	PINID_GPA10 = PINID(0, 10),
	PINID_GPA11 = PINID(0, 11),

	PINID_GPB00 = PINID(1, 0),
	PINID_GPB01 = PINID(1, 1),
	PINID_GPB02 = PINID(1, 2),
	PINID_GPB03 = PINID(1, 3),
	PINID_GPB04 = PINID(1, 4),
	PINID_GPB05 = PINID(1, 5),
	PINID_GPB06 = PINID(1, 6),
	PINID_GPB07 = PINID(1, 7),
	PINID_GPB08 = PINID(1, 8),
	PINID_GPB09 = PINID(1, 9),
	PINID_GPB10 = PINID(1, 10),
	PINID_GPB11 = PINID(1, 11),
	PINID_GPB12 = PINID(1, 12),
	PINID_GPB13 = PINID(1, 13),
	PINID_GPB14 = PINID(1, 14),
	PINID_GPB15 = PINID(1, 15),

	PINID_GPC00 = PINID(2, 0),
	PINID_GPC01 = PINID(2, 1),
	PINID_GPC02 = PINID(2, 2),
	PINID_GPC03 = PINID(2, 3),
	PINID_GPC04 = PINID(2, 4),
	PINID_GPC05 = PINID(2, 5),
	PINID_GPC06 = PINID(2, 6),
	PINID_GPC07 = PINID(2, 7),
	PINID_GPC08 = PINID(2, 8),
	PINID_GPC09 = PINID(2, 9),
	PINID_GPC10 = PINID(2, 10),
	PINID_GPC11 = PINID(2, 11),
	PINID_GPC12 = PINID(2, 12),
	PINID_GPC13 = PINID(2, 13),
	PINID_GPC14 = PINID(2, 14),
	PINID_GPC15 = PINID(2, 15),

	PINID_GPD00 = PINID(3, 0),
	PINID_GPD01 = PINID(3, 1),
	PINID_GPD02 = PINID(3, 2),
	PINID_GPD03 = PINID(3, 3),
	PINID_GPD04 = PINID(3, 4),
	PINID_GPD05 = PINID(3, 5),
	PINID_GPD06 = PINID(3, 6),
	PINID_GPD07 = PINID(3, 7),
	PINID_GPD08 = PINID(3, 8),
	PINID_GPD09 = PINID(3, 9),
	PINID_GPD10 = PINID(3, 10),
	PINID_GPD11 = PINID(3, 11),
	PINID_GPD12 = PINID(3, 12),
	PINID_GPD13 = PINID(3, 13),
	PINID_GPD14 = PINID(3, 14),
	PINID_GPD15 = PINID(3, 15),

	PINID_GPE00 = PINID(4, 0),
	PINID_GPE01 = PINID(4, 1),
	PINID_GPE02 = PINID(4, 2),
	PINID_GPE03 = PINID(4, 3),
	PINID_GPE04 = PINID(4, 4),
	PINID_GPE05 = PINID(4, 5),
	PINID_GPE06 = PINID(4, 6),
	PINID_GPE07 = PINID(4, 7),
	PINID_GPE08 = PINID(4, 8),
	PINID_GPE09 = PINID(4, 9),
	PINID_GPE10 = PINID(4, 10),
	PINID_GPE11 = PINID(4, 11),
};

static const struct pinctrl_pin_desc n32905_pins[] = {
	N32905_PINCTRL_PIN(PINID_GPA00),
	N32905_PINCTRL_PIN(PINID_GPA01),
	N32905_PINCTRL_PIN(PINID_GPA02),
	N32905_PINCTRL_PIN(PINID_GPA03),
	N32905_PINCTRL_PIN(PINID_GPA04),
	N32905_PINCTRL_PIN(PINID_GPA05),
	N32905_PINCTRL_PIN(PINID_GPA06),
	N32905_PINCTRL_PIN(PINID_GPA07),
	N32905_PINCTRL_PIN(PINID_GPA08),
	N32905_PINCTRL_PIN(PINID_GPA09),
	N32905_PINCTRL_PIN(PINID_GPA10),
	N32905_PINCTRL_PIN(PINID_GPA11),

	N32905_PINCTRL_PIN(PINID_GPB00),
	N32905_PINCTRL_PIN(PINID_GPB01),
	N32905_PINCTRL_PIN(PINID_GPB02),
	N32905_PINCTRL_PIN(PINID_GPB03),
	N32905_PINCTRL_PIN(PINID_GPB04),
	N32905_PINCTRL_PIN(PINID_GPB05),
	N32905_PINCTRL_PIN(PINID_GPB06),
	N32905_PINCTRL_PIN(PINID_GPB07),
	N32905_PINCTRL_PIN(PINID_GPB08),
	N32905_PINCTRL_PIN(PINID_GPB09),
	N32905_PINCTRL_PIN(PINID_GPB10),
	N32905_PINCTRL_PIN(PINID_GPB11),
	N32905_PINCTRL_PIN(PINID_GPB12),
	N32905_PINCTRL_PIN(PINID_GPB13),
	N32905_PINCTRL_PIN(PINID_GPB14),
	N32905_PINCTRL_PIN(PINID_GPB15),

	N32905_PINCTRL_PIN(PINID_GPC00),
	N32905_PINCTRL_PIN(PINID_GPC01),
	N32905_PINCTRL_PIN(PINID_GPC02),
	N32905_PINCTRL_PIN(PINID_GPC03),
	N32905_PINCTRL_PIN(PINID_GPC04),
	N32905_PINCTRL_PIN(PINID_GPC05),
	N32905_PINCTRL_PIN(PINID_GPC06),
	N32905_PINCTRL_PIN(PINID_GPC07),
	N32905_PINCTRL_PIN(PINID_GPC08),
	N32905_PINCTRL_PIN(PINID_GPC09),
	N32905_PINCTRL_PIN(PINID_GPC10),
	N32905_PINCTRL_PIN(PINID_GPC11),
	N32905_PINCTRL_PIN(PINID_GPC12),
	N32905_PINCTRL_PIN(PINID_GPC13),
	N32905_PINCTRL_PIN(PINID_GPC14),
	N32905_PINCTRL_PIN(PINID_GPC15),

	N32905_PINCTRL_PIN(PINID_GPD00),
	N32905_PINCTRL_PIN(PINID_GPD01),
	N32905_PINCTRL_PIN(PINID_GPD02),
	N32905_PINCTRL_PIN(PINID_GPD03),
	N32905_PINCTRL_PIN(PINID_GPD04),
	N32905_PINCTRL_PIN(PINID_GPD05),
	N32905_PINCTRL_PIN(PINID_GPD06),
	N32905_PINCTRL_PIN(PINID_GPD07),
	N32905_PINCTRL_PIN(PINID_GPD08),
	N32905_PINCTRL_PIN(PINID_GPD09),
	N32905_PINCTRL_PIN(PINID_GPD10),
	N32905_PINCTRL_PIN(PINID_GPD11),
	N32905_PINCTRL_PIN(PINID_GPD12),
	N32905_PINCTRL_PIN(PINID_GPD13),
	N32905_PINCTRL_PIN(PINID_GPD14),
	N32905_PINCTRL_PIN(PINID_GPD15),

	N32905_PINCTRL_PIN(PINID_GPE00),
	N32905_PINCTRL_PIN(PINID_GPE01),
	N32905_PINCTRL_PIN(PINID_GPE02),
	N32905_PINCTRL_PIN(PINID_GPE03),
	N32905_PINCTRL_PIN(PINID_GPE04),
	N32905_PINCTRL_PIN(PINID_GPE05),
	N32905_PINCTRL_PIN(PINID_GPE06),
	N32905_PINCTRL_PIN(PINID_GPE07),
	N32905_PINCTRL_PIN(PINID_GPE08),
	N32905_PINCTRL_PIN(PINID_GPE09),
	N32905_PINCTRL_PIN(PINID_GPE10),
	N32905_PINCTRL_PIN(PINID_GPE11),
};

static struct n32905_pinctrl_soc_data n32905_pinctrl_data = {
	.pins = n32905_pins,
	.npins = ARRAY_SIZE(n32905_pins),
};

struct n32905_pinctrl_data {
	struct device *dev;
	struct device *gcr_dev;
	struct pinctrl_dev *pctl;
	struct gpio_chip gc;
	void __iomem *gpio_base;
	struct n32905_pinctrl_soc_data *soc;
	struct irq_domain *domain;
	spinlock_t lock;
	unsigned hw_irq0;
	unsigned hw_irq1;
	unsigned hw_irq2;
	unsigned hw_irq3;
	unsigned rising[5];
	unsigned falling[5];
};

#define to_n32905_pinctrl_data(u) container_of(u, struct n32905_pinctrl_data, gc)

static unsigned n32905_offset_to_pinid(unsigned offset)
{
	unsigned pinid = BADPINID;

	if (offset < 12)
		pinid = PINID(0, offset);
	else if (offset < 28)
		pinid = PINID(1, offset - 12);
	else if (offset < 44)
		pinid = PINID(2, offset - 28);
	else if (offset < 60)
		pinid = PINID(3, offset - 44);
	else if (offset < 72)
		pinid = PINID(4, offset - 60);

	return pinid;
}

static unsigned n32905_pinid_to_offset(unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	unsigned offset;

	if (bank < 1)
		offset = pinid;
	else if (bank < 2)
		offset = 12 + pin;
	else if (bank < 3)
		offset = 28 + pin;
	else if (bank < 4)
		offset = 44 + pin;
	else
		offset = 60 + pin;

	return offset;
}

static int n32905_pinctrl_gpio_get(struct n32905_pinctrl_data *pc,
			unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + REG_GPIOA_OMD + (bank << 4) + 0x0c;

	/* Return the value of the GPIO pin */
	return readl(reg) & (1 << pin) ? 1 : 0;
}

static void n32905_pinctrl_gpio_set(struct n32905_pinctrl_data *pc,
			unsigned pinid, int state)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + REG_GPIOA_OMD + (bank << 4) + 0x08;
	unsigned long flags;

	spin_lock_irqsave(&pc->lock, flags);

	/* Set the pin out to state */
	if (state)
		writel(readl(reg) | (1 << pin), reg);
	else
		writel(readl(reg) & ~(1 << pin), reg);

	spin_unlock_irqrestore(&pc->lock, flags);
}

static void n32905_pinctrl_gpio_set_input(struct n32905_pinctrl_data *pc,
			unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + (bank << 4);
	unsigned long flags;

	spin_lock_irqsave(&pc->lock, flags);

	/* Set direction of pin to input mode */
	writel(readl(reg) & ~(1 << pin), reg);

	spin_unlock_irqrestore(&pc->lock, flags);
}

static void n32905_pinctrl_gpio_set_output(struct n32905_pinctrl_data *pc,
			unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + REG_GPIOA_OMD + (bank << 4);
	unsigned long flags;

	spin_lock_irqsave(&pc->lock, flags);

	/* Set direction of pin to output mode */
	writel(readl(reg) | (1 << pin), reg);

	spin_unlock_irqrestore(&pc->lock, flags);
}

static void n32905_pinctrl_gpio_set_falling(struct n32905_pinctrl_data *pc,
			unsigned pinid, unsigned falling)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + REG_IRQENGPA + (bank << 2);
	unsigned long flags;

	spin_lock_irqsave(&pc->lock, flags);

	/* Set or clear falling edge IRQ enable */
	if (falling) {
		writel(readl(reg) | (1 << pin), reg);
	} else {
		writel(readl(reg) & ~(1 << pin), reg);
	}

	spin_unlock_irqrestore(&pc->lock, flags);
}

static void n32905_pinctrl_gpio_set_rising(struct n32905_pinctrl_data *pc,
			unsigned pinid, unsigned rising)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + REG_IRQENGPA + (bank << 2);
	unsigned long flags;

	spin_lock_irqsave(&pc->lock, flags);

	/* Set or clear rising edge IRQ enable */
	if (rising) {
		writel(readl(reg) | (1 << (pin + 16)), reg);
	} else {
		writel(readl(reg) & ~(1 << (pin + 16)), reg);
	}

	spin_unlock_irqrestore(&pc->lock, flags);
}

static void n32905_pinctrl_gpio_reset_trigger(struct n32905_pinctrl_data *pc,
			unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + REG_IRQTGSRC0 + ((bank >> 1) << 2);

	/* Determine the bit to clear */
	unsigned clear = (bank & 0x01) ? (1 << (pin + 16)) : (1 << pin);

	/* Clear the interrupt trigger */
	writel(clear, reg);
}

static unsigned n32905_pinctrl_gpio_get_triggers(struct n32905_pinctrl_data *pc,
			unsigned bank)
{
	void __iomem *reg = pc->gpio_base + REG_IRQTGSRC0 + ((bank >> 1) << 2);
	unsigned trigger;

	/* Get the trigger source bits for this bank */
	if (bank & 0x01)
		trigger = (readl(reg) >> 16) & 0xffff;
	else
		trigger = readl(reg) & 0xffff;

	return trigger;
}

static int n32905_pinctrl_mux_select_gpio(struct n32905_pinctrl_data *pc,
			unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	u32 reg;
	u32 val;

	/* Sanity checks */
	if (bank > (N32905_BANKS - 1))
		goto err;
	if ((pin > 15) || (((bank == 0) || (bank == 4)) && pin > 11))
		goto err;

	/* Mux register address */
	reg = REG_GCR_GPAFUN + (bank << 2);

	if (n329_gcr_down(pc->gcr_dev))
		goto err;

	/* Clear out the bits corresponding to the pin */
	val = n329_gcr_read(pc->gcr_dev, reg);
	val &= ~(0x3 << (pin << 1));
	n329_gcr_write(pc->gcr_dev, val, reg);

	n329_gcr_up(pc->gcr_dev);

	return 1;

err:
	return 0;
}

static unsigned n32905_pinctrl_get_irq_source(struct n32905_pinctrl_data *pc,
			unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	unsigned shift = (1 << pin);
	void __iomem *reg = pc->gpio_base + REG_IRQSRCGPA + (bank << 2);

	/* Return the irq index for the GPIO pin */
	unsigned irq_src = (int) (readl(reg) >> shift) & 0x03;

	/* GPIO IRQs are offset by two */
	return irq_src;
}

static void n32905_pinctrl_set_irq_source(struct n32905_pinctrl_data *pc,
			unsigned pinid, unsigned irq_src)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	unsigned shift = (1 << pin);
	unsigned long flags;
	u32 val;
	void __iomem *reg = pc->gpio_base + REG_IRQSRCGPA + (bank << 2);

	spin_lock_irqsave(&pc->lock, flags);

	/* Update the soure register with the irq source */
	val = readl(reg);
	val &= ~(0x03 << shift);
	val |= ((irq_src & 0x03) << shift);
	writel(val, reg);

	spin_unlock_irqrestore(&pc->lock, flags);
}

static int n32905_pinctrl_irq_to_irq_source(struct n32905_pinctrl_data *pc,
			int irq)
{
	/* Match the IRQ to an IRQ source group */
	if (irq == pc->hw_irq0)
		return GPIO_IRQ_SRC_0;
	else if (irq == pc->hw_irq1)
		return GPIO_IRQ_SRC_1;
	else if (irq == pc->hw_irq2)
		return GPIO_IRQ_SRC_2;
	else if (irq == pc->hw_irq3)
		return GPIO_IRQ_SRC_3;

	return -1;
}

static int n32905_pinctrl_gpio_get_value(struct gpio_chip *gc,
			unsigned offset)
{
	struct n32905_pinctrl_data *pc = to_n32905_pinctrl_data(gc);
	unsigned pinid = n32905_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return 0;

	/* Get the value */
	return n32905_pinctrl_gpio_get(pc, pinid);
}

static void n32905_pinctrl_gpio_set_value(struct gpio_chip *gc,
			unsigned offset, int value)
{
	struct n32905_pinctrl_data *pc = to_n32905_pinctrl_data(gc);
	unsigned pinid = n32905_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return;

	n32905_pinctrl_gpio_set(pc, pinid, value);
}

static int n32905_pinctrl_gpio_dir_out(struct gpio_chip *gc,
			unsigned offset, int value)
{
	struct n32905_pinctrl_data *pc = to_n32905_pinctrl_data(gc);
	unsigned pinid = n32905_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return -ENXIO;

	/* Select pin mux for gpio functionality */
	n32905_pinctrl_mux_select_gpio(pc, pinid);

	/* Set for output */
	n32905_pinctrl_gpio_set_output(pc, pinid);

	/* Set initial value */
	n32905_pinctrl_gpio_set(pc, pinid, value);

	return 0;
}

static int n32905_pinctrl_gpio_dir_in(struct gpio_chip *gc, unsigned offset)
{
	struct n32905_pinctrl_data *pc = to_n32905_pinctrl_data(gc);
	unsigned pinid = n32905_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return -ENXIO;

	/* Select pin mux for gpio functionality */
	n32905_pinctrl_mux_select_gpio(pc, pinid);

	/* Set for input */
	n32905_pinctrl_gpio_set_input(pc, pinid);

	return 0;
}

static int n32905_pinctrl_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct n32905_pinctrl_data *pc = to_n32905_pinctrl_data(gc);
	return irq_find_mapping(pc->domain, offset);
}

static int n32905_pinctrl_gpio_irq_set_type(struct irq_data *id, unsigned type)
{
	struct n32905_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;
	unsigned pinid, bank, pin;
	int ret;

	/* We only support rising and falling types */
	if (type & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	ret = gpio_lock_as_irq(&pc->gc, offset);
	if (ret)
		return ret;

	pinid = n32905_offset_to_pinid(offset);
	if (pinid == BADPINID)
		return -EINVAL;

	bank = PINID_TO_BANK(pinid);
	pin = PINID_TO_PIN(pinid);

	if (type & IRQ_TYPE_EDGE_RISING) {
		pc->rising[bank] |= (1 << pin);
	} else {
		pc->rising[bank] &= ~(1 << pin);
	}

	if (type & IRQ_TYPE_EDGE_FALLING) {
		pc->falling[bank] |= (1 << pin);
	} else {
		pc->falling[bank] &= ~(1 << pin);
	}

	__irq_set_handler_locked(id->irq, handle_edge_irq);

	return 0;
}

static void n32905_pinctrl_gpio_irq_shutdown(struct irq_data *id)
{
	struct n32905_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;

	/* This GPIO is no longer used exclusively for IRQ */
	gpio_unlock_as_irq(&pc->gc, offset);
}

static void n32905_pinctrl_gpio_irq_ack(struct irq_data *id)
{
	struct n32905_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;
	unsigned pinid = n32905_offset_to_pinid(offset);
	if (pinid == BADPINID)
		return;

	/* Clear the interrupt trigger */
	n32905_pinctrl_gpio_reset_trigger(pc, pinid);
}

static void n32905_pinctrl_gpio_irq_mask(struct irq_data *id)
{
	struct n32905_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;
	unsigned pinid = n32905_offset_to_pinid(offset);
	if (pinid == BADPINID)
		return;

	/* Clear both rising and falling enable */
	n32905_pinctrl_gpio_set_rising(pc, pinid, 0);
	n32905_pinctrl_gpio_set_falling(pc, pinid, 0);
}

static void n32905_pinctrl_gpio_irq_unmask(struct irq_data *id)
{
	struct n32905_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;
	unsigned pinid = n32905_offset_to_pinid(offset);
	unsigned bank, pin;

	if (pinid == BADPINID)
		return;

	bank = PINID_TO_BANK(pinid);
	pin = PINID_TO_PIN(pinid);

	/* Make sure pin is an input */
	n32905_pinctrl_gpio_set_input(pc, pinid);

	/* Set the GPIO IRQ0 source group for this pin */
	n32905_pinctrl_set_irq_source(pc, pinid, GPIO_IRQ_SRC_0);

	/* Set or clear rising enable */
	n32905_pinctrl_gpio_set_rising(pc, pinid,
					pc->rising[bank] & (1 << pin));

	/* Set or clear falling enable */
	n32905_pinctrl_gpio_set_falling(pc, pinid,
					pc->falling[bank] & (1 << pin));
}

static irqreturn_t n32905_pinctrl_gpio_interrupt(int irq, void *dev_id)
{
	struct n32905_pinctrl_data *pc = dev_id;
	unsigned bank, pinid;
	unsigned long triggers, i;
	int offset, srcgrp;

	/* Match the IRQ to an IRQ source group */
	srcgrp = n32905_pinctrl_irq_to_irq_source(pc, irq);
	if (srcgrp < 0)
		goto no_irq_data;

	for (bank = 0; bank < N32905_BANKS; bank++) {
		/* Get the IRQ triggers active for this bank */
		triggers = n32905_pinctrl_gpio_get_triggers(pc, bank);

		/* Get the active pins for this bank */
		for_each_set_bit(i, &triggers, 16) {
			pinid = PINID(bank, (unsigned) i);

			/* Only process interrupts matching this source group */
			if (srcgrp == n32905_pinctrl_get_irq_source(pc, pinid)) {

				/* Map pin to IRQ offset within GPIO IRQ domain */
				offset = n32905_pinid_to_offset(pinid);

				/* Clear the edge trigger so we don't miss edges*/
				n32905_pinctrl_gpio_reset_trigger(pc, pinid);

				/* Call the software interrupt handler */
				generic_handle_irq(irq_find_mapping(pc->domain, offset));
			}
		}
	}

no_irq_data:
	return IRQ_HANDLED;
}

static struct irq_chip n32905_irqchip = {
	.name = "N32905 GPIO chip",
	.irq_enable = n32905_pinctrl_gpio_irq_unmask,
	.irq_disable = n32905_pinctrl_gpio_irq_mask,
	.irq_unmask = n32905_pinctrl_gpio_irq_unmask,
	.irq_mask = n32905_pinctrl_gpio_irq_mask,
	.irq_ack = n32905_pinctrl_gpio_irq_ack,
	.irq_set_type = n32905_pinctrl_gpio_irq_set_type,
	.irq_shutdown = n32905_pinctrl_gpio_irq_shutdown,
};

static int n32905_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->soc->ngroups;
}

static const char *n32905_get_group_name(struct pinctrl_dev *pctldev,
			unsigned group)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->soc->groups[group].name;
}

static int n32905_get_group_pins(struct pinctrl_dev *pctldev,
			unsigned group,
			const unsigned **pins,
			unsigned *num_pins)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	*pins = pc->soc->groups[group].pins;
	*num_pins = pc->soc->groups[group].npins;

	return 0;
}

static void n32905_pin_dbg_show(struct pinctrl_dev *pctldev,
			struct seq_file *s,
			unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}

static int n32905_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np,
			struct pinctrl_map **map,
			unsigned *num_maps)
{
	struct pinctrl_map *new_map;
	char *group = NULL;
	unsigned new_num = 1;
	unsigned long config = 0;
	unsigned long *pconfig;
	int length = strlen(np->name) + SUFFIX_LEN;
	bool purecfg = false;
	u32 val, reg;
	int ret, i = 0;

	/* Check for pin config node which has no 'reg' property */
	if (of_property_read_u32(np, "reg", &reg))
		purecfg = true;

	ret = of_property_read_u32(np, "nuvoton,pull-up", &val);
	if (!ret)
		config |= val << PULL_SHIFT | PULL_PRESENT;

	/* Check for group node which has both mux and config settings */
	if (!purecfg && config)
		new_num = 2;

	new_map = kzalloc(sizeof(*new_map) * new_num, GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	if (!purecfg) {
		new_map[i].type = PIN_MAP_TYPE_MUX_GROUP;
		new_map[i].data.mux.function = np->name;

		/* Compose group name */
		group = kzalloc(length, GFP_KERNEL);
		if (!group) {
			ret = -ENOMEM;
			goto free;
		}
		snprintf(group, length, "%s.%d", np->name, reg);
		new_map[i].data.mux.group = group;
		i++;
	}

	if (config) {
		pconfig = kmemdup(&config, sizeof(config), GFP_KERNEL);
		if (!pconfig) {
			ret = -ENOMEM;
			goto free_group;
		}

		new_map[i].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		new_map[i].data.configs.group_or_pin = purecfg ? np->name :
								 group;
		new_map[i].data.configs.configs = pconfig;
		new_map[i].data.configs.num_configs = 1;
	}

	*map = new_map;
	*num_maps = new_num;

	return 0;

free_group:
	if (!purecfg)
		kfree(group);
free:
	kfree(new_map);
	return ret;
}

static void n32905_dt_free_map(struct pinctrl_dev *pctldev,
			struct pinctrl_map *map, unsigned num_maps)
{
	unsigned i;

	for (i = 0; i < num_maps; i++) {
		if (map[i].type == PIN_MAP_TYPE_MUX_GROUP)
			kfree(map[i].data.mux.group);
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);
	}

	kfree(map);
}

static const struct pinctrl_ops n32905_pinctrl_ops = {
	.get_groups_count = n32905_get_groups_count,
	.get_group_name = n32905_get_group_name,
	.get_group_pins = n32905_get_group_pins,
	.pin_dbg_show = n32905_pin_dbg_show,
	.dt_node_to_map = n32905_dt_node_to_map,
	.dt_free_map = n32905_dt_free_map,
};

static int n32905_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->soc->nfunctions;
}

static const char *n32905_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
			unsigned function)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->soc->functions[function].name;
}

static int n32905_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
			unsigned group,
			const char * const **groups,
			unsigned * const num_groups)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	*groups = pc->soc->functions[group].groups;
	*num_groups = pc->soc->functions[group].ngroups;

	return 0;
}

static int n32905_pinctrl_enable(struct pinctrl_dev *pctldev,
			unsigned selector,
			unsigned group)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);
	struct n32905_group *g = &pc->soc->groups[group];
	unsigned bank, pin, shift, i;
	u32 reg, val;

	for (i = 0; i < g->npins; i++) {
		bank = PINID_TO_BANK(g->pins[i]);
		pin = PINID_TO_PIN(g->pins[i]);
		shift = pin << 1;
		reg = REG_GCR_GPAFUN + (bank << 2);

		if (!n329_gcr_down(pc->gcr_dev)) {
			val = n329_gcr_read(pc->gcr_dev, reg);
			val &= ~(0x3 << shift);
			val |= (g->muxsel[i] << shift);
			n329_gcr_write(pc->gcr_dev, val, reg);

			n329_gcr_up(pc->gcr_dev);
		}
	}

	return 0;
}

static const struct pinmux_ops n32905_pinmux_ops = {
	.get_functions_count = n32905_pinctrl_get_funcs_count,
	.get_function_name = n32905_pinctrl_get_func_name,
	.get_function_groups = n32905_pinctrl_get_func_groups,
	.enable = n32905_pinctrl_enable,
};

static int n32905_pinconf_get(struct pinctrl_dev *pctldev,
			unsigned pin, unsigned long *config)
{
	return -ENOTSUPP;
}

static int n32905_pinconf_set(struct pinctrl_dev *pctldev,
			unsigned pin, unsigned long *configs,
			unsigned num_configs)
{
	return -ENOTSUPP;
}

static int n32905_pinconf_group_get(struct pinctrl_dev *pctldev,
			unsigned group, unsigned long *config)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	*config = pc->soc->groups[group].config;

	return 0;
}

static int n32905_pinconf_group_set(struct pinctrl_dev *pctldev,
			unsigned group, unsigned long *configs,
			unsigned num_configs)
{
	struct n32905_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);
	struct n32905_group *g = &pc->soc->groups[group];
	void __iomem *reg;
	unsigned pull, bank, pin, shift, i;
	int n;
	unsigned long config;

	for (n = 0; n < num_configs; n++) {
		config = configs[n];

		pull = CONFIG_TO_PULL(config);

		for (i = 0; i < g->npins; i++) {
			bank = PINID_TO_BANK(g->pins[i]);
			pin = PINID_TO_PIN(g->pins[i]);

			/* pull */
			if (config & PULL_PRESENT) {
				reg = pc->gpio_base;
				reg += (bank * 0x10) + 0x04;
				shift = pin;
				if (pull)
					writel(readl(reg) |
						(1 << shift), reg);
				else
					writel(readl(reg) &
						~(1 << shift), reg);
			}
		}

		/* Cache the config value for n32905_pinconf_group_get() */
		g->config = config;

	}

	return 0;
}

static void n32905_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned pin)
{
	/* not supported */
}

static void n32905_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
			struct seq_file *s, unsigned group)
{
	unsigned long config;

	if (!n32905_pinconf_group_get(pctldev, group, &config))
		seq_printf(s, "0x%lx", config);
}

static const struct pinconf_ops n32905_pinconf_ops = {
	.pin_config_get = n32905_pinconf_get,
	.pin_config_set = n32905_pinconf_set,
	.pin_config_group_get = n32905_pinconf_group_get,
	.pin_config_group_set = n32905_pinconf_group_set,
	.pin_config_dbg_show = n32905_pinconf_dbg_show,
	.pin_config_group_dbg_show = n32905_pinconf_group_dbg_show,
};

static struct pinctrl_desc n32905_pinctrl_desc = {
	.pctlops = &n32905_pinctrl_ops,
	.pmxops = &n32905_pinmux_ops,
	.confops = &n32905_pinconf_ops,
	.owner = THIS_MODULE,
};

static int n32905_pinctrl_parse_group(struct platform_device *pdev,
			struct device_node *np, int idx,
			const char **out_name)
{
	struct n32905_pinctrl_data *pc = platform_get_drvdata(pdev);
	struct n32905_group *g = &pc->soc->groups[idx];
	struct property *prop;
	const char *propname = "nuvoton,pinmux-ids";
	char *group;
	int length = strlen(np->name) + SUFFIX_LEN;
	unsigned i;
	u32 val;

	group = devm_kzalloc(&pdev->dev, length, GFP_KERNEL);
	if (!group)
		return -ENOMEM;
	if (of_property_read_u32(np, "reg", &val))
		snprintf(group, length, "%s", np->name);
	else
		snprintf(group, length, "%s.%d", np->name, val);
	g->name = group;

	prop = of_find_property(np, propname, &length);
	if (!prop)
		return -EINVAL;
	g->npins = length / sizeof(u32);

	g->pins = devm_kzalloc(&pdev->dev, g->npins * sizeof(*g->pins),
			       GFP_KERNEL);
	if (!g->pins)
		return -ENOMEM;

	g->muxsel = devm_kzalloc(&pdev->dev, g->npins * sizeof(*g->muxsel),
				 GFP_KERNEL);
	if (!g->muxsel)
		return -ENOMEM;

	of_property_read_u32_array(np, propname, g->pins, g->npins);
	for (i = 0; i < g->npins; i++) {
		g->muxsel[i] = MUXID_TO_MUXSEL(g->pins[i]);
		g->pins[i] = MUXID_TO_PINID(g->pins[i]);
	}
	if (out_name)
		*out_name = g->name;

	return 0;
}

static int n32905_pinctrl_probe_dt(struct platform_device *pdev,
			struct n32905_pinctrl_data *pc)
{
	struct n32905_pinctrl_soc_data *soc = pc->soc;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct n32905_function *f;
	const char *fn, *fnull = "";
	int i = 0, idxf = 0, idxg = 0;
	int ret;
	u32 val;

	child = of_get_next_child(np, NULL);
	if (!child) {
		dev_err(&pdev->dev, "no group is defined\n");
		return -ENOENT;
	}

	/* Count total non-gpio functions and groups */
	fn = fnull;
	for_each_child_of_node(np, child) {
		if (of_find_property(child, "gpio-controller", NULL))
			continue;
		soc->ngroups++;
		/* Skip pure pinconf node */
		if (of_property_read_u32(child, "reg", &val))
			continue;
		if (strcmp(fn, child->name)) {
			fn = child->name;
			soc->nfunctions++;
		}
	}

	soc->functions = devm_kzalloc(&pdev->dev, soc->nfunctions *
				      sizeof(*soc->functions), GFP_KERNEL);
	if (!soc->functions)
		return -ENOMEM;

	soc->groups = devm_kzalloc(&pdev->dev, soc->ngroups *
				   sizeof(*soc->groups), GFP_KERNEL);
	if (!soc->groups)
		return -ENOMEM;

	/* Count groups for each function */
	fn = fnull;
	f = &soc->functions[idxf];
	for_each_child_of_node(np, child) {
		if (of_find_property(child, "gpio-controller", NULL))
			continue;
		if (of_property_read_u32(child, "reg", &val))
			continue;
		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->name = fn = child->name;
		}
		f->ngroups++;
	};

	/* Get groups for each function */
	idxf = 0;
	fn = fnull;
	for_each_child_of_node(np, child) {
		if (of_find_property(child, "gpio-controller", NULL))
			continue;
		if (of_property_read_u32(child, "reg", &val)) {
			ret = n32905_pinctrl_parse_group(pdev, child,
						      idxg++, NULL);
			if (ret)
				return ret;
			continue;
		}

		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->groups = devm_kzalloc(&pdev->dev, f->ngroups *
						 sizeof(*f->groups),
						 GFP_KERNEL);
			if (!f->groups)
				return -ENOMEM;
			fn = child->name;
			i = 0;
		}
		ret = n32905_pinctrl_parse_group(pdev, child, idxg++,
					      &f->groups[i++]);
		if (ret)
			return ret;
	}

	return 0;
}

static struct device_node *n32905_get_first_gpio(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *np;

	for_each_child_of_node(node, np) {
		if (of_find_property(np, "gpio-controller", NULL))
			return np;
	}

	return NULL;
}

static int n32905_pinctrl_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *gp;
	struct device_node *gcr_node;
	struct platform_device *gcr_pdev;
	struct n32905_pinctrl_data *pc;
	struct clk *clk_mux;
	struct clk *clk_div;
	struct clk *clk_gate;
	int pin, ret;

	/* Defer probing until the GCR driver is available */
	gcr_node = of_parse_phandle(np, "gcr-dev", 0);
	if (!gcr_node)
		return -EPROBE_DEFER;
	gcr_pdev = of_find_device_by_node(gcr_node);
	if (!gcr_pdev)
		return -EPROBE_DEFER;

	/* We must have at least one child gpio node */
	gp = n32905_get_first_gpio(pdev);
	if (!gp) {
		ret = -EINVAL;
		goto err;
	}

	/* Initialize gpio clocks */
	clk_mux = of_clk_get(gp, 0);
	clk_div = of_clk_get(gp, 1);
	clk_gate = of_clk_get(gp, 2);
	if (IS_ERR(clk_mux) || IS_ERR(clk_div) || IS_ERR(clk_gate)) {
		ret = -ENXIO;
		goto err;
	}
	clk_prepare_enable(clk_mux);
	clk_prepare_enable(clk_div);
	clk_prepare_enable(clk_gate);

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc) {
		ret = -ENOMEM;
		goto err;
	}

	pc->dev = &pdev->dev;
	pc->gcr_dev = &gcr_pdev->dev;
	pc->soc = &n32905_pinctrl_data;

	spin_lock_init(&pc->lock);

	pc->gpio_base = of_iomap(np, 0);
	if (!pc->gpio_base) {
		ret = -EADDRNOTAVAIL;
		goto err_free;
	}

	platform_set_drvdata(pdev, pc);

	ret = n32905_pinctrl_probe_dt(pdev, pc);
	if (ret) {
		dev_err(&pdev->dev, "pinctrl dt probe failed: %d\n", ret);
		goto err_unmapio;
	}

	pc->gc.label = "n32905-gpio";
	pc->gc.base = 0;
	pc->gc.ngpio = pc->soc->npins;
	pc->gc.owner = THIS_MODULE;

	pc->gc.direction_input = n32905_pinctrl_gpio_dir_in;
	pc->gc.direction_output = n32905_pinctrl_gpio_dir_out;
	pc->gc.get = n32905_pinctrl_gpio_get_value;
	pc->gc.set = n32905_pinctrl_gpio_set_value;
	pc->gc.to_irq = n32905_pinctrl_gpio_to_irq;
	pc->gc.can_sleep = 0;
	pc->gc.of_node = gp;

	/* Register the GPIO chip */
	ret = gpiochip_add(&pc->gc);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register N32905 gpio driver\n");
		goto err_unmapio;
	}

	/* Create an IRQ domain for the GPIO pins */
	pc->domain = irq_domain_add_linear(gp, pc->soc->npins,
				&irq_domain_simple_ops, NULL);
	if (!pc->domain) {
		ret = -ENODEV;
		goto err_unmapio;
	}

	/* Initialize the IRQ chip and handler for each GPIO pin  */
	for (pin = 0; pin < pc->soc->npins; pin++) {
		unsigned pinid = n32905_offset_to_pinid(pin);
		int irq = irq_create_mapping(pc->domain, pin);
		/* No validity check; all N32905 GPIOs pins are valid IRQs */
		irq_set_chip_data(irq, pc);
		irq_set_chip(irq, &n32905_irqchip);
		irq_set_handler(irq, handle_simple_irq);
		set_irq_flags(irq, IRQF_VALID);
		n32905_pinctrl_set_irq_source(pc, pinid, GPIO_IRQ_SRC_0);
	}

	/* Redirect each hardware interrupt to the same handler */
	pc->hw_irq0 = irq_of_parse_and_map(gp, 0);
	pc->hw_irq1 = irq_of_parse_and_map(gp, 1);
	pc->hw_irq2 = irq_of_parse_and_map(gp, 2);
	pc->hw_irq3 = irq_of_parse_and_map(gp, 3);
	request_irq(pc->hw_irq0, n32905_pinctrl_gpio_interrupt, 0,
			dev_name(pc->dev), pc);
	request_irq(pc->hw_irq1, n32905_pinctrl_gpio_interrupt, 0,
			dev_name(pc->dev), pc);
	request_irq(pc->hw_irq2, n32905_pinctrl_gpio_interrupt, 0,
			dev_name(pc->dev), pc);
	request_irq(pc->hw_irq3, n32905_pinctrl_gpio_interrupt, 0,
			dev_name(pc->dev), pc);

	/* Add pin control */
	n32905_pinctrl_desc.pins = pc->soc->pins;
	n32905_pinctrl_desc.npins = pc->soc->npins;
	n32905_pinctrl_desc.name = dev_name(&pdev->dev);
	pc->pctl = pinctrl_register(&n32905_pinctrl_desc, &pdev->dev, pc);
	if (!pc->pctl) {
		dev_err(&pdev->dev, "Couldn't register N329 pinctrl driver\n");
		ret = gpiochip_remove(&pc->gc);
		ret = -EINVAL;
		goto err_unmapio;
	}

	return 0;

err_unmapio:
	if (pc->gpio_base)
		iounmap(pc->gpio_base);
err_free:
	devm_kfree(&pdev->dev, pc);
err:
	return ret;
}

static int n32905_pinctrl_remove(struct platform_device *pdev)
{
	struct n32905_pinctrl_data *pc = platform_get_drvdata(pdev);

	pinctrl_unregister(pc->pctl);

	return 0;
}

static struct of_device_id n32905_pinctrl_of_match[] = {
	{ .compatible = "nuvoton,n32905-pinctrl", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n32905_pinctrl_of_match);

static struct platform_driver n32905_pinctrl_driver = {
	.driver = {
		.name = "n32905-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = n32905_pinctrl_of_match,
	},
	.probe = n32905_pinctrl_probe,
	.remove = n32905_pinctrl_remove,
};

static int __init n32905_pinctrl_init(void)
{
	return platform_driver_register(&n32905_pinctrl_driver);
}
postcore_initcall(n32905_pinctrl_init);

static void __exit n32905_pinctrl_exit(void)
{
	platform_driver_unregister(&n32905_pinctrl_driver);
}
module_exit(n32905_pinctrl_exit);

