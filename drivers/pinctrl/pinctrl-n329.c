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
#include <linux/of_address.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "core.h"
#include "pinctrl-n329.h"

#define SUFFIX_LEN		4
#define BADPINID		0xffff


#define HW_GCR_GPAFUN	0x80	/* R/W GPIO A Multi Function Control */
#define HW_GCR_GPBFUN	0x84	/* R/W GPIO B Multi Function Control */
#define HW_GCR_GPCFUN	0x88	/* R/W GPIO C Multi Function Control */
#define HW_GCR_GPDFUN	0x8C	/* R/W GPIO D Multi Function Control */
#define HW_GCR_GPEFUN	0x90	/* R/W GPIO E Multi Function Control */

#define HW_GPIOA_OMD	0x00	/* R/W GPIO Port A Output Mode Enable */
#define HW_GPIOA_PUEN	0x04	/* R/W GPIO Port A Pull-up Resistor Enable */
#define HW_GPIOA_DOUT	0x08	/* R/W GPIO Port A Data Output Value */
#define HW_GPIOA_PIN	0x0C	/* R GPIO Port A Value */
#define HW_GPIOB_OMD	0x10	/* R/W GPIO Port B Output Mode Enable */
#define HW_GPIOB_PUEN	0x14	/* R/W GPIO Port B Pull-up Resistor Enable */
#define HW_GPIOB_DOUT	0x18	/* R/W GPIO Port B Data Output Value */
#define HW_GPIOB_PIN	0x1C	/* R GPIO Port B Value */
#define HW_GPIOC_OMD	0x20	/* R/W GPIO Port C Output Mode Enable */
#define HW_GPIOC_PUEN	0x24	/* R/W GPIO Port C Pull-up Resistor Enable */
#define HW_GPIOC_DOUT	0x28	/* R/W GPIO Port C Data Output Value */
#define HW_GPIOC_PIN	0x2C	/* R GPIO Port C Value */
#define HW_GPIOD_OMD	0x30	/* R/W GPIO Port D Output Mode Enable */
#define HW_GPIOD_PUEN	0x34	/* R/W GPIO Port D Pull-up Resistor Enable */
#define HW_GPIOD_DOUT	0x38	/* R/W GPIO Port D Data Output Value */
#define HW_GPIOD_PIN	0x3C	/* R GPIO Port D Value */
#define HW_GPIOE_OMD	0x40	/* R/W GPIO Port E Output Mode Enable */
#define HW_GPIOE_PUEN	0x44	/* R/W GPIO Port E Pull-up Resistor Enable */
#define HW_GPIOE_DOUT	0x48	/* R/W GPIO Port E Data Output Value */
#define HW_GPIOE_PIN	0x4C	/* R GPIO Port E Value */
#define HW_DBNCECON		0x70	/* R/W External Interrupt De-bounce Control */
#define HW_IRQSRCGPA	0x80	/* R/W GPIO Port A IRQ Source Grouping */
#define HW_IRQSRCGPB	0x84	/* R/W GPIO Port B IRQ Source Grouping */
#define HW_IRQSRCGPC	0x88	/* R/W GPIO Port C IRQ Source Grouping */
#define HW_IRQSRCGPD	0x8C	/* R/W GPIO Port D IRQ Source Grouping */
#define HW_IRQSRCGPE	0x90	/* R/W GPIO Port E IRQ Source Grouping */
#define HW_IRQENGPA		0xA0	/* R/W GPIO Port A Interrupt Enable */
#define HW_IRQENGPB		0xA4	/* R/W GPIO Port B Interrupt Enable */
#define HW_IRQENGPC		0xA8	/* R/W GPIO Port C Interrupt Enable */
#define HW_IRQENGPD		0xAC	/* R/W GPIO Port D Interrupt Enable */
#define HW_IRQENGPE		0xB0	/* R/W GPIO Port E Interrupt Enable */
#define HW_IRQLHSEL		0xC0	/* R/W Interrupt Latch Trigger Selection Register */
#define HW_IRQLHGPA		0xD0	/* R GPIO Port A Interrupt Latch Value */
#define HW_IRQLHGPB		0xD4	/* R GPIO Port B Interrupt Latch Value */
#define HW_IRQLHGPC		0xD8	/* R GPIO Port C Interrupt Latch Value */
#define HW_IRQLHGPD	 	0xDC	/* R GPIO Port D Interrupt Latch Value */
#define HW_IRQLHGPE		0xE0	/* R GPIO Port E Interrupt Latch Value */
#define HW_IRQTGSRC0	0xF0	/* R/C IRQ0~3 Trigger Source Indicator GPIO Port A and GPIO Port B */
#define HW_IRQTGSRC1	0xF4	/* R/C IRQ0~3 Trigger Source Indicator GPIO Port C and GPIO Port D */
#define HW_IRQTGSRC2	0xF8	/* R/C IRQ0~3 Trigger Source Indicator GPIO Port E */

