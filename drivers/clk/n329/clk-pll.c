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

/*
 * struct clk_pll - n329 apll and upll clock
 * @hw: clk_hw for the pll
 * @base: base address of the pll
 *
 * The N329XX pll is a variable rate clock with power and gate control.
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
	unsigned long pllcon;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);

	pllcon = __raw_readl(pll->base);
	state = ~pllcon & BIT(16) ? 1 : 0;	/* Powered down */
	if (state)
		state = ~pllcon & BIT(18) ? 1 : 0; /* Enabled */
	return state;
}

static int clk_pll_enable(struct clk_hw *hw)
{
	unsigned long pllcon;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);

	pllcon = __raw_readl(pll->base);
	pllcon &= ~BIT(18); /* enable */
	pllcon &= ~BIT(16); /* power up */
	__raw_writel(pllcon, pll->base);

	return 0;
}

static void clk_pll_disable(struct clk_hw *hw)
{
	unsigned long pllcon;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);

	pllcon = __raw_readl(pll->base);
	pllcon |= BIT(18); /* disable */
	__raw_writel(pllcon, pll->base);
}

static unsigned long clk_pll_calc_rate(unsigned long fin, 
			unsigned long nf, unsigned long nr, unsigned long no)
{
	u64 fout;
	
	/* fout = fin * nf / nr / no
	 * avoid 64 bit / 64 bit division in kernel 
	 */
#ifndef do_div
	fout = (u64) fin * nf / nr / no;
#else
	fout = (u64) fin * nf;
	do_div(fout, nr);
	do_div(fout, no);
#endif

	return (unsigned long) fout;
}

