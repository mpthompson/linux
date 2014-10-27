/*
 * Copyright (C) 2010-2011 Canonical Ltd <jeremy.kerr@canonical.com>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
 * Copyright (C) 2014 Michael P. Thompson <mpthompson@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Gated clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>

/**
 * DOC: basic gatable clock which can gate and ungate it's ouput
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gating
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

struct clk_and_gate {
	struct clk_hw hw;
	void __iomem    *reg;
	u8              bit1_idx;
	u8              bit2_idx;
	u8              flags;
	spinlock_t      *lock;
};

#define to_clk_gate(_hw) container_of(_hw, struct clk_and_gate, hw)

static int clk_gate_enable(struct clk_hw *hw)
{
	struct clk_and_gate *gate = to_clk_gate(hw);
	u32 reg;
	unsigned long flags = 0;

	if (gate->lock)
		spin_lock_irqsave(gate->lock, flags);

	reg = clk_readl(gate->reg);

	reg |= BIT(gate->bit1_idx);
	reg |= BIT(gate->bit2_idx);

	clk_writel(reg, gate->reg);

	if (gate->lock)
		spin_unlock_irqrestore(gate->lock, flags);

	return 0;
}

static void clk_gate_disable(struct clk_hw *hw)
{
	struct clk_and_gate *gate = to_clk_gate(hw);
	u32 reg;
	unsigned long flags = 0;

	if (gate->lock)
		spin_lock_irqsave(gate->lock, flags);

	reg = clk_readl(gate->reg);

	reg &= ~BIT(gate->bit1_idx);
	reg &= ~BIT(gate->bit2_idx);

	clk_writel(reg, gate->reg);

	if (gate->lock)
		spin_unlock_irqrestore(gate->lock, flags);
}

static int clk_gate_is_enabled(struct clk_hw *hw)
{
	u32 reg;
	struct clk_and_gate *gate = to_clk_gate(hw);

	reg = clk_readl(gate->reg);

	return (reg & BIT(gate->bit1_idx)) && 
			(reg & BIT(gate->bit2_idx)) ? 1 : 0;
}

const struct clk_ops clk_and_gate_ops = {
	.enable = clk_gate_enable,
	.disable = clk_gate_disable,
	.is_enabled = clk_gate_is_enabled,
};

/**
 * clk_register_and_gate - register a gate clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of this clock's parent
 * @flags: framework-specific flags for this clock
 * @reg: register address to control gating of this clock
 * @bit1_idx: which bit in the register controls gating of this clock
 * @bit2_idx: which bit in the register controls gating of this clock
 * @clk_gate_flags: gate-specific flags for this clock
 * @lock: shared register lock for this clock
 */
struct clk *clk_register_and_gate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit1_idx, u8 bit2_idx,
		u8 clk_gate_flags, spinlock_t *lock)
{
	struct clk_and_gate *gate;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the gate */
	gate = kzalloc(sizeof(struct clk_and_gate), GFP_KERNEL);
	if (!gate) {
		pr_err("%s: could not allocate gated clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_and_gate_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_and_gate assignments */
	gate->reg = reg;
	gate->bit1_idx = bit1_idx;
	gate->bit2_idx = bit2_idx;
	gate->flags = clk_gate_flags;
	gate->lock = lock;
	gate->hw.init = &init;

	clk = clk_register(dev, &gate->hw);

	if (IS_ERR(clk))
		kfree(gate);

	return clk;
}