/* Each GPIO pin can be mapped to one of four IRQ sources */
#define GPIO_IRQ_SRC_0	0
#define GPIO_IRQ_SRC_1	1
#define GPIO_IRQ_SRC_2	2
#define GPIO_IRQ_SRC_3	3

/* Must start after 32 N329XX AIC hardware IRQs */
#define GPIO_IRQ_START	32

struct n329_pinctrl_data {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct gpio_chip gc;
	void __iomem *gcr_base;
	void __iomem *gpio_base;
	struct n329_pinctrl_soc_data *soc;
	struct irq_domain *domain;
	spinlock_t lock;
	unsigned hw_irq0;
	unsigned hw_irq1;
	unsigned hw_irq2;
	unsigned hw_irq3;
	unsigned rising[5];
	unsigned falling[5];
};

#define to_n329_pinctrl_data(u) container_of(u, struct n329_pinctrl_data, gc)

static unsigned n329_offset_to_pinid(unsigned offset)
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

static unsigned n329_pinid_to_offset(unsigned pinid)
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

static int n329_pinctrl_gpio_get(struct n329_pinctrl_data *pc, 
				unsigned pinid) 
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + HW_GPIOA_OMD + (bank << 4) + 0x0c;

	/* Return the value of the GPIO pin */
	return readl(reg) & (1 << pin) ? 1 : 0;
}

static void n329_pinctrl_gpio_set(struct n329_pinctrl_data *pc, 
				unsigned pinid, int state) 
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + HW_GPIOA_OMD + (bank << 4) + 0x08;
	unsigned long flags;

	spin_lock_irqsave(&pc->lock, flags);

	/* Set the pin out to state */
	if (state)
		writel(readl(reg) | (1 << pin), reg); 
	else
		writel(readl(reg) & ~(1 << pin), reg);

	spin_unlock_irqrestore(&pc->lock, flags);
}

static void n329_pinctrl_gpio_set_input(struct n329_pinctrl_data *pc, 
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

static void n329_pinctrl_gpio_set_output(struct n329_pinctrl_data *pc, 
				unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + HW_GPIOA_OMD + (bank << 4);
	unsigned long flags;

	spin_lock_irqsave(&pc->lock, flags);

	/* Set direction of pin to output mode */
	writel(readl(reg) | (1 << pin), reg);

	spin_unlock_irqrestore(&pc->lock, flags);
}

static void n329_pinctrl_gpio_set_falling(struct n329_pinctrl_data *pc, 
				unsigned pinid, unsigned falling)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + HW_IRQENGPA + (bank << 2);
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

static void n329_pinctrl_gpio_set_rising(struct n329_pinctrl_data *pc, 
				unsigned pinid, unsigned rising)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + HW_IRQENGPA + (bank << 2);
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

static void n329_pinctrl_gpio_reset_trigger(struct n329_pinctrl_data *pc, 
				unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = pc->gpio_base + HW_IRQTGSRC0 + ((bank >> 1) << 2);

	/* Determine the bit to clear */
	unsigned clear = (bank & 0x01) ? (1 << (pin + 16)) : (1 << pin);

	/* Clear the interrupt trigger */
	writel(clear, reg);
}

static unsigned n329_pinctrl_gpio_get_triggers(struct n329_pinctrl_data *pc, 
				unsigned bank)
{
	void __iomem *reg = pc->gpio_base + HW_IRQTGSRC0 + ((bank >> 1) << 2);
	unsigned trigger;

	/* Get the trigger source bits for this bank */
	if (bank & 0x01)
		trigger = (readl(reg) >> 16) & 0xffff;
	else
		trigger = readl(reg) & 0xffff;

	return trigger;
}

static int n329_pinctrl_mux_select_gpio(struct n329_pinctrl_data *pc, 
				unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	unsigned long flags;
	void __iomem *reg;

	/* Sanity checks */
	if (bank > (N329_BANKS - 1))
		goto err;
	if ((pin > 15) || (((bank == 0) || (bank == 4)) && pin > 11))
		goto err;

	/* Mux register address */
	reg = pc->gcr_base + HW_GCR_GPAFUN + (bank << 2);

	spin_lock_irqsave(&pc->lock, flags);

	/* Select the mux to enable gpio function on the indicated pin */
	writel(readl(reg) & ~(0x3 << (pin << 1)), reg);

	spin_unlock_irqrestore(&pc->lock, flags);

	return 1;
	
err:
	return 0;
}

static unsigned n329_pinctrl_get_irq_source(struct n329_pinctrl_data *pc, 
				unsigned pinid) 
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	unsigned shift = (1 << pin);
	void __iomem *reg = pc->gpio_base + HW_IRQSRCGPA + (bank << 2);

	/* Return the irq index for the GPIO pin */
	unsigned irq_src = (int) (readl(reg) >> shift) & 0x03;

	/* GPIO IRQs are offset by two */
	return irq_src;
}

