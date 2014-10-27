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
 * Adjustable split divider clock implementation
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
 * This specific clock handles a weirdness in the N329XX where some 
 * dividers are not specified by contiguous bits in a register
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.  clk->rate = DIV_ROUND_UP(parent->rate / divisor)
 * parent - fixed parent.  No clk_set_parent support
 */

struct clk_split_divider {
	struct clk_hw   hw;
	void __iomem    *reg;
	u8              lo_shift;
	u8              lo_width;
	u8              hi_shift;
	u8              hi_width;
	u8              flags;
	spinlock_t      *lock;
};

#define to_clk_divider(_hw) container_of(_hw, struct clk_split_divider, hw)

#define div_mask(d)	((1 << ((d)->lo_width + (d)->hi_width)) - 1)
#define div_lo_mask(d)	((1 << ((d)->lo_width)) - 1)
#define div_hi_mask(d)	((1 << ((d)->hi_width)) - 1)

static unsigned int _get_maxdiv(struct clk_split_divider *divider)
{
	if (divider->flags & CLK_DIVIDER_ONE_BASED)
		return div_mask(divider);
	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		return 1 << div_mask(divider);
	return div_mask(divider) + 1;
}

static unsigned int _get_div(struct clk_split_divider *divider, 
			unsigned int val)
{
	if (divider->flags & CLK_DIVIDER_ONE_BASED)
		return val;
	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		return 1 << val;
	return val + 1;
}

static unsigned int _get_val(struct clk_split_divider *divider, 
			unsigned int div)
{
	if (divider->flags & CLK_DIVIDER_ONE_BASED)
		return div;
	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		return __ffs(div);
	return div - 1;
}

static unsigned long clk_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_split_divider *divider = to_clk_divider(hw);
	unsigned int div, tmp, val;

	tmp = clk_readl(divider->reg) >> divider->hi_shift;
	tmp &= div_hi_mask(divider);
	val = tmp << divider->lo_width;
	tmp = clk_readl(divider->reg) >> divider->lo_shift;
	tmp &= div_lo_mask(divider);
	val |= tmp;

	div = _get_div(divider, val);
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

static bool _is_valid_div(struct clk_split_divider *divider, unsigned int div)
{
	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		return is_power_of_2(div);
	return true;
}

static int _div_round_up(struct clk_split_divider *divider,
		unsigned long parent_rate, unsigned long rate)
{
	int div = DIV_ROUND_UP(parent_rate, rate);

	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		div = __roundup_pow_of_two(div);

	return div;
}

static int _div_round_closest(struct clk_split_divider *divider,
		unsigned long parent_rate, unsigned long rate)
{
	int up, down, div;

	up = down = div = DIV_ROUND_CLOSEST(parent_rate, rate);

	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO) {
		up = __roundup_pow_of_two(div);
		down = __rounddown_pow_of_two(div);
	}

	return (up - div) <= (div - down) ? up : down;
}

static int _div_round(struct clk_split_divider *divider, 
			unsigned long parent_rate,
			unsigned long rate)
{
	if (divider->flags & CLK_DIVIDER_ROUND_CLOSEST)
		return _div_round_closest(divider, parent_rate, rate);

	return _div_round_up(divider, parent_rate, rate);
}

static bool _is_best_div(struct clk_split_divider *divider,
			unsigned long rate, unsigned long now, 
			unsigned long best)
{
	if (divider->flags & CLK_DIVIDER_ROUND_CLOSEST)
		return abs(rate - now) < abs(rate - best);

	return now <= rate && now > best;
}

static int _next_div(struct clk_split_divider *divider, int div)
{
	div++;

	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		return __roundup_pow_of_two(div);

	return div;
}

static int clk_divider_bestdiv(struct clk_hw *hw, unsigned long rate,
		unsigned long *best_parent_rate)
{
	struct clk_split_divider *divider = to_clk_divider(hw);
	int i, bestdiv = 0;
	unsigned long parent_rate, best = 0, now, maxdiv;
	unsigned long parent_rate_saved = *best_parent_rate;

	if (!rate)
		rate = 1;

	maxdiv = _get_maxdiv(divider);

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

	for (i = 1; i <= maxdiv; i = _next_div(divider, i)) {
		if (!_is_valid_div(divider, i))
			continue;
		if (rate * i == parent_rate_saved) {
			/*
			 * It's the most ideal case if the requested rate 
			 * can be divided from parent clock without needing 
			 * to change parent rate, so return the divider 
			 * immediately.
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
		bestdiv = _get_maxdiv(divider);
		*best_parent_rate = __clk_round_rate(__clk_get_parent(hw->clk), 1);
	}

	return bestdiv;
}

static long clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	int div;
	div = clk_divider_bestdiv(hw, rate, prate);

	return DIV_ROUND_UP(*prate, div);
}

static int clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct clk_split_divider *divider = to_clk_divider(hw);
	unsigned int div, value;
	unsigned long flags = 0;
	u32 val;

	div = DIV_ROUND_UP(parent_rate, rate);

	if (!_is_valid_div(divider, div))
		return -EINVAL;

	value = _get_val(divider, div);

	if (value > div_mask(divider))
		value = div_mask(divider);

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);

	val = clk_readl(divider->reg);

	val &= ~(div_lo_mask(divider) << divider->lo_shift);
	val &= ~(div_hi_mask(divider) << divider->hi_shift);

	val |= (value & div_lo_mask(divider)) << divider->lo_shift;
	val |= ((value >> divider->lo_width) & div_hi_mask(divider)) << 
					divider->hi_shift;

	clk_writel(val, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);

	return 0;
}

const struct clk_ops clk_split_divider_ops = {
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
 * @lo_shift: low number of bits to shift the bitfield
 * @lo_width: low width of the bitfield
 * @hi_shift: high number of bits to shift the bitfield
 * @hi_width: high width of the bitfield
 * @clk_divider_flags: divider-specific flags for this clock
 * @lock: shared register lock for this clock
 */
struct clk *clk_register_split_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags, void __iomem *reg, 
		u8 lo_shift, u8 lo_width, u8 hi_shift, u8 hi_width,
		u8 clk_divider_flags, spinlock_t *lock)
{
	struct clk_split_divider *div;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the split divider */
	div = kzalloc(sizeof(struct clk_split_divider), GFP_KERNEL);
	if (!div) {
		pr_err("%s: could not allocate split divider clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_split_divider_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_split_divider assignments */
	div->reg = reg;
	div->lo_shift = lo_shift;
	div->lo_width = lo_width;
	div->hi_shift = hi_shift;
	div->hi_width = hi_width;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;

	/* register the clock */
	clk = clk_register(dev, &div->hw);

	if (IS_ERR(clk))
		kfree(div);

	return clk;
}

