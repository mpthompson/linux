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

enum n329_clk {
	xtal_clk	= 0,
	rtx_clk		= 1,
	apll_clk	= 2,
	upll_clk	= 3,
	reserved_clk	= 4,
	adiv0_clk	= 5,
	adiv1_clk	= 6,
	adiv2_clk	= 7,
	adiv3_clk	= 8,
	adiv4_clk	= 9,
	adiv5_clk	= 10,
	adiv6_clk	= 11,
	adiv7_clk	= 12,
	udiv0_clk	= 13,
	udiv1_clk	= 14,
	udiv2_clk	= 15,
	udiv3_clk	= 16,
	udiv4_clk	= 17,
	udiv5_clk	= 18,
	udiv6_clk	= 19,
	udiv7_clk	= 20,
	adc_aclk	= 21,
	adc_uclk	= 22,
	adc_src		= 23,
	adc_div		= 24,
	adc_clk		= 25,
	ado_aclk	= 26,
	ado_uclk	= 27,
	ado_src		= 28,
	ado_div		= 29,
	ado_clk		= 30,
	vpost_aclk	= 31,
	vpost_uclk	= 32,
	vpost_src	= 33,
	vpost_div	= 34,
	vpost_clk	= 35,
	vpostd2_div	= 36,
	vpostd2_clk	= 37,
	vpost_hclk	= 38,
	sd_aclk		= 39,
	sd_uclk		= 40,
	sd_src		= 41,
	sd_div		= 42,
	sd_clk		= 43,
	sen_aclk	= 44,
	sen_uclk	= 45,
	sen_src		= 46,
	sen_div		= 47,
	sen_clk		= 48,
	usb_aclk	= 49,
	usb_uclk	= 50,
	usb_src		= 51,
	usb_div		= 52,
	usb_clk		= 53,
	usbh_hclk	= 54,
	usb20_aclk	= 55,
	usb20_uclk	= 56,
	usb20_src	= 57,
	usb20_div	= 58,
	usb20_clk	= 59,
	usb20_hclk	= 60,
	uart0_aclk	= 61,
	uart0_uclk	= 62,
	uart0_src	= 63,
	uart0_div	= 64,
	uart0_clk	= 65,
	uart1_aclk	= 66,
	uart1_uclk	= 67,
	uart1_src	= 68,
	uart1_div	= 69,
	uart1_clk	= 70,
	sys_aclk	= 71,
	sys_uclk	= 72,
	sys_src		= 73,
	sys_clk		= 74,
	gpio_src	= 75,
	gpio_div	= 76,
	gpio_clk	= 77,
	kpi_src		= 78,
	kpi_div		= 79,
	kpi_clk		= 80,
	cpu_div		= 81,
	cpu_clk		= 82,
	hclk_div	= 83,
	hclk1_div	= 84,
	hclk234_div	= 85,
	hclk_clk	= 86,
	hclk1_clk	= 87,
	hclk2_clk	= 88,
	hclk3_clk	= 89,
	hclk4_clk	= 90,
	jpg_div		= 91,
	jpg_eclk	= 92,
	jpg_hclk	= 93,
	cap_div		= 94,
	cap_eclk	= 95,
	cap_hclk	= 96,
	edma0_hclk	= 97,
	edma1_hclk	= 98,
	edma2_hclk	= 99,
	edma3_hclk	= 100,
	edma4_hclk	= 101,
	fsc_hclk	= 102,
	dram_clk	= 103,
	sram_clk	= 104,
	ddr_clk		= 105,
	blt_hclk	= 106,
	sic_hclk	= 107,
	nand_hclk	= 108,
	spu_hclk	= 109,
	i2s_hclk	= 110,
	spu1_clk	= 111,
	pclk_div	= 112,
	pclk_clk	= 113,
	adc_pclk	= 114,
	i2c_pclk	= 115,
	rtc_pclk	= 116,
	uart0_pclk	= 117,
	uart1_pclk	= 118,
	pwm_pclk	= 119,
	spims0_pclk	= 120,
	spims1_pclk	= 121,
	timer0_pclk	= 122,
	timer1_pclk	= 123,
	wdt_pclk	= 124,
	tic_pclk	= 125,
	kpi_pclk	= 126,
	clk_max
};

extern spinlock_t n329_lock;

struct clk *n329_clocks_get(enum n329_clk idx);

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

struct clk *clk_register_sd(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx, spinlock_t *lock);

struct clk *clk_register_usb(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx, spinlock_t *lock);

struct clk *clk_register_usb20(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx, spinlock_t *lock);

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

static inline struct clk *n329_clk_sd(const char *name,
		const char *parent_name, void __iomem *reg, u8 shift)
{
	return clk_register_sd(NULL, name, parent_name, 0,
			 reg, shift, &n329_lock);
}

static inline struct clk *n329_clk_usb(const char *name,
		const char *parent_name, void __iomem *reg, u8 shift)
{
	return clk_register_usb(NULL, name, parent_name, 0,
			 reg, shift, &n329_lock);
}

static inline struct clk *n329_clk_usb20(const char *name,
		const char *parent_name, void __iomem *reg, u8 shift)
{
	return clk_register_usb20(NULL, name, parent_name, 0,
			 reg, shift, &n329_lock);
}

#endif /* __N329_CLK_H */