static void n329_pinctrl_set_irq_source(struct n329_pinctrl_data *pc, 
				unsigned pinid, unsigned irq_src) 
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	unsigned shift = (1 << pin);
	unsigned long flags;
	u32 val;
	void __iomem *reg = pc->gpio_base + HW_IRQSRCGPA + (bank << 2);

	spin_lock_irqsave(&pc->lock, flags);

	/* Update the soure register with the irq source */
	val = readl(reg);
	val &= ~(0x03 << shift);
	val |= ((irq_src & 0x03) << shift);
	writel(val, reg);

	spin_unlock_irqrestore(&pc->lock, flags);
}

static int n329_pinctrl_irq_to_irq_source(struct n329_pinctrl_data *pc, 
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

#if 0
static int n329_pinctrl_gpio_request(struct gpio_chip *gc, 
				unsigned offset)
{
	return pinctrl_request_gpio(offset);
}

static void n329_pinctrl_gpio_free(struct gpio_chip *gc, 
				unsigned offset)
{
	struct n329_pinctrl_data *pc = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	pinctrl_free_gpio(offset);
	n329_pinctrl_gpio_set_rising(pc, pinid, 0);
	n329_pinctrl_gpio_set_falling(pc, pinid, 0);
}
#endif

static int n329_pinctrl_gpio_get_value(struct gpio_chip *gc, 
				unsigned offset)
{
	struct n329_pinctrl_data *pc = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return 0;

	/* Get the value */
	return n329_pinctrl_gpio_get(pc, pinid);
}

static void n329_pinctrl_gpio_set_value(struct gpio_chip *gc, 
				unsigned offset, int value)
{
	struct n329_pinctrl_data *pc = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return;

	n329_pinctrl_gpio_set(pc, pinid, value); 
}

static int n329_pinctrl_gpio_dir_out(struct gpio_chip *gc, 
				unsigned offset, int value)
{
	struct n329_pinctrl_data *pc = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return -ENXIO;

	/* Select pin mux for gpio functionality */
	n329_pinctrl_mux_select_gpio(pc, pinid);

	/* Set for output */
	n329_pinctrl_gpio_set_output(pc, pinid);

	/* Set initial value */
	n329_pinctrl_gpio_set(pc, pinid, value);

	return 0;
}

static int n329_pinctrl_gpio_dir_in(struct gpio_chip *gc, unsigned offset)
{
	struct n329_pinctrl_data *pc = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return -ENXIO;

	/* Select pin mux for gpio functionality */
	n329_pinctrl_mux_select_gpio(pc, pinid);

	/* Set for input */
	n329_pinctrl_gpio_set_input(pc, pinid);

	return 0;
}

static int n329_pinctrl_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct n329_pinctrl_data *pc = to_n329_pinctrl_data(gc);
	return irq_find_mapping(pc->domain, offset);
}