static unsigned long clk_pll_find_rate(unsigned long fin, 
			unsigned long fout, unsigned long *found_nf, 
			unsigned long *found_nr, unsigned long *found_no)
{
	unsigned long nf, nr, no;
	unsigned long try_fout, best_fout;
	unsigned long best_nf, best_nr, best_no;

	/* Flag to capture the first default values */
	best_fout = 0;
	best_nr = 2;
	best_nf = 48;
	best_no = 4;

	/* Try output divider values 1, 2 and 4 values */
	for (no = 1; no <= 4; no <<= 1) {

		/* Try input divider values 2 thru 33 */
		for (nr = 33; nr >= 2; --nr) {

			/* nr constraint -- 1 MHz < fin / nr < 15 MHz */
			if (((fin / nr) <= 1000000UL) || 
				((fin / nr) >= 15000000UL)) {
				continue;
			}

			/* Determine feedback divider to try
			 * avoid 64 bit / 64 bit division in kernel
			 */
#ifndef do_div
			nf = (unsigned long) ((u64) fout * nr * no / fin);
#else
			{
				u64 tmp = (u64) fout * nr * no;
				do_div(tmp, fin);
				nf = (unsigned long) tmp;
			}
#endif

			/* nf constraint */
			if ((nf < 2) || (nf > 513)) {
				continue;
			}

			/* Calculate fout with these values */
			try_fout = clk_pll_calc_rate(fin, nf, nr, no);

			/* No constraint -- 100 MHz <= fout * no <= 500 MHz */
			if (((try_fout * no) < 100000000UL) || 
				((try_fout * no) > 500000000UL)) {
				continue;
			}

			/* Capture the first valid values */
			if (!best_fout) {
				best_fout = try_fout;
				best_no = no;
				best_nr = nr;
				best_nf = nf;
			}

			/* Save the new best values if this the best so far */
			if (ABS_DELTA(fout, try_fout) <= 
					ABS_DELTA(fout, best_fout)) {
				best_fout = try_fout;
				best_no = no;
				best_nr = nr;
				best_nf = nf;
			}

			++nf;

			if (nf > 513) {
				continue;
			}

			/* Calculate fout with these values */
			try_fout = clk_pll_calc_rate(fin, nf, nr, no);

			/* No constraint -- 100 MHz <= fout * no <= 500 MHz */
			if (((try_fout * no) < 100000000UL) || 
				((try_fout * no) > 500000000UL)) {
				continue;
			}

			/* Save the new best values if this the best so far */
			if (ABS_DELTA(fout, try_fout) <= 
					ABS_DELTA(fout, best_fout)) {
				best_fout = try_fout;
				best_no = no;
				best_nr = nr;
				best_nf = nf;
			}
		}
	} 

	/* Manufacture defaults if no valid combination found */
	if (!best_fout) {
		best_nr = 2;
		best_nf = 48;
		best_no = 4;
		best_fout = clk_pll_calc_rate(fin, best_nf, 
						best_nr, best_no);
	}

	/* Return the best values */
	if (found_no) *found_no = best_no;
	if (found_nr) *found_nr = best_nr;
	if (found_nf) *found_nf = best_nf;

	/* Return the nearest fout found */
	return best_fout;
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	unsigned long fout;
	unsigned long fin = parent_rate;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);

	/* Read the configuration register */
	unsigned long pllcon = __raw_readl(pll->base);

	pr_devel("pllcon reg: 0x%08lx\n", pllcon);

	if (pllcon & BIT(16)) {
		/* PLL power down */
		/* Assume no output when powered down */
		pr_devel("pllcon power down\n");
		fout = 0;
	} else if (pllcon & BIT(18)) {
		/* PLL output disable */
		pr_devel("pllcon disabled\n");
		fout = 0;
	} else if (pllcon & BIT(17)) {
		/* PLL bypass mode */
		/* Assume bypass does not work when powered down or disabled */
		pr_devel("pllcon bypass\n");
		fout = fin;
	} else {
		/* fout = fin * nf / nr / no */
		unsigned long nf = (pllcon & (BIT(9) - 1)) + 2;
		unsigned long nr = ((pllcon >> 9) & (BIT(5) - 1)) + 2;
		unsigned long no = ((pllcon >> 14) & (BIT(2) - 1));
		if (no == 0)
			no = 1;
		else if (no == 1) 
			no = 2;
		else if (no == 2)
			no = 2;
		else 
			no = 4;
		WARN_ON(fin % MHZ);
		pr_devel("pllcon fin: %lu nf: %lu nr: %lu no: %lu\n", 
					fin, nf, nr, no);
		fout = clk_pll_calc_rate(fin, nf, nr, no);
	}

	pr_devel("pllcon fout: %lu\n", fout);

	return fout;
}

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *parent_rate)
{
	unsigned long fin, fout, nearest_fout;

	/* Parent rate */
	fin = *parent_rate;

	/* Desired output rate */
	fout = rate;

	/* Are input and output the same? */
	if (fin == fout) {
		/* We bypass the clock */
		nearest_fout = fout;
	} else {
		/* Determine nearest rate */
		nearest_fout = clk_pll_find_rate(fin, fout, NULL, NULL, NULL);
	}
 
	return (long) nearest_fout;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	unsigned long in_dv, out_dv, fb_dv; 
	unsigned long fin, fout, best_fout, nf, nr, no, pllcon;
	struct clk_pll *pll = container_of(hw, struct clk_pll, hw);
	unsigned long flags = 0;

	/* Parent rate */
	fin = parent_rate;

	/* Desired output rate */
	fout = rate;

	/* Adjustment to mhz */
	fout = fout - fout % MHZ;

	/* Handle special case */
	if (fout == fin) {
		if (pll->lock)
			spin_lock_irqsave(pll->lock, flags);

		/* Bypass pll */
		pllcon = __raw_readl(pll->base);
		pllcon |= BIT(17);
		__raw_writel(pllcon, pll->base);

		if (pll->lock)
			spin_unlock_irqrestore(pll->lock, flags);
	} else {
		/* Determine nearest best rate */
		best_fout = clk_pll_find_rate(fin, fout, &nf, &nr, &no);

		/* Should match */
		WARN_ON(fout - best_fout);

		/* Prepare register values */
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

		/* Configure the pll control register */
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
