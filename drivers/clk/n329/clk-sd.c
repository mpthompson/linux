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

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include "clk.h"

/**
 * DOC: sd clock which can set its rate and gate/ungate it's ouput
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gating
 * rate - rate is adjustable.
 * parent - fixed parent.  No clk_set_parent support
 */

struct clk_sd {
	struct clk_hw hw;
	void __iomem *reg;
	u8 bit_idx;
	spinlock_t *lock;
};

#define to_clk_sd(_hw) container_of(_hw, struct clk_sd, hw)

static int clk_sd_enable(struct clk_hw *hw)
{
	struct clk_sd *sd_clk = to_clk_sd(hw);
	u32 reg;
	unsigned long flags = 0;

	if (sd_clk->lock)
		spin_lock_irqsave(sd_clk->lock, flags);

	reg = clk_readl(sd_clk->reg);

	reg &= ~BIT(sd_clk->bit_idx);

	clk_writel(reg, sd_clk->reg);

	if (sd_clk->lock)
		spin_unlock_irqrestore(sd_clk->lock, flags);

	return 0;
}

static void clk_sd_disable(struct clk_hw *hw)
{
	struct clk_sd *sd_clk = to_clk_sd(hw);
	u32 reg;
	unsigned long flags = 0;

	if (sd_clk->lock)
		spin_lock_irqsave(sd_clk->lock, flags);

	reg = clk_readl(sd_clk->reg);

	reg &= ~BIT(sd_clk->bit_idx);

	clk_writel(reg, sd_clk->reg);

	if (sd_clk->lock)
		spin_unlock_irqrestore(sd_clk->lock, flags);
}

static int clk_sd_is_enabled(struct clk_hw *hw)
{
	u32 reg;
	struct clk_sd *sd_clk = to_clk_sd(hw);

	reg = clk_readl(sd_clk->reg);

	return (reg & BIT(sd_clk->bit_idx)) ? 1 : 0;
}

#define ABS_DELTA(a,b)  ((a) > (b) ? (a) - (b) : (b) - (a))

unsigned long clk_sd_best_rate(unsigned long rate, unsigned long *ret_pll_div,
				unsigned long *ret_src)
{
	unsigned long apll_rate, upll_rate, xin_rate, pll_div, clk_div;
	unsigned long best_rate, best_pll_div, best_clk_div, best_src;
	unsigned long test_rate, test_pll_div, test_clk_div;

	apll_rate = clk_get_rate(n329_clocks_get(apll_clk));
	upll_rate = clk_get_rate(n329_clocks_get(upll_clk));
	xin_rate = clk_get_rate(n329_clocks_get(xtal_clk));
	pll_div = (1 << 3);
	clk_div = (1 << 8);

	best_rate = 0xffffffff;
	best_pll_div = 0;
	best_clk_div = 0;
	best_src = 0;

	/* Test best rate for the xin input */
	for (test_clk_div = 0; test_clk_div < clk_div; ++test_clk_div) {
		test_rate = xin_rate / (test_clk_div + 1);
		if ((ABS_DELTA(rate, test_rate) < ABS_DELTA(rate, best_rate))) {
			best_rate = test_rate;
			best_src = 0;
			best_pll_div = 0;
			best_clk_div = test_clk_div;
		}
	}

	if (rate == best_rate)
		goto found;

	/* Test best rate for upll input */
	for (test_pll_div = 0; test_pll_div < pll_div; ++test_pll_div) {
		for (test_clk_div = 0; test_clk_div < clk_div; ++test_clk_div) {
			test_rate = (upll_rate / (test_pll_div + 1)) / 
					(test_clk_div + 1);
			if ((ABS_DELTA(rate, test_rate) < 
					ABS_DELTA(rate, best_rate))) {
				best_rate = test_rate;
				best_src = 3;
				best_pll_div = test_pll_div;
				best_clk_div = test_clk_div;
			}
		}
	}

	if (rate == best_rate)
		goto found;

	/* Test best rate for apll input */
	for (test_pll_div = 0; test_pll_div < pll_div; ++test_pll_div) {
		for (test_clk_div = 0; test_clk_div < clk_div; ++test_clk_div) {
			test_rate = (apll_rate / (test_pll_div + 1)) / 
					(test_clk_div + 1);
			if ((ABS_DELTA(rate, test_rate) < 
					ABS_DELTA(rate, best_rate))) {
				best_rate = test_rate;
				best_src = 2;
				best_pll_div = test_pll_div;
				best_clk_div = test_clk_div;
			}
		}
	}

found:

	if (ret_src)
		*ret_src = best_src;
	if (ret_pll_div)
		*ret_pll_div = best_pll_div;

	return best_rate;
}

static unsigned long clk_sd_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	if (!clk_sd_is_enabled(hw))
		return 0;

	return parent_rate;
}

static long clk_sd_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	unsigned long best_rate;

	if (!clk_sd_is_enabled(hw))
		best_rate = 0;
	else
		best_rate = clk_sd_best_rate(rate, NULL, NULL);

	return best_rate;
}

static int clk_sd_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	unsigned long best_src;
	unsigned long best_pll_div;
	unsigned long best_rate;

	best_rate = clk_sd_best_rate(rate, &best_pll_div, &best_src);

	if (best_src == 3) {
		clk_set_parent(n329_clocks_get(sd_uclk),
				n329_clocks_get(udiv0_clk + best_pll_div));
		clk_set_parent(n329_clocks_get(sd_src), 
				n329_clocks_get(sd_uclk));
	} else if (best_src == 2) {
		clk_set_parent(n329_clocks_get(sd_aclk),
				n329_clocks_get(adiv0_clk + best_pll_div));
		clk_set_parent(n329_clocks_get(sd_src), 
				n329_clocks_get(sd_aclk));
	} else {
		clk_set_parent(n329_clocks_get(sd_src), 
				n329_clocks_get(xtal_clk));
	}
	clk_set_rate(n329_clocks_get(sd_div), best_rate);

	return 0;
}

const struct clk_ops clk_sd_ops = {
	.enable = clk_sd_enable,
	.disable = clk_sd_disable,
	.is_enabled = clk_sd_is_enabled,
	.recalc_rate = clk_sd_recalc_rate,
	.round_rate = clk_sd_round_rate,
	.set_rate = clk_sd_set_rate,
};
EXPORT_SYMBOL_GPL(clk_sd_ops);

/**
 * clk_register_gate - register a gate clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of this clock's parent
 * @flags: framework-specific flags for this clock
 * @reg: register address to control gating of this clock
 * @bit_idx: which bit in the register controls gating of this clock
 * @lock: shared register lock for this clock
 */
struct clk *clk_register_sd(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		spinlock_t *lock)
{
	struct clk_sd *sd_clk;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the sd_clk */
	sd_clk = kzalloc(sizeof(struct clk_sd), GFP_KERNEL);
	if (!sd_clk) {
		pr_err("%s: could not allocate gated clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_sd_ops;
	init.flags = (flags & ~CLK_SET_RATE_PARENT) | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_sd assignments */
	sd_clk->reg = reg;
	sd_clk->bit_idx = bit_idx;
	sd_clk->lock = lock;
	sd_clk->hw.init = &init;

	clk = clk_register(dev, &sd_clk->hw);

	if (IS_ERR(clk))
		kfree(sd_clk);

	return clk;
}
EXPORT_SYMBOL_GPL(clk_register_sd);