static int n329_pinctrl_gpio_irq_set_type(struct irq_data *id, unsigned type)
{
	struct n329_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;
	unsigned pinid, bank, pin;
	int ret;

	/* We only support rising and falling types */
	if (type & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	ret = gpio_lock_as_irq(&pc->gc, offset);
	if (ret)
		return ret;

	pinid = n329_offset_to_pinid(offset);
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

static void n329_pinctrl_gpio_irq_shutdown(struct irq_data *id)
{
	struct n329_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;

	/* This GPIO is no longer used exclusively for IRQ */
	gpio_unlock_as_irq(&pc->gc, offset);
}

static void n329_pinctrl_gpio_irq_ack(struct irq_data *id)
{
	struct n329_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;
	unsigned pinid = n329_offset_to_pinid(offset);
	if (pinid == BADPINID)
		return;

	/* Clear the interrupt trigger */
	n329_pinctrl_gpio_reset_trigger(pc, pinid);
}

static void n329_pinctrl_gpio_irq_mask(struct irq_data *id)
{
	struct n329_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;
	unsigned pinid = n329_offset_to_pinid(offset);
	if (pinid == BADPINID)
		return;

	/* Clear both rising and falling enable */
	n329_pinctrl_gpio_set_rising(pc, pinid, 0);
	n329_pinctrl_gpio_set_falling(pc, pinid, 0);
}

static void n329_pinctrl_gpio_irq_unmask(struct irq_data *id)
{
	struct n329_pinctrl_data *pc = irq_get_chip_data(id->irq);
	unsigned offset = id->hwirq;
	unsigned pinid = n329_offset_to_pinid(offset);
	unsigned bank, pin;

	if (pinid == BADPINID)
		return;

	bank = PINID_TO_BANK(pinid);
	pin = PINID_TO_PIN(pinid);

	/* Make sure pin is an input */
	n329_pinctrl_gpio_set_input(pc, pinid);

	/* Set the GPIO IRQ0 source group for this pin */
	n329_pinctrl_set_irq_source(pc, pinid, GPIO_IRQ_SRC_0);

	/* Set or clear rising enable */
	n329_pinctrl_gpio_set_rising(pc, pinid, 
					pc->rising[bank] & (1 << pin));

	/* Set or clear falling enable */
	n329_pinctrl_gpio_set_falling(pc, pinid, 
					pc->falling[bank] & (1 << pin));
}

static irqreturn_t n329_pinctrl_gpio_interrupt(int irq, void *dev_id)
{
	struct n329_pinctrl_data *pc = dev_id;
	unsigned bank, pinid;
	unsigned long triggers, i;
	int offset, srcgrp;

	/* Match the IRQ to an IRQ source group */
	srcgrp = n329_pinctrl_irq_to_irq_source(pc, irq);
	if (srcgrp < 0)
		goto no_irq_data;

	for (bank = 0; bank < N329_BANKS; bank++) {

		/* Get the IRQ triggers active for this bank */
		triggers = n329_pinctrl_gpio_get_triggers(pc, bank);

		/* Get the active pins for this bank */
		for_each_set_bit(i, &triggers, 16) {
			pinid = PINID(bank, (unsigned) i);

			/* Only process interrupts matching this source group */
			if (srcgrp == n329_pinctrl_get_irq_source(pc, pinid)) {

				/* Map pin to IRQ offset within GPIO IRQ domain */
				offset = n329_pinid_to_offset(pinid);

				/* Clear the edge trigger so we don't miss edges*/
				n329_pinctrl_gpio_reset_trigger(pc, pinid);

				/* Call the software interrupt handler */
				generic_handle_irq(irq_find_mapping(pc->domain, offset));
			}
		}
	}

no_irq_data:
	return IRQ_HANDLED;
}

static struct irq_chip n329_irqchip = {
	.name = "N329 GPIO chip",
	.irq_enable = n329_pinctrl_gpio_irq_unmask,
	.irq_disable = n329_pinctrl_gpio_irq_mask,
	.irq_unmask = n329_pinctrl_gpio_irq_unmask,
	.irq_mask = n329_pinctrl_gpio_irq_mask,
	.irq_ack = n329_pinctrl_gpio_irq_ack,
	.irq_set_type = n329_pinctrl_gpio_irq_set_type,
	.irq_shutdown = n329_pinctrl_gpio_irq_shutdown,
};

static int n329_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->soc->ngroups;
}

static const char *n329_get_group_name(struct pinctrl_dev *pctldev,
				unsigned group)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->soc->groups[group].name;
}

static int n329_get_group_pins(struct pinctrl_dev *pctldev, 
				unsigned group,
			    const unsigned **pins,
			    unsigned *num_pins)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	*pins = pc->soc->groups[group].pins;
	*num_pins = pc->soc->groups[group].npins;

	return 0;
}

static void n329_pin_dbg_show(struct pinctrl_dev *pctldev, 
				struct seq_file *s,
			    unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}

