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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/stmp_device.h>
#include <asm/exception.h>

#include "irqchip.h"

#define HW_AIC_SCR1					0x0000
#define HW_AIC_SCR2					0x0004
#define HW_AIC_SCR3					0x0008
#define HW_AIC_SCR4					0x000C
#define HW_AIC_SCR5					0x0010
#define HW_AIC_SCR6					0x0014
#define HW_AIC_SCR7					0x0018
#define HW_AIC_SCR8					0x001C
#define HW_AIC_IRSR					0x0100
#define HW_AIC_IASR					0x0104
#define HW_AIC_ISR					0x0108
#define HW_AIC_IPER					0x010C
#define HW_AIC_ISNR					0x0110
#define HW_AIC_IMR					0x0114
#define HW_AIC_OISR					0x0118

#define HW_AIC_MECR					0x0120
#define HW_AIC_MDCR					0x0124
#define HW_AIC_SSCR					0x0128
#define HW_AIC_SCCR					0x012C
#define HW_AIC_EOSCR				0x0130
#define HW_AIC_TEST					0x0134

#define AIC_NUM_IRQS				32

static void __iomem *aic_base;
static struct irq_domain *aic_domain;

static void aic_ack_irq(struct irq_data *d)
{
	/* This register is used by the interrupt service routine to 
	 * indicate that it is completely served. Thus, the interrupt 
	 * handler can write any value to this register to indicate 
	 * the end of its interrupt service */
	__raw_writel(0x01, aic_base + HW_AIC_EOSCR);
}

static void aic_mask_irq(struct irq_data *d)
{
	/* Disables the corresponding interrupt channel */
	__raw_writel(1 << (d->hwirq), aic_base + HW_AIC_MDCR);
}

static void aic_unmask_irq(struct irq_data *d)
{
	/* Enables the corresponding interrupt channel */
	__raw_writel(1 << (d->hwirq), aic_base + HW_AIC_MECR);
}

static struct irq_chip n329_aic_chip = {
	.irq_ack = aic_ack_irq,
	.irq_mask = aic_mask_irq,
	.irq_unmask = aic_unmask_irq,
};

asmlinkage void __exception_irq_entry aic_handle_irq(struct pt_regs *regs)
{
	u32 irqnr;

	irqnr = __raw_readl(aic_base + HW_AIC_IPER);
	irqnr = __raw_readl(aic_base + HW_AIC_ISNR);
	if (!irqnr)
		__raw_writel(0x01, aic_base + HW_AIC_EOSCR);
	irqnr = irq_find_mapping(aic_domain, irqnr);
	handle_IRQ(irqnr, regs);
}

static int aic_irq_domain_map(struct irq_domain *d, unsigned int virq,
				irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &n329_aic_chip, handle_level_irq);
	set_irq_flags(virq, IRQF_VALID);

	return 0;
}

static struct irq_domain_ops aic_irq_domain_ops = {
	.map = aic_irq_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

static int __init aic_of_init(struct device_node *np,
			  struct device_node *interrupt_parent)
{
	aic_base = of_iomap(np, 0);
	WARN_ON(!aic_base);

	/* The AIC doesn't have an individual reset so we put the
	 * source control registers back to their defaults */
	__raw_writel(0xFFFFFFFF, aic_base + HW_AIC_MDCR);
	__raw_writel(0xFFFFFFFF, aic_base + HW_AIC_SCCR);
	__raw_writel(0x47474747, aic_base + HW_AIC_SCR1);
	__raw_writel(0x47474747, aic_base + HW_AIC_SCR2);
	__raw_writel(0x47474747, aic_base + HW_AIC_SCR3);
	__raw_writel(0x47474747, aic_base + HW_AIC_SCR4);
	__raw_writel(0x47474747, aic_base + HW_AIC_SCR5);
	__raw_writel(0x47474747, aic_base + HW_AIC_SCR6);
	__raw_writel(0x47474747, aic_base + HW_AIC_SCR7);
	__raw_writel(0x47474747, aic_base + HW_AIC_SCR8);

	aic_domain = irq_domain_add_linear(np, AIC_NUM_IRQS,
					     &aic_irq_domain_ops, NULL);
	return aic_domain ? 0 : -ENODEV;
}
IRQCHIP_DECLARE(n329, "nuvoton,aic", aic_of_init);
