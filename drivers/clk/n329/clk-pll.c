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

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include "clk.h"

/**
 * struct clk_pll - n329 apll and upll clock
 * @hw: clk_hw for the pll
 * @base: base address of the pll
 *
 * The n329 pll is a variable rate clock with power and gate control.
 */
struct clk_pll {
	struct clk_hw   hw;
	void __iomem    *base;
	spinlock_t      *lock;
};

#define ABS_DELTA(a,b)  ((a) > (b) ? (a) - (b) : (b) - (a))

static int clk_pll_is_enabled(struct clk_hw *hw)
{
	int state;
	u32 pllcon;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);

	pllcon = __raw_readl(pll->base);
	state = ~pllcon & BIT(16) ? 1 : 0;	/* powered down */
	if (state)
		state = ~pllcon & BIT(18) ? 1 : 0; /* enabled */
	return state;
}

static int clk_pll_enable(struct clk_hw *hw)
{
	u32 pllcon;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);

	pllcon = __raw_readl(pll->base);
	pllcon &= ~BIT(18); /* enable */
	pllcon &= ~BIT(16); /* power up */
	__raw_writel(pllcon, pll->base);

	return 0;
}

static void clk_pll_disable(struct clk_hw *hw)
{
	u32 pllcon;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);

	pllcon = __raw_readl(pll->base);
	pllcon |= BIT(18); /* disable */
	__raw_writel(pllcon, pll->base);
}

static u32 clk_pll_calc_rate(u32 fin, u32 nf, u32 nr, u32 no)
{
	u64 fout;
	
	/* fout = fin * nf / nr / no */
	/* avoid 64 bit / 64 bit division in kernel */
#ifndef do_div
	fout = (u64) fin * nf / nr / no;
#else
	fout = (u64) fin * nf;
	do_div(fout, nr);
	do_div(fout, no);
#endif

	return (u32) fout;
}