static int n329_dt_node_to_map(struct pinctrl_dev *pctldev,
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

static void n329_dt_free_map(struct pinctrl_dev *pctldev,
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

static const struct pinctrl_ops n329_pinctrl_ops = {
	.get_groups_count = n329_get_groups_count,
	.get_group_name = n329_get_group_name,
	.get_group_pins = n329_get_group_pins,
	.pin_dbg_show = n329_pin_dbg_show,
	.dt_node_to_map = n329_dt_node_to_map,
	.dt_free_map = n329_dt_free_map,
};

static int n329_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->soc->nfunctions;
}

static const char *n329_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
				unsigned function)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->soc->functions[function].name;
}

static int n329_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
				unsigned group,
				const char * const **groups,
				unsigned * const num_groups)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	*groups = pc->soc->functions[group].groups;
	*num_groups = pc->soc->functions[group].ngroups;

	return 0;
}

static int n329_pinctrl_enable(struct pinctrl_dev *pctldev, 
				unsigned selector,
				unsigned group)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);
	struct n329_group *g = &pc->soc->groups[group];
	void __iomem *reg;
	unsigned bank, pin, shift, val, i;

	for (i = 0; i < g->npins; i++) {
		bank = PINID_TO_BANK(g->pins[i]);
		pin = PINID_TO_PIN(g->pins[i]);
		reg = pc->gcr_base + HW_IRQSRCGPA + (bank << 2);
		shift = pin << 1;

		val = readl(reg);
		val &= ~(0x3 << shift);
		val |= (g->muxsel[i] << shift);
		writel(val, reg);
	}

	return 0;
}

static const struct pinmux_ops n329_pinmux_ops = {
	.get_functions_count = n329_pinctrl_get_funcs_count,
	.get_function_name = n329_pinctrl_get_func_name,
	.get_function_groups = n329_pinctrl_get_func_groups,
	.enable = n329_pinctrl_enable,
};

static int n329_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned pin, unsigned long *config)
{
	return -ENOTSUPP;
}

static int n329_pinconf_set(struct pinctrl_dev *pctldev,
			   unsigned pin, unsigned long *configs,
			   unsigned num_configs)
{
	return -ENOTSUPP;
}

static int n329_pinconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned group, unsigned long *config)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);

	*config = pc->soc->groups[group].config;

	return 0;
}

static int n329_pinconf_group_set(struct pinctrl_dev *pctldev,
				 unsigned group, unsigned long *configs,
				 unsigned num_configs)
{
	struct n329_pinctrl_data *pc = pinctrl_dev_get_drvdata(pctldev);
	struct n329_group *g = &pc->soc->groups[group];
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
					writel(readl(reg) | (1 << shift), reg);
				else
					writel(readl(reg) & ~(1 << shift), reg);
			}
		}

		/* Cache the config value for n329_pinconf_group_get() */
		g->config = config;

	}

	return 0;
}

static void n329_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned pin)
{
	/* not supported */
}

static void n329_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned group)
{
	unsigned long config;

	if (!n329_pinconf_group_get(pctldev, group, &config))
		seq_printf(s, "0x%lx", config);
}

static const struct pinconf_ops n329_pinconf_ops = {
	.pin_config_get = n329_pinconf_get,
	.pin_config_set = n329_pinconf_set,
	.pin_config_group_get = n329_pinconf_group_get,
	.pin_config_group_set = n329_pinconf_group_set,
	.pin_config_dbg_show = n329_pinconf_dbg_show,
	.pin_config_group_dbg_show = n329_pinconf_group_dbg_show,
};

static struct pinctrl_desc n329_pinctrl_desc = {
	.pctlops = &n329_pinctrl_ops,
	.pmxops = &n329_pinmux_ops,
	.confops = &n329_pinconf_ops,
	.owner = THIS_MODULE,
};

