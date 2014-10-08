/*
 * Copyright (C) 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright (C) 2011 Richard Zhao, Linaro <richard.zhao@linaro.org>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
 * Copyright (C) 2014 Michael P. Thompson <mpthompson@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Adjustable divider clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/log2.h>

/*
 * DOC: basic adjustable divider clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.  clk->rate = DIV_ROUND_UP(parent->rate / divisor)
 * parent - fixed parent.  No clk_set_parent support
 */

struct clk_source_divider {
	struct clk_hw   hw;
	void __iomem    *reg;
	u8              shift;
	u8              width;
	u8              source;
	u8              flags;
	spinlock_t      *lock;
};

#define to_clk_source_divider(_hw) container_of(_hw, struct clk_source_divider, hw)

#define div_mask(d)	((1 << ((d)->width)) - 1)

static unsigned long clk_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_source_divider *divider = to_clk_source_divider(hw);
	unsigned int reg, div, val, src;

	reg = clk_readl(divider->reg);

	src = reg >> divider->source;
	src &= 0x3;

	val = reg >> divider->shift;
	val &= div_mask(divider);

	if (src != 0x2 && src != 0x3)
		val = 0;

	div = val + 1;
	if (!div) {
		WARN(!(divider->flags & CLK_DIVIDER_ALLOW_ZERO),
			"%s: Zero divisor and CLK_DIVIDER_ALLOW_ZERO not set\n",
			__clk_get_name(hw->clk));
		return parent_rate;
	}

	return DIV_ROUND_UP(parent_rate, div);
}

/*
 * The reverse of DIV_ROUND_UP: The maximum number which
 * divided by m is r
 */
#define MULT_ROUND_UP(r, m) ((r) * (m) + (m) - 1)

static int _div_round_up(struct clk_source_divider *divider,
		unsigned long parent_rate, unsigned long rate)
{
	int div = DIV_ROUND_UP(parent_rate, rate);

	return div;
}

static int _div_round_closest(struct clk_source_divider *divider,
		unsigned long parent_rate, unsigned long rate)
{
	int up, down, div;

	up = down = div = DIV_ROUND_CLOSEST(parent_rate, rate);

	return (up - div) <= (div - down) ? up : down;
}

static int _div_round(struct clk_source_divider *divider, unsigned long parent_rate,
		unsigned long rate)
{
	if (divider->flags & CLK_DIVIDER_ROUND_CLOSEST)
		return _div_round_closest(divider, parent_rate, rate);

	return _div_round_up(divider, parent_rate, rate);
}

static bool _is_best_div(struct clk_source_divider *divider,
		unsigned long rate, unsigned long now, unsigned long best)
{
	if (divider->flags & CLK_DIVIDER_ROUND_CLOSEST)
		return abs(rate - now) < abs(rate - best);

	return now <= rate && now > best;
}

static int clk_divider_bestdiv(struct clk_hw *hw, unsigned long rate,
		unsigned long *best_parent_rate)
{
	struct clk_source_divider *divider = to_clk_source_divider(hw);
	int i, bestdiv = 0;
	unsigned long parent_rate, best = 0, now, maxdiv;
	unsigned long parent_rate_saved = *best_parent_rate;

	if (!rate)
		rate = 1;

	maxdiv = div_mask(divider) + 1;

	if (!(__clk_get_flags(hw->clk) & CLK_SET_RATE_PARENT)) {
		parent_rate = *best_parent_rate;
		bestdiv = _div_round(divider, parent_rate, rate);
		bestdiv = bestdiv == 0 ? 1 : bestdiv;
		bestdiv = bestdiv > maxdiv ? maxdiv : bestdiv;
		return bestdiv;
	}

	/*
	 * The maximum divider we can use without overflowing
	 * unsigned long in rate * i below
	 */
	maxdiv = min(ULONG_MAX / rate, maxdiv);

	for (i = 1; i <= maxdiv; i += 1) {
		if (rate * i == parent_rate_saved) {
			/*
			 * It's the most ideal case if the requested rate can be
			 * divided from parent clock without needing to change
			 * parent rate, so return the divider immediately.
			 */
			*best_parent_rate = parent_rate_saved;
			return i;
		}
		parent_rate = __clk_round_rate(__clk_get_parent(hw->clk),
				MULT_ROUND_UP(rate, i));
		now = DIV_ROUND_UP(parent_rate, i);
		if (_is_best_div(divider, rate, now, best)) {
			bestdiv = i;
			best = now;
			*best_parent_rate = parent_rate;
		}
	}

	if (!bestdiv) {
		bestdiv = div_mask(divider) + 1;
		*best_parent_rate = __clk_round_rate(__clk_get_parent(hw->clk), 1);
	}

	return bestdiv;
}

static long clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_source_divider *divider = to_clk_source_divider(hw);
	int div = 1;
	unsigned int src;

	src = clk_readl(divider->reg) >> divider->source;
	src &= 0x3;

	if (src == 0x2 || src == 0x3)
		div = clk_divider_bestdiv(hw, rate, prate);

	return DIV_ROUND_UP(*prate, div);
}

static int clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_source_divider *divider = to_clk_source_divider(hw);
	unsigned int div, value;
	unsigned long flags = 0;
	u32 val;

	div = DIV_ROUND_UP(parent_rate, rate);

	value = div - 1;

	if (value > div_mask(divider))
		value = div_mask(divider);

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);

	if (divider->flags & CLK_DIVIDER_HIWORD_MASK) {
		val = div_mask(divider) << (divider->shift + 16);
	} else {
		val = clk_readl(divider->reg);
		val &= ~(div_mask(divider) << divider->shift);
	}
	val |= value << divider->shift;
	clk_writel(val, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);

	return 0;
}

const struct clk_ops clk_source_divider_ops = {
	.recalc_rate = clk_divider_recalc_rate,
	.round_rate = clk_divider_round_rate,
	.set_rate = clk_divider_set_rate,
};

/**
 * clk_register_divider - register a divider clock with the clock framework
 * @dev: device registering this clock
 * @name: name of this clock
 * @parent_name: name of clock's parent
 * @flags: framework-specific flags
 * @reg: register address to adjust divider
 * @shift: number of bits to shift the bitfield
 * @width: width of the bitfield
 * @source: number of bits to shift the source
 * @clk_divider_flags: divider-specific flags for this clock
 * @lock: shared register lock for this clock
 */
struct clk *clk_register_source_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width, u8 source,
		u8 clk_divider_flags, spinlock_t *lock)
{
	struct clk_source_divider *div;
	struct clk *clk;
	struct clk_init_data init;

	if (clk_divider_flags & CLK_DIVIDER_HIWORD_MASK) {
		if (width + shift > 16) {
			pr_warn("divider value exceeds LOWORD field\n");
			return ERR_PTR(-EINVAL);
		}
	}

	/* allocate the divider */
	div = kzalloc(sizeof(struct clk_source_divider), GFP_KERNEL);
	if (!div) {
		pr_err("%s: could not allocate divider clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_source_divider_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_source_divider assignments */
	div->reg = reg;
	div->shift = shift;
	div->width = width;
	div->source = source;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;

	/* register the clock */
	clk = clk_register(dev, &div->hw);

	if (IS_ERR(clk))
		kfree(div);

	return clk;
}
