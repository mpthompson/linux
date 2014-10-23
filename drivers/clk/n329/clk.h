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

#ifndef __N329_CLK_H
#define __N329_CLK_H

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>

#define KHZ     1000
#define MHZ     (KHZ * KHZ)

extern spinlock_t n329_lock;

struct clk *clk_register_pll(const char *name, const char *parent_name,
			void __iomem *base, spinlock_t *lock);

struct clk *clk_register_and_gate(struct device *dev, const char *name,
			const char *parent_name, unsigned long flags,
			void __iomem *reg, u8 bit1_idx, u8 bit2_idx,
			u8 clk_gate_flags, spinlock_t *lock);

struct clk *clk_register_split_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags, void __iomem *reg, 
		u8 lo_shift, u8 lo_width, u8 hi_shift, u8 hi_width,
		u8 clk_divider_flags, spinlock_t *lock);

static inline struct clk *n329_clk_fixed(const char *name, int rate)
{
	return clk_register_fixed_rate(NULL, name, NULL, CLK_IS_ROOT, rate);
}

static inline struct clk *n329_clk_pll(const char *name,
			const char *parent_name, void __iomem *reg)
{
	return clk_register_pll(name, parent_name, reg, &n329_lock);
}

static inline struct clk *n329_clk_gate(const char *name,
			const char *parent_name, void __iomem *reg, u8 shift)
{
	return clk_register_gate(NULL, name, parent_name, CLK_SET_RATE_PARENT,
				 reg, shift, 0, &n329_lock);
}

static inline struct clk *n329_clk_and_gate(const char *name,
			const char *parent_name, void __iomem *reg, u8 shift1, u8 shift2)
{
	return clk_register_and_gate(NULL, name, parent_name, CLK_SET_RATE_PARENT,
				 reg, shift1, shift2, 0, &n329_lock);
}

static inline struct clk *n329_clk_mux(const char *name, void __iomem *reg,
			u8 shift, u8 width, const char **parent_names, int num_parents)
{
	return clk_register_mux(NULL, name, parent_names, num_parents,
				CLK_SET_RATE_PARENT, reg, shift, width, 0, &n329_lock);
}

static inline struct clk *n329_clk_div(const char *name, 
			const char *parent_name, void __iomem *reg, u8 shift, u8 width)
{
	return clk_register_divider(NULL, name, parent_name, 0, 
				reg, shift, width, 0, &n329_lock);
}

static inline struct clk *n329_clk_split_div(const char *name, 
			const char *parent_name, void __iomem *reg, 
			u8 lo_shift, u8 lo_width, u8 hi_shift, u8 hi_width)
{
	return clk_register_split_divider(NULL, name, parent_name, 
				CLK_SET_RATE_PARENT, reg, lo_shift, lo_width, 
				hi_shift, hi_width, 0, &n329_lock);
}

static inline struct clk *n329_clk_source_div(const char *name, 
			const char *parent_name, void __iomem *reg, 
			u8 shift, u8 width)
{
	return clk_register_divider(NULL, name, parent_name, 
				CLK_SET_RATE_PARENT, reg, shift, width, 
				0, &n329_lock);
}

static inline struct clk *n329_clk_table_div(const char *name, 
			const char *parent_name, void __iomem *reg, 
			u8 shift, u8 width, const struct clk_div_table *table)
{
	return clk_register_divider_table(NULL, name, parent_name, 0, 
			reg, shift, width, 0, table, &n329_lock);
}

static inline struct clk *n329_clk_fixed_div(const char *name,
		const char *parent_name, unsigned int div)
{
	return clk_register_fixed_factor(NULL, name, parent_name,
					 0, 1, div);
}

#endif /* __N329_CLK_H */
