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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define HW_TMR_TCSR0	0x00	/* R/W Timer Control and Status Register 0. */
#define HW_TMR_TCSR1	0x04	/* R/W Timer Control and Status Register 1. */
#define HW_TMR_TICR0	0x08	/* R/W Timer Initial Control Register 0. */
#define HW_TMR_TICR1	0x0C	/* R/W Timer Initial Control Register 1. */
#define HW_TMR_TDR0		0x10	/* R Timer Data Register. */
#define HW_TMR_TDR1		0x14	/* R Timer Data Register. */
#define HW_TMR_TISR		0x18	/* R/W Timer Interrupt Status Register. */
#define HW_TMR_WTCR		0x1C	/* R/W Watchdog Timer Control Register. */

#define TMR_PERIOD		(0x01 << 27)
#define TMR_ONESHOT		(0x00 << 27)
#define TMR_COUNTEN		(0x01 << 30)
#define TMR_INTEN		(0x01 << 29)
#define TMR_TDREN		(0x01 << 16)

static void __iomem *tmr_base;
static unsigned int clock_event_rate;
static struct clock_event_device n329_clockevent_device;
static enum clock_event_mode n329_clockevent_mode = CLOCK_EVT_MODE_UNUSED;

static irqreturn_t n329_timer0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	/* Clear timer0 interrupt flag. */
	__raw_writel(0x01, tmr_base + HW_TMR_TISR);

	/* Handle the scheduled event. */
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction n329_timer_irq = {
	.name		= "N329 Timer Tick",
	.dev_id		= &n329_clockevent_device,
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= n329_timer0_interrupt,
};

#ifdef DEBUG
static const char *clock_event_mode_label[] const = {
	[CLOCK_EVT_MODE_PERIODIC] = "CLOCK_EVT_MODE_PERIODIC",
	[CLOCK_EVT_MODE_ONESHOT]  = "CLOCK_EVT_MODE_ONESHOT",
	[CLOCK_EVT_MODE_SHUTDOWN] = "CLOCK_EVT_MODE_SHUTDOWN",
	[CLOCK_EVT_MODE_UNUSED]   = "CLOCK_EVT_MODE_UNUSED"
};
#endif /* DEBUG */

static void n329_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	unsigned int val;

#ifdef DEBUG
	pr_info("%s: changing mode from %s to %s\n", __func__,
		clock_event_mode_label[mxs_clockevent_mode],
		clock_event_mode_label[mode]);
#endif /* DEBUG */

	/* Remember timer mode. */
	n329_clockevent_mode = mode;

	val = __raw_readl(tmr_base + HW_TMR_TCSR0);
	val &= ~(0x03 << 27);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		__raw_writel(clock_event_rate / HZ, tmr_base + HW_TMR_TICR0);
		val |= (TMR_PERIOD | TMR_COUNTEN | TMR_INTEN);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		val |= (TMR_ONESHOT | TMR_COUNTEN | TMR_INTEN);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		/* Left event sources disabled, no more interrupts appear */
		break;
	}

	__raw_writel(val, tmr_base + HW_TMR_TCSR0);
}

static int n329_set_next_event(unsigned long evt,
					struct clock_event_device *clk)
{
	unsigned int val;

	__raw_writel(evt, tmr_base + HW_TMR_TICR0);

	val = __raw_readl(tmr_base + HW_TMR_TCSR0);
	val |= (TMR_COUNTEN | TMR_INTEN);
	__raw_writel(val, tmr_base + HW_TMR_TCSR0);

	return 0;
}

static struct clock_event_device n329_clockevent_device = {
	.name		= "n329_timer0",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= n329_set_mode,
	.set_next_event	= n329_set_next_event,
	.rating		= 200,
};

static void __init n329_clockevents_init(struct device_node *np)
{
	int irq;
	struct clk *timer_clk;

	/* Timer 0 clock source. */
	timer_clk = of_clk_get(np, 0);
	if (IS_ERR(timer_clk)) {
		pr_err("%s: failed to get clk\n", __func__);
		return;
	}

	clk_enable(timer_clk);

	__raw_writel(0x00, tmr_base + HW_TMR_TCSR0);
	__raw_writel(0x01, tmr_base + HW_TMR_TISR);

	clock_event_rate = clk_get_rate(timer_clk);

	/* Make irqs happen */
	irq = irq_of_parse_and_map(np, 0);
	setup_irq(irq, &n329_timer_irq);

	/* Configure and register a clock event device. */
	n329_clockevent_device.cpumask = cpumask_of(0);
	clockevents_config_and_register(&n329_clockevent_device,
					clk_get_rate(timer_clk),
					0xf, 0xffffffff);
}

static cycle_t n329_get_cycles(struct clocksource *cs)
{
	return __raw_readl(tmr_base + HW_TMR_TDR1);
}

static struct clocksource clocksource_n329 = {
	.name		= "n329_timer1",
	.rating		= 200,
	.read		= n329_get_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 10,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init n329_clocksource_init(struct device_node *np)
{
	unsigned int c;
	struct clk *timer_clk;

	/* Timer 1 clock source. */
	timer_clk = of_clk_get(np, 1);
	if (IS_ERR(timer_clk)) {
		pr_err("%s: failed to get clk\n", __func__);
		return;
	}

	clk_enable(timer_clk);

	__raw_writel(0x00, tmr_base + HW_TMR_TCSR1);
	__raw_writel(0x02, tmr_base + HW_TMR_TISR);
	__raw_writel(0xffffffff, tmr_base + HW_TMR_TICR1);
	__raw_writel(__raw_readl(tmr_base + HW_TMR_TCSR1) | 
					TMR_COUNTEN | TMR_PERIOD | TMR_TDREN, 
					tmr_base + HW_TMR_TCSR1);

	c = clk_get_rate(timer_clk);
	clocksource_register_hz(&clocksource_n329, c);
}

static void __init n329_timer_init(struct device_node *np)
{
	/* Get the timer base address. */
	tmr_base = of_iomap(np, 0);
	WARN_ON(!tmr_base);

	n329_clockevents_init(np);
	n329_clocksource_init(np);
}
CLOCKSOURCE_OF_DECLARE(n329, "nuvoton,tmr", n329_timer_init);
