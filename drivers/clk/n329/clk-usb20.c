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
 * DOC: usb clock which can set its rate and gate/ungate it's ouput
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gating
 * rate - rate is adjustable.
 * parent - fixed parent.  No clk_set_parent support
 */

struct clk_usb20 {
	struct clk_hw hw;
	void __iomem *reg;
	u8 bit_idx;
	spinlock_t *lock;
};

#define to_clk_usb20(_hw) container_of(_hw, struct clk_usb20, hw)

static int clk_usb20_enable(struct clk_hw *hw)
{
	struct clk_usb20 *usb_clk = to_clk_usb20(hw);
	u32 reg;
	unsigned long flags = 0;

	if (usb_clk->lock)
		spin_lock_irqsave(usb_clk->lock, flags);

	reg = clk_readl(usb_clk->reg);

	reg &= ~BIT(usb_clk->bit_idx);

	clk_writel(reg, usb_clk->reg);

	if (usb_clk->lock)
		spin_unlock_irqrestore(usb_clk->lock, flags);

	return 0;
}

static void clk_usb20_disable(struct clk_hw *hw)
{
	struct clk_usb20 *usb_clk = to_clk_usb20(hw);
	u32 reg;
	unsigned long flags = 0;

	if (usb_clk->lock)
		spin_lock_irqsave(usb_clk->lock, flags);

	reg = clk_readl(usb_clk->reg);

	reg &= ~BIT(usb_clk->bit_idx);

	clk_writel(reg, usb_clk->reg);

	if (usb_clk->lock)
		spin_unlock_irqrestore(usb_clk->lock, flags);
}

static int clk_usb20_is_enabled(struct clk_hw *hw)
{
	u32 reg;
	struct clk_usb20 *usb_clk = to_clk_usb20(hw);

	reg = clk_readl(usb_clk->reg);

	return (reg & BIT(usb_clk->bit_idx)) ? 1 : 0;
}

#define ABS_DELTA(a,b)  ((a) > (b) ? (a) - (b) : (b) - (a))

unsigned long clk_usb20_best_rate(unsigned long rate, unsigned long *ret_pll_div,
				unsigned long *ret_src)
{
	unsigned long apll_rate, upll_rate, xin_rate, pll_div, clk_div;
	unsigned long best_rate, best_pll_div, best_clk_div, best_src;
	unsigned long test_rate, test_pll_div, test_clk_div;

	apll_rate = clk_get_rate(n329_clocks_get(apll_clk));
	upll_rate = clk_get_rate(n329_clocks_get(upll_clk));
	xin_rate = clk_get_rate(n329_clocks_get(xtal_clk));
	pll_div = (1 << 3);
	clk_div = (1 << 4);

	best_rate = 0xffffffff;
	best_pll_div = 0;
	best_clk_div = 0;
	best_src = 0;

	/* Test best rate for the xin input */
	for (test_clk_div = 0; test_clk_div < clk_div; ++test_clk_div) {
		test_rate = xin_rate / (test_clk_div + 1);
		if ((ABS_DELTA(rate, test_rate) < 
				ABS_DELTA(rate, best_rate))) {
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

static unsigned long clk_usb20_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	if (!clk_usb20_is_enabled(hw))
		return 0;

	return parent_rate;
}

static long clk_usb20_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	unsigned long best_rate;

	if (!clk_usb20_is_enabled(hw))
		best_rate = 0;
	else
		best_rate = clk_usb20_best_rate(rate, NULL, NULL);

	return best_rate;
}

static int clk_usb20_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	unsigned long best_src;
	unsigned long best_pll_div;
	unsigned long best_rate;

	best_rate = clk_usb20_best_rate(rate, &best_pll_div, &best_src);

	if (best_src == 3) {
		clk_set_parent(n329_clocks_get(usb20_uclk),
				n329_clocks_get(udiv0_clk + best_pll_div));
		clk_set_parent(n329_clocks_get(usb20_src), 
				n329_clocks_get(usb20_uclk));
	} else if (best_src == 2) {
		clk_set_parent(n329_clocks_get(usb20_aclk),
				n329_clocks_get(adiv0_clk + best_pll_div));
		clk_set_parent(n329_clocks_get(usb20_src), 
				n329_clocks_get(usb20_aclk));
	} else {
		clk_set_parent(n329_clocks_get(usb20_src), 
				n329_clocks_get(xtal_clk));
	}
	clk_set_rate(n329_clocks_get(usb20_div), best_rate);

	return 0;
}

const struct clk_ops clk_usb20_ops = {
	.enable = clk_usb20_enable,
	.disable = clk_usb20_disable,
	.is_enabled = clk_usb20_is_enabled,
	.recalc_rate = clk_usb20_recalc_rate,
	.round_rate = clk_usb20_round_rate,
	.set_rate = clk_usb20_set_rate,
};
EXPORT_SYMBOL_GPL(clk_usb20_ops);

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
struct clk *clk_register_usb20(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		spinlock_t *lock)
{
	struct clk_usb20 *usb_clk;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the usb_clk */
	usb_clk = kzalloc(sizeof(struct clk_usb20), GFP_KERNEL);
	if (!usb_clk) {
		pr_err("%s: could not allocate gated clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_usb20_ops;
	init.flags = (flags & ~CLK_SET_RATE_PARENT) | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_usb20 assignments */
	usb_clk->reg = reg;
	usb_clk->bit_idx = bit_idx;
	usb_clk->lock = lock;
	usb_clk->hw.init = &init;

	clk = clk_register(dev, &usb_clk->hw);

	if (IS_ERR(clk))
		kfree(usb_clk);

	return clk;
}
EXPORT_SYMBOL_GPL(clk_register_usb20);