static int n329_pinctrl_parse_group(struct platform_device *pdev,
				   struct device_node *np, int idx,
				   const char **out_name)
{
	struct n329_pinctrl_data *pc = platform_get_drvdata(pdev);
	struct n329_group *g = &pc->soc->groups[idx];
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

static int n329_pinctrl_probe_dt(struct platform_device *pdev,
				struct n329_pinctrl_data *pc)
{
	struct n329_pinctrl_soc_data *soc = pc->soc;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct n329_function *f;
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
			ret = n329_pinctrl_parse_group(pdev, child,
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
		ret = n329_pinctrl_parse_group(pdev, child, idxg++,
					      &f->groups[i++]);
		if (ret)
			return ret;
	}

	return 0;
}

static struct device_node *n329_get_first_gpio(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *np;

	for_each_child_of_node(node, np) {
		if (of_find_property(np, "gpio-controller", NULL))
			return np;
	}

	return NULL;
}

int n329_pinctrl_probe(struct platform_device *pdev,
		      struct n329_pinctrl_soc_data *soc)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *gp;
	struct n329_pinctrl_data *pc;
	struct clk *clk_mux;
	struct clk *clk_div;
	struct clk *clk_gate;
	int pin, ret;

	/* We must have at least one child gpio node */
	gp = n329_get_first_gpio(pdev);
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
	pc->soc = soc;

	spin_lock_init(&pc->lock);

	pc->gpio_base = of_iomap(np, 0);
	pc->gcr_base = of_iomap(np, 1);
	if (!pc->gpio_base || !pc->gcr_base) {
		ret = -EADDRNOTAVAIL;
		goto err_free;
	}

	platform_set_drvdata(pdev, pc);

	ret = n329_pinctrl_probe_dt(pdev, pc);
	if (ret) {
		dev_err(&pdev->dev, "pinctrl dt probe failed: %d\n", ret);
		goto err_unmapio;
	}

	pc->gc.label = "n329-gpio";
	pc->gc.base = 0;
	pc->gc.ngpio = pc->soc->npins;
	pc->gc.owner = THIS_MODULE;

#if 0
	pc->gc.request = n329_pinctrl_gpio_request;
	pc->gc.free = n329_pinctrl_gpio_free;
#endif
	pc->gc.direction_input = n329_pinctrl_gpio_dir_in;
	pc->gc.direction_output = n329_pinctrl_gpio_dir_out;
	pc->gc.get = n329_pinctrl_gpio_get_value;
	pc->gc.set = n329_pinctrl_gpio_set_value;
	pc->gc.to_irq = n329_pinctrl_gpio_to_irq;
	pc->gc.can_sleep = 0;
	pc->gc.of_node = gp;

	/* Register the GPIO chip */
	ret = gpiochip_add(&pc->gc);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register N329 gpio driver\n");
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
		unsigned pinid = n329_offset_to_pinid(pin);
		int irq = irq_create_mapping(pc->domain, pin);
		/* No validity check; all N329XX GPIOs pins are valid IRQs */
		irq_set_chip_data(irq, pc);
		irq_set_chip(irq, &n329_irqchip);
		irq_set_handler(irq, handle_simple_irq);
		set_irq_flags(irq, IRQF_VALID);
		n329_pinctrl_set_irq_source(pc, pinid, GPIO_IRQ_SRC_0);
	}

	/* Redirect each hardware interrupt to the same handler */
	pc->hw_irq0 = irq_of_parse_and_map(gp, 0);
	pc->hw_irq1 = irq_of_parse_and_map(gp, 1);
	pc->hw_irq2 = irq_of_parse_and_map(gp, 2);
	pc->hw_irq3 = irq_of_parse_and_map(gp, 3);
	request_irq(pc->hw_irq0, n329_pinctrl_gpio_interrupt, 0, dev_name(pc->dev), pc);
	request_irq(pc->hw_irq1, n329_pinctrl_gpio_interrupt, 0, dev_name(pc->dev), pc);
	request_irq(pc->hw_irq2, n329_pinctrl_gpio_interrupt, 0, dev_name(pc->dev), pc);
	request_irq(pc->hw_irq3, n329_pinctrl_gpio_interrupt, 0, dev_name(pc->dev), pc);

	/* Add pin control */
	n329_pinctrl_desc.pins = pc->soc->pins;
	n329_pinctrl_desc.npins = pc->soc->npins;
	n329_pinctrl_desc.name = dev_name(&pdev->dev);
	pc->pctl = pinctrl_register(&n329_pinctrl_desc, &pdev->dev, pc);
	if (!pc->pctl) {
		dev_err(&pdev->dev, "Couldn't register N329 pinctrl driver\n");
		ret = gpiochip_remove(&pc->gc);
		ret = -EINVAL;
		goto err_unmapio;
	}

	return 0;

err_unmapio:
	if (pc->gcr_base)
		iounmap(pc->gcr_base);
	if (pc->gpio_base)
		iounmap(pc->gpio_base);
err_free:
	devm_kfree(&pdev->dev, pc);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(n329_pinctrl_probe);

int n329_pinctrl_remove(struct platform_device *pdev)
{
	struct n329_pinctrl_data *pc = platform_get_drvdata(pdev);

	pinctrl_unregister(pc->pctl);
	iounmap(pc->gcr_base);

	return 0;
}
EXPORT_SYMBOL_GPL(n329_pinctrl_remove);