static u32 clk_pll_find_rate(u32 fin, u32 fout,
					u32 *found_nf, u32 *found_nr, u32 *found_no)
{
	u32 nf, nr, no;
	u32 try_fout, best_fout;
	u32 best_nf, best_nr, best_no;

	/* flag to capture the first default values */
	best_fout = 0;
	best_nr = 2;
	best_nf = 48;
	best_no = 4;

	/* try output divider values 1, 2 and 4 values */
	for (no = 1; no <= 4; no <<= 1) {

		/* try input divider values 2 thru 33 */
		for (nr = 33; nr >= 2; --nr) {

			/* nr constraint -- 1 MHz < fin / nr < 15 MHz */
			if (((fin / nr) <= 1000000UL) || 
				((fin / nr) >= 15000000UL)) {
				continue;
			}

			/* determine feedback divider to try */
			/* avoid 64 bit / 64 bit division in kernel */
#ifndef do_div
			nf = (u32) ((u64) fout * nr * no / fin);
#else
			{
				u64 tmp = (u64) fout * nr * no;
				do_div(tmp, fin);
				nf = (u32) tmp;
			}
#endif

			/* nf constraint */
			if ((nf < 2) || (nf > 513)) {
				continue;
			}

			/* calculate fout with these values */
			try_fout = clk_pll_calc_rate(fin, nf, nr, no);

			/* no constraint -- 100 MHz <= fout * no <= 500 MHz */
			if (((try_fout * no) < 100000000UL) || 
				((try_fout * no) > 500000000UL)) {
				continue;
			}

			/* capture the first valid values */
			if (!best_fout) {
				best_fout = try_fout;
				best_no = no;
				best_nr = nr;
				best_nf = nf;
			}

			/* save the new best values if this the best so far */
			if (ABS_DELTA(fout, try_fout) <= ABS_DELTA(fout, best_fout)) {
				best_fout = try_fout;
				best_no = no;
				best_nr = nr;
				best_nf = nf;
			}

			/* increment nf by one */
			++nf;

			/* nf constraint */
			if (nf > 513) {
				continue;
			}

			/* calculate fout with these values */
			try_fout = clk_pll_calc_rate(fin, nf, nr, no);

			/* no constraint -- 100 MHz <= fout * no <= 500 MHz */
			if (((try_fout * no) < 100000000UL) || 
				((try_fout * no) > 500000000UL)) {
				continue;
			}

			/* save the new best values if this the best so far */
			if (ABS_DELTA(fout, try_fout) <= ABS_DELTA(fout, best_fout)) {
				best_fout = try_fout;
				best_no = no;
				best_nr = nr;
				best_nf = nf;
			}
		}
	} 

	/* manufacture defaults if no valid combination found */
	if (!best_fout) {
		best_nr = 2;
		best_nf = 48;
		best_no = 4;
		best_fout = clk_pll_calc_rate(fin, best_nf, best_nr, best_no);
	}

	/* return the best values */
	if (found_no) *found_no = best_no;
	if (found_nr) *found_nr = best_nr;
	if (found_nf) *found_nf = best_nf;

	/* return the nearest fout found */
	return best_fout;
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	unsigned long rate;
	unsigned long fin = parent_rate;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);

	/* read the configuration register */
	u32 pllcon = __raw_readl(pll->base);

	pr_info("pllcon reg: 0x%08x\n", pllcon);

	if (pllcon & BIT(16)) {
		/* pll power down */
		/* assume no output when powered down */
		pr_info("pllcon power down\n");
		rate = 0;
	} else if (pllcon & BIT(18)) {
		/* pll output disable */
		pr_info("pllcon disabled\n");
		rate = 0;
	} else if (pllcon & BIT(17)) {
		/* pll bypass mode */
		/* assume bypass does not work when powered down or disabled */
		pr_info("pllcon bypass\n");
		rate = fin;
	} else {
		/* fout = fin * nf / nr / no */
		u32 nf = (pllcon & (BIT(9) - 1)) + 2;
		u32 nr = ((pllcon >> 9) & (BIT(5) - 1)) + 2;
		u32 no = ((pllcon >> 14) & (BIT(2) - 1));
		if (no == 0)
			no = 1;
		else if (no == 1) 
			no = 2;
		else if (no == 2)
			no = 2;
		else 
			no = 4;
		WARN_ON(fin % MHZ);
		pr_info("pllcon fin: %lu nf: %lu nr: %lu no: %lu\n", fin, nf, nr, no);
		rate = clk_pll_calc_rate(fin, nf, nr, no);
	}

	pr_info("pllcon fout: %lu\n", rate);

	return rate;
}

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	u32 fin, fout, nearest_fout;

	/* parent rate */
	fin = *parent_rate;

	/* desired output rate */
	fout = rate;

	/* are input and output the same */
	if (fin == fout) {
		/* can we bypass the clock */
		nearest_fout = fout;
	} else {
		/* determine nearest rate */
		nearest_fout = clk_pll_find_rate(fin, fout, NULL, NULL, NULL);
	}
 
	return (long) nearest_fout;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long parent_rate)
{
	u32 in_dv, out_dv, fb_dv; 
	u32 fin, fout, best_fout, nf, nr, no, pllcon;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);
	unsigned long flags = 0;

	/* parent rate */
	fin = parent_rate;

	/* desired output rate */
	fout = rate;

	/* adjustment to mhz */
	fout = fout - fout % MHZ;

	/* handle special case */
	if (fout == fin) {
		if (pll->lock)
			spin_lock_irqsave(pll->lock, flags);

		/* bypass pll */
		pllcon = __raw_readl(pll->base);
		pllcon |= BIT(17);
		__raw_writel(pllcon, pll->base);

		if (pll->lock)
			spin_unlock_irqrestore(pll->lock, flags);
	} else {
		/* determine nearest best rate */
		best_fout = clk_pll_find_rate(fin, fout, &nf, &nr, &no);

		/* should match */
		WARN_ON(fout - best_fout);

		/* prepare register values */
		fb_dv = nf - 2;
		in_dv = nr - 2;
		if (no == 1)
			out_dv = 0;
		else if (no == 2)
			out_dv = 1;
		else
			out_dv = 3;

		if (pll->lock)
			spin_lock_irqsave(pll->lock, flags);

		/* configure the pll control register */
		pllcon = __raw_readl(pll->base);
		pllcon &= ~((BIT(2) - 1) << 14);
		pllcon &= ~((BIT(5) - 1) << 9);
		pllcon &= ~(BIT(9) - 1);
		pllcon &= ~BIT(17);
		pllcon |= (out_dv << 14);
		pllcon |= (in_dv << 9);
		pllcon |= fb_dv;
		__raw_writel(pllcon, pll->base);

		if (pll->lock)
			spin_unlock_irqrestore(pll->lock, flags);
	}

	return 0;
}

static const struct clk_ops clk_pll_ops = {
	.is_enabled = clk_pll_is_enabled,
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_round_rate,
	.set_rate = clk_pll_set_rate,
};

struct clk *clk_register_pll(const char *name, const char *parent_name,
			void __iomem *base, spinlock_t *lock)
{
	struct clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_pll_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	pll->base = base;
	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}
