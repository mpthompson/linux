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
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "clk.h"

static void __iomem *clkctrl;
static void __iomem *gcrctrl;

#define HW_GCR_CHIPCFG	0x04	/* R/W Chip Power-On Configuration */

#define HW_CLK_PWRCON 	0x00	/* R/W System Power Down Control */
#define HW_CLK_AHBCLK 	0x04	/* R/W Clock Enable Control */
#define HW_CLK_APBCLK 	0x08	/* R/W Clock Enable Control */
#define HW_CLK_CLKDIV0 	0x0C	/* R/W Clock Divider Number */
#define HW_CLK_CLKDIV1 	0x10	/* R/W Clock Divider Number */
#define HW_CLK_CLKDIV2 	0x14	/* R/W Clock Divider Number */
#define HW_CLK_CLKDIV3 	0x18	/* R/W Clock Divider Number */
#define HW_CLK_CLKDIV4 	0x1C	/* R/W Clock Divider Number */
#define HW_CLK_APLLCON 	0x20	/* R/W APLL Control */
#define HW_CLK_UPLLCON 	0x24	/* R/W UPLL Control */
#define HW_CLK_TREG 	0x30	/* R/W TEST Clock Control */

#define CLKCTRL clkctrl
#define DIGCTRL gcrctrl

#define REG_PWRCON		(CLKCTRL + HW_CLK_PWRCON)
#define REG_AHBCLK		(CLKCTRL + HW_CLK_AHBCLK)
#define REG_APBCLK		(CLKCTRL + HW_CLK_APBCLK)
#define REG_CLKDIV0		(CLKCTRL + HW_CLK_CLKDIV0)
#define REG_CLKDIV1		(CLKCTRL + HW_CLK_CLKDIV1)
#define REG_CLKDIV2		(CLKCTRL + HW_CLK_CLKDIV2)
#define REG_CLKDIV3		(CLKCTRL + HW_CLK_CLKDIV3)
#define REG_CLKDIV4		(CLKCTRL + HW_CLK_CLKDIV4)
#define REG_APLLCON		(CLKCTRL + HW_CLK_APLLCON)
#define REG_UPLLCON		(CLKCTRL + HW_CLK_UPLLCON)
#define REG_TREG		(CLKCTRL + HW_CLK_TREG)

static const char *sel_apll[] __initconst = { "adiv0_clk", "adiv1_clk",
											  "adiv2_clk", "adiv3_clk",
											  "adiv4_clk", "adiv5_clk",
											  "adiv6_clk", "adiv7_clk", };
static const char *sel_upll[] __initconst = { "udiv0_clk", "udiv1_clk",
											  "udiv2_clk", "udiv3_clk",
											  "udiv4_clk", "udiv5_clk",
											  "udiv6_clk", "udiv7_clk", };
static const char *sel_adc_src[] __initconst = { "xtal_clk", "reserved_clk",
												 "adc_aclk", "adc_uclk", };
static const char *sel_ado_src[] __initconst = { "xtal_clk", "reserved_clk",
												 "ado_aclk", "ado_uclk", };
static const char *sel_vpost_src[] __initconst = { "xtal_clk", "reserved_clk",
												 "vpost_aclk", "vpost_uclk", };
static const char *sel_sd_src[] __initconst = { "xtal_clk", "reserved_clk",
												 "sd_aclk", "sd_uclk", };
static const char *sel_sen_src[] __initconst = { "xtal_clk", "reserved_clk",
												 "sen_aclk", "sen_uclk", };
static const char *sel_usb_src[] __initconst = { "xtal_clk", "reserved_clk",
												 "usb_aclk", "usb_uclk", };
static const char *sel_usb20_src[] __initconst = { "xtal_clk", "reserved_clk",
												   "usb20_aclk", "usb20_uclk", };
static const char *sel_uart0_src[] __initconst = { "xtal_clk", "reserved_clk",
												   "uart0_aclk", "uart0_uclk", };
static const char *sel_uart1_src[] __initconst = { "xtal_clk", "reserved_clk",
												   "uart1_aclk", "uart1_uclk", };
static const char *sel_sys_src[] __initconst = { "xtal_clk", "reserved_clk",
												 "sys_aclk", "sys_uclk", };
static const char *sel_gpio_src[] __initconst = { "xtal_clk", "rtx_clk", };
static const char *sel_kpi_src[] __initconst = { "xtal_clk", "rtx_clk", };

static struct clk_div_table hclk1_div_table[] = {
	{ .val = 1, .div = 1, },
	{ .val = 0, .div = 2, },
	{ }
};

enum n329_clk {
	xtal_clk		= 0,
	rtx_clk			= 1,
	apll_clk		= 2,
	upll_clk		= 3,
	reserved_clk	= 4,
	adiv0_clk		= 5,
	adiv1_clk		= 6,
	adiv2_clk		= 7,
	adiv3_clk		= 8,
	adiv4_clk		= 9,
	adiv5_clk		= 10,
	adiv6_clk		= 11,
	adiv7_clk		= 12,
	udiv0_clk		= 13,
	udiv1_clk		= 14,
	udiv2_clk		= 15,
	udiv3_clk		= 16,
	udiv4_clk		= 17,
	udiv5_clk		= 18,
	udiv6_clk		= 19,
	udiv7_clk		= 20,
	adc_aclk		= 21,
	adc_uclk		= 22,
	adc_src			= 23,
	adc_div			= 24,
	adc_clk			= 25,
	ado_aclk		= 26,
	ado_uclk		= 27,
	ado_src			= 28,
	ado_div			= 29,
	ado_clk			= 30,
	vpost_aclk		= 31,
	vpost_uclk		= 32,
	vpost_src		= 33,
	vpost_div		= 34,
	vpost_clk		= 35,
	vpostd2_div		= 36,
	vpostd2_clk		= 37,
	vpost_hclk		= 38,
	sd_aclk			= 39,
	sd_uclk			= 40,
	sd_src			= 41,
	sd_div			= 42,
	sd_clk			= 43,
	sen_aclk		= 44,
	sen_uclk		= 45,
	sen_src			= 46,
	sen_div			= 47,
	sen_clk			= 48,
	usb_aclk		= 49,
	usb_uclk		= 50,
	usb_src			= 51,
	usb_div			= 52,
	usb_clk			= 53,
	usbh_hclk		= 54,
	usb20_aclk		= 55,
	usb20_uclk		= 56,
	usb20_src		= 57,
	usb20_div		= 58,
	usb20_clk		= 59,
	usb20_hclk		= 60,
	uart0_aclk		= 61,
	uart0_uclk		= 62,
	uart0_src		= 63,
	uart0_div		= 64,
	uart0_clk		= 65,
	uart1_aclk		= 66,
	uart1_uclk		= 67,
	uart1_src		= 68,
	uart1_div		= 69,
	uart1_clk		= 70,
	sys_aclk		= 71,
	sys_uclk		= 72,
	sys_src			= 73,
	sys_clk			= 74,
	gpio_src		= 75,
	gpio_div		= 76,
	gpio_clk		= 77,
	kpi_src			= 78,
	kpi_div			= 79,
	kpi_clk			= 80,
	cpu_div			= 81,
	cpu_clk			= 82,
	hclk_div		= 83,
	hclk1_div		= 84,
	hclk234_div		= 85,
	hclk_clk		= 86,
	hclk1_clk		= 87,
	hclk2_clk		= 88,
	hclk3_clk		= 89,
	hclk4_clk		= 90,
	jpg_div			= 91,
	jpg_eclk		= 92,
	jpg_hclk		= 93,
	cap_div			= 94,
	cap_eclk		= 95,
	cap_hclk		= 96,
	edma0_hclk		= 97,
	edma1_hclk		= 98,
	edma2_hclk		= 99,
	edma3_hclk		= 100,
	edma4_hclk		= 101,
	fsc_hclk		= 102,
	dram_clk		= 103,
	sram_clk		= 104,
	ddr_clk			= 105,
	blt_hclk		= 106,
	sic_hclk		= 107,
	nand_hclk		= 108,
	spu_hclk		= 109,
	i2s_hclk		= 110,
	spu1_clk		= 111,
	pclk_div		= 112,
	pclk_clk		= 113,
	adc_pclk		= 114,
	i2c_pclk		= 115,
	rtc_pclk		= 116,
	uart0_pclk		= 117,
	uart1_pclk		= 118,
	pwm_pclk		= 119,
	spims0_pclk		= 120,
	spims1_pclk		= 121,
	timer0_pclk		= 122,
	timer1_pclk		= 123,
	wdt_pclk		= 124,
	tic_pclk		= 125,
	kpi_pclk		= 126,
	clk_max
};

static struct clk *clks[clk_max];
static struct clk_onecell_data clk_data;

/* clocks needed for basic system operation */
static enum n329_clk clks_init_on[] __initdata = {
	xtal_clk, rtx_clk, apll_clk, upll_clk, reserved_clk,
	adiv0_clk, adiv1_clk, adiv2_clk, adiv3_clk, 
	adiv4_clk, adiv5_clk, adiv6_clk, adiv7_clk,
	udiv0_clk, udiv1_clk, udiv2_clk, udiv3_clk, 
	udiv4_clk, udiv5_clk, udiv6_clk, udiv7_clk,
	uart1_aclk, uart1_uclk, uart1_src, uart1_div, uart1_clk,
	sys_aclk, sys_uclk, sys_src, sys_clk,
	gpio_src, gpio_div, gpio_clk,
	cpu_div, cpu_clk,
	hclk_div, hclk1_div, hclk234_div, hclk_clk,
	hclk1_clk, hclk2_clk, hclk3_clk, hclk4_clk,
	dram_clk, sram_clk, ddr_clk,
	pclk_div, pclk_clk, uart1_pclk
};

#define ABS_DELTA(a,b)  ((a) > (b) ? (a) - (b) : (b) - (a))

unsigned long n329_clocks_config_usb(unsigned long rate)
{
	unsigned long apll_rate, upll_rate, xin_rate, pll_div, clk_div;
	unsigned long best_rate, best_pll_div, best_clk_div, best_src;
	unsigned long test_rate, test_pll_div, test_clk_div;

	apll_rate = clk_get_rate(clks[apll_clk]);
	upll_rate = clk_get_rate(clks[upll_clk]);
	xin_rate = clk_get_rate(clks[xtal_clk]);
	pll_div = (1 << 3);
	clk_div = (1 << 4);

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
			test_rate = (upll_rate / (test_pll_div + 1)) / (test_clk_div + 1);
			if ((ABS_DELTA(rate, test_rate) < ABS_DELTA(rate, best_rate))) {
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
			test_rate = (apll_rate / (test_pll_div + 1)) / (test_clk_div + 1);
			if ((ABS_DELTA(rate, test_rate) < ABS_DELTA(rate, best_rate))) {
				best_rate = test_rate;
				best_src = 2;
				best_pll_div = test_pll_div;
				best_clk_div = test_clk_div;
			}
		}
	}

found:

	if (best_src == 3) {
		clk_set_parent(clks[usb_uclk], clks[udiv0_clk + best_pll_div]);
		clk_set_parent(clks[usb_src], clks[usb_uclk]);
	} else if (best_src == 2) {
		clk_set_parent(clks[usb_aclk], clks[adiv0_clk + best_pll_div]);
		clk_set_parent(clks[usb_src], clks[usb_aclk]);
	} else {
		clk_set_parent(clks[usb_src], clks[xtal_clk]);
	}
	clk_set_rate(clks[usb_div], best_rate);

	return clk_get_rate(clks[usb_div]);
}
EXPORT_SYMBOL(n329_clocks_config_usb);

unsigned long n329_clocks_config_usb20(unsigned long rate)
{
	unsigned long apll_rate, upll_rate, xin_rate, pll_div, clk_div;
	unsigned long best_rate, best_pll_div, best_clk_div, best_src;
	unsigned long test_rate, test_pll_div, test_clk_div;

	apll_rate = clk_get_rate(clks[apll_clk]);
	upll_rate = clk_get_rate(clks[upll_clk]);
	xin_rate = clk_get_rate(clks[xtal_clk]);
	pll_div = (1 << 3);
	clk_div = (1 << 4);

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
			test_rate = (upll_rate / (test_pll_div + 1)) / (test_clk_div + 1);
			if ((ABS_DELTA(rate, test_rate) < ABS_DELTA(rate, best_rate))) {
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
			test_rate = (apll_rate / (test_pll_div + 1)) / (test_clk_div + 1);
			if ((ABS_DELTA(rate, test_rate) < ABS_DELTA(rate, best_rate))) {
				best_rate = test_rate;
				best_src = 2;
				best_pll_div = test_pll_div;
				best_clk_div = test_clk_div;
			}
		}
	}

found:

	if (best_src == 3) {
		clk_set_parent(clks[usb20_uclk], clks[udiv0_clk + best_pll_div]);
		clk_set_parent(clks[usb20_src], clks[usb20_uclk]);
	} else if (best_src == 2) {
		clk_set_parent(clks[usb20_aclk], clks[adiv0_clk + best_pll_div]);
		clk_set_parent(clks[usb20_src], clks[usb20_aclk]);
	} else {
		clk_set_parent(clks[usb20_src], clks[xtal_clk]);
	}
	clk_set_rate(clks[usb20_div], best_rate);

	return clk_get_rate(clks[usb20_div]);
}
EXPORT_SYMBOL(n329_clocks_config_usb20);

unsigned long n329_clocks_config_sd(unsigned long rate)
{
	unsigned long apll_rate, upll_rate, xin_rate, pll_div, clk_div;
	unsigned long best_rate, best_pll_div, best_clk_div, best_src;
	unsigned long test_rate, test_pll_div, test_clk_div;

	apll_rate = clk_get_rate(clks[apll_clk]);
	upll_rate = clk_get_rate(clks[upll_clk]);
	xin_rate = clk_get_rate(clks[xtal_clk]);
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
			test_rate = (upll_rate / (test_pll_div + 1)) / (test_clk_div + 1);
			if ((ABS_DELTA(rate, test_rate) < ABS_DELTA(rate, best_rate))) {
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
			test_rate = (apll_rate / (test_pll_div + 1)) / (test_clk_div + 1);
			if ((ABS_DELTA(rate, test_rate) < ABS_DELTA(rate, best_rate))) {
				best_rate = test_rate;
				best_src = 2;
				best_pll_div = test_pll_div;
				best_clk_div = test_clk_div;
			}
		}
	}

found:

	if (best_src == 3) {
		clk_set_parent(clks[sd_uclk], clks[udiv0_clk + best_pll_div]);
		clk_set_parent(clks[sd_src], clks[sd_uclk]);
	} else if (best_src == 2) {
		clk_set_parent(clks[sd_aclk], clks[adiv0_clk + best_pll_div]);
		clk_set_parent(clks[sd_src], clks[sd_aclk]);
	} else {
		clk_set_parent(clks[sd_src], clks[xtal_clk]);
	}
	clk_set_rate(clks[sd_div], best_rate);

	return clk_get_rate(clks[sd_div]);
}
EXPORT_SYMBOL(n329_clocks_config_sd);

static void __init n329_clocks_init(struct device_node *np)
{
	int xtal;
	unsigned int i;
	struct device_node *gcr;

	clkctrl = of_iomap(np, 0);
	WARN_ON(!clkctrl);

	/* locate the system management control registers */
	gcr = of_find_compatible_node(NULL, NULL, "nuvoton,gcr");
	gcrctrl = of_iomap(gcr, 0);
	WARN_ON(!gcrctrl);
	of_node_put(gcr);

	/* determine frequency of external crystal clock */
	if ((__raw_readl(DIGCTRL + HW_GCR_CHIPCFG) & 0xC) == 0x8)
		xtal = 12000000;
	else
		xtal = 27000000;

	/* system crystal, rtx, APLL and UPLL clocks */
	clks[xtal_clk] = n329_clk_fixed("xtal_clk", xtal);
	clks[rtx_clk] = n329_clk_fixed("rtx_clk", 32768);
	clks[apll_clk] = n329_clk_pll("apll_clk", "xtal_clk", REG_APLLCON);
	clks[upll_clk] = n329_clk_pll("upll_clk", "xtal_clk", REG_UPLLCON);
	clks[reserved_clk] = n329_clk_fixed("reserved_clk", 0);

	/* APLL 1 to 8 divider clocks */
	clks[adiv0_clk] = n329_clk_fixed_div("adiv0_clk", "apll_clk", 1);
	clks[adiv1_clk] = n329_clk_fixed_div("adiv1_clk", "apll_clk", 2);
	clks[adiv2_clk] = n329_clk_fixed_div("adiv2_clk", "apll_clk", 3);
	clks[adiv3_clk] = n329_clk_fixed_div("adiv3_clk", "apll_clk", 4);
	clks[adiv4_clk] = n329_clk_fixed_div("adiv4_clk", "apll_clk", 5);
	clks[adiv5_clk] = n329_clk_fixed_div("adiv5_clk", "apll_clk", 6);
	clks[adiv6_clk] = n329_clk_fixed_div("adiv6_clk", "apll_clk", 7);
	clks[adiv7_clk] = n329_clk_fixed_div("adiv7_clk", "apll_clk", 8);

	/* UPLL 1 to 8 divider clocks */
	clks[udiv0_clk] = n329_clk_fixed_div("udiv0_clk", "upll_clk", 1);
	clks[udiv1_clk] = n329_clk_fixed_div("udiv1_clk", "upll_clk", 2);
	clks[udiv2_clk] = n329_clk_fixed_div("udiv2_clk", "upll_clk", 3);
	clks[udiv3_clk] = n329_clk_fixed_div("udiv3_clk", "upll_clk", 4);
	clks[udiv4_clk] = n329_clk_fixed_div("udiv4_clk", "upll_clk", 5);
	clks[udiv5_clk] = n329_clk_fixed_div("udiv5_clk", "upll_clk", 6);
	clks[udiv6_clk] = n329_clk_fixed_div("udiv6_clk", "upll_clk", 7);
	clks[udiv7_clk] = n329_clk_fixed_div("udiv7_clk", "upll_clk", 8);

	/* ADC engine clock generator */
	clks[adc_aclk] = n329_clk_mux("adc_aclk", REG_CLKDIV3, 16, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[adc_uclk] = n329_clk_mux("adc_uclk", REG_CLKDIV3, 16, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[adc_src] = n329_clk_mux("adc_src", REG_CLKDIV3, 19, 2, sel_adc_src, ARRAY_SIZE(sel_adc_src));
	clks[adc_div] = n329_clk_source_div("adc_div", "adc_src", REG_CLKDIV3, 24, 8);
	clks[adc_clk] = n329_clk_gate("adc_clk", "adc_div", REG_APBCLK, 0);

	/* ADO (Audio) engine clock generator */
	clks[ado_aclk] = n329_clk_mux("ado_aclk", REG_CLKDIV1, 16, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[ado_uclk] = n329_clk_mux("ado_uclk", REG_CLKDIV1, 16, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[ado_src] = n329_clk_mux("ado_src", REG_CLKDIV1, 19, 2, sel_ado_src, ARRAY_SIZE(sel_ado_src));
	clks[ado_div] = n329_clk_div("ado_div", "ado_src", REG_CLKDIV1, 24, 8);
	clks[ado_clk] = n329_clk_gate("ado_clk", "ado_div", REG_AHBCLK, 30);

	/* LCD VPOST engine clock generator */
	clks[vpost_aclk] = n329_clk_mux("vpost_aclk", REG_CLKDIV1, 0, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[vpost_uclk] = n329_clk_mux("vpost_uclk", REG_CLKDIV1, 0, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[vpost_src] = n329_clk_mux("vpost_src", REG_CLKDIV1, 3, 2, sel_vpost_src, ARRAY_SIZE(sel_vpost_src));
	clks[vpost_div] = n329_clk_source_div("vpost_div", "vpost_src", REG_CLKDIV1, 8, 8);
	clks[vpost_clk] = n329_clk_gate("vpost_clk", "vpost_div", REG_AHBCLK, 27);
	clks[vpostd2_div] = n329_clk_fixed_div("vpostd2_div", "vpost_div", 2);
	clks[vpostd2_clk] = n329_clk_gate("vpostd2_clk", "vpostd2_div", REG_AHBCLK, 27);
	clks[vpost_hclk] = n329_clk_gate("vpost_hclk", "hclk4_clk", REG_AHBCLK, 27);

	/* SD engine clock generator */
	clks[sd_aclk] = n329_clk_mux("sd_aclk", REG_CLKDIV2, 16, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[sd_uclk] = n329_clk_mux("sd_uclk", REG_CLKDIV2, 16, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[sd_src] = n329_clk_mux("sd_src", REG_CLKDIV2, 19, 2, sel_sd_src, ARRAY_SIZE(sel_sd_src));
	clks[sd_div] = n329_clk_source_div("sd_div", "sd_src", REG_CLKDIV2, 24, 8);
	clks[sd_clk] = n329_clk_gate("sd_clk", "sd_div", REG_AHBCLK, 23);

	/* Sensor clock generator */
	clks[sen_aclk] = n329_clk_mux("sen_aclk", REG_CLKDIV0, 16, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[sen_uclk] = n329_clk_mux("sen_uclk", REG_CLKDIV0, 16, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[sen_src] = n329_clk_mux("sen_src", REG_CLKDIV0, 19, 2, sel_sen_src, ARRAY_SIZE(sel_sen_src));
	clks[sen_div] = n329_clk_source_div("sen_div", "sen_src", REG_CLKDIV0, 24, 4);
	clks[sen_clk] = n329_clk_gate("sen_clk", "sen_div", REG_AHBCLK, 29);

	/* USB 1.1 48MHz clocks generator */
	clks[usb_aclk] = n329_clk_mux("usb_aclk", REG_CLKDIV2, 0, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[usb_uclk] = n329_clk_mux("usb_uclk", REG_CLKDIV2, 0, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[usb_src] = n329_clk_mux("usb_src", REG_CLKDIV2, 3, 2, sel_usb_src, ARRAY_SIZE(sel_usb_src));
	clks[usb_div] = n329_clk_source_div("usb_div", "usb_src", REG_CLKDIV2, 8, 4);
	clks[usb_clk] = n329_clk_gate("usb_clk", "usb_div", REG_AHBCLK, 17);
	clks[usbh_hclk] = n329_clk_gate("usbh_hclk", "hclk3_clk", REG_AHBCLK, 17);

	/* USB 2.0 PHY 12 MHz source clock generator */
	clks[usb20_aclk] = n329_clk_mux("usb20_aclk", REG_CLKDIV2, 5, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[usb20_uclk] = n329_clk_mux("usb20_uclk", REG_CLKDIV2, 5, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[usb20_src] = n329_clk_mux("usb20_src", REG_CLKDIV2, 21, 2, sel_usb20_src, ARRAY_SIZE(sel_usb20_src));
	clks[usb20_div] = n329_clk_source_div("usb20_div", "usb20_src", REG_CLKDIV2, 12, 4);
	clks[usb20_clk] = n329_clk_gate("usb20_clk", "usb20_div", REG_AHBCLK, 18);
	clks[usb20_hclk] = n329_clk_gate("usb20_hclk", "hclk3_clk", REG_AHBCLK, 18);

	/* UART 0 clock generator */
	clks[uart0_aclk] = n329_clk_mux("uart0_aclk", REG_CLKDIV3, 0, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[uart0_uclk] = n329_clk_mux("uart0_uclk", REG_CLKDIV3, 0, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[uart0_src] = n329_clk_mux("uart0_src", REG_CLKDIV3, 3, 2, sel_uart0_src, ARRAY_SIZE(sel_uart0_src));
	clks[uart0_div] = n329_clk_source_div("uart0_div", "uart0_src", REG_CLKDIV3, 5, 3);
	clks[uart0_clk] = n329_clk_gate("uart0_clk", "uart0_div", REG_APBCLK, 3);

	/* UART 1 clock generator */
	clks[uart1_aclk] = n329_clk_mux("uart1_aclk", REG_CLKDIV3, 8, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[uart1_uclk] = n329_clk_mux("uart1_uclk", REG_CLKDIV3, 8, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[uart1_src] = n329_clk_mux("uart1_src", REG_CLKDIV3, 11, 2, sel_uart1_src, ARRAY_SIZE(sel_uart1_src));
	clks[uart1_div] = n329_clk_source_div("uart1_div", "uart1_src", REG_CLKDIV3, 13, 3);
	clks[uart1_clk] = n329_clk_gate("uart1_clk", "uart1_div", REG_APBCLK, 4);

	/* System clock generator. */
	clks[sys_aclk] = n329_clk_mux("sys_aclk", REG_CLKDIV0, 0, 3, sel_apll, ARRAY_SIZE(sel_apll));
	clks[sys_uclk] = n329_clk_mux("sys_uclk", REG_CLKDIV0, 0, 3, sel_upll, ARRAY_SIZE(sel_upll));
	clks[sys_src] = n329_clk_mux("sys_src", REG_CLKDIV0, 3, 2, sel_sys_src, ARRAY_SIZE(sel_sys_src));
	clks[sys_clk] = n329_clk_source_div("sys_clk", "sys_src", REG_CLKDIV0, 8, 4);

	/* GPIO clock generator */
	clks[gpio_src] = n329_clk_mux("gpio_src", REG_CLKDIV4, 16, 1, sel_gpio_src, ARRAY_SIZE(sel_gpio_src));
	clks[gpio_div] = n329_clk_div("gpio_div", "gpio_src", REG_CLKDIV4, 17, 7);
	clks[gpio_clk] = n329_clk_gate("gpio_clk", "gpio_div", REG_AHBCLK, 1);

	/* KPI clock generator */
	clks[kpi_src] = n329_clk_mux("kpi_src", REG_CLKDIV0, 5, 1, sel_kpi_src, ARRAY_SIZE(sel_kpi_src));
	clks[kpi_div] = n329_clk_split_div("kpi_div", "kpi_src", REG_CLKDIV0, 12, 4, 21, 3);
	clks[kpi_clk] = n329_clk_gate("kpi_clk", "kpi_div", REG_APBCLK, 25);

	/* CPU dividers and clocks. */
	clks[cpu_div] = n329_clk_div("cpu_div", "sys_clk", REG_CLKDIV4, 0, 4);
	clks[cpu_clk] = n329_clk_gate("cpu_clk", "cpu_div", REG_AHBCLK, 0);

	/* HCLK dividers and clocks */
	clks[hclk_div] = n329_clk_fixed_div("hclk_div", "sys_clk", 2);
	clks[hclk1_div] = n329_clk_table_div("hclk1_div", "cpu_div", REG_CLKDIV4, 0, 1, hclk1_div_table);
	clks[hclk234_div] = n329_clk_div("hclk234_div", "hclk_div", REG_CLKDIV4, 4, 4);
	clks[hclk_clk] = n329_clk_gate("hclk_clk", "hclk_div", REG_AHBCLK, 2);
	clks[hclk1_clk] = n329_clk_gate("hclk1_clk", "hclk1_div", REG_AHBCLK, 8);
	clks[hclk2_clk] = n329_clk_and_gate("hclk2_clk", "hclk234_div", REG_AHBCLK, 16, 24);
	clks[hclk3_clk] = n329_clk_gate("hclk3_clk", "hclk234_div", REG_AHBCLK, 16);
	clks[hclk4_clk] = n329_clk_gate("hclk4_clk", "hclk234_div", REG_AHBCLK, 24);

	/* JPG dividers and clocks */
	clks[jpg_div] = n329_clk_div("jpg_div", "hclk3_clk", REG_CLKDIV4, 24, 3);
	clks[jpg_eclk] = n329_clk_gate("jpg_eclk", "jpg_div", REG_APBCLK, 7);
	clks[jpg_hclk] = n329_clk_gate("jpg_hclk", "hclk3_clk", REG_APBCLK, 7);

	/* capture engine dividers and clocks */
	clks[cap_div] = n329_clk_div("cap_div", "hclk4_clk", REG_CLKDIV4, 12, 3);
	clks[cap_eclk] = n329_clk_gate("cap_eclk", "cap_div", REG_APBCLK, 28);
	clks[cap_hclk] = n329_clk_gate("cap_hclk", "hclk4_clk", REG_AHBCLK, 28);

	/* EDMA controller clocks */
	clks[edma0_hclk] = n329_clk_gate("edma0_hclk", "hclk1_div", REG_AHBCLK, 10);
	clks[edma1_hclk] = n329_clk_gate("edma1_hclk", "hclk1_div", REG_AHBCLK, 11);
	clks[edma2_hclk] = n329_clk_gate("edma2_hclk", "hclk1_div", REG_AHBCLK, 12);
	clks[edma3_hclk] = n329_clk_gate("edma3_hclk", "hclk1_div", REG_AHBCLK, 13);
	clks[edma4_hclk] = n329_clk_gate("edma4_hclk", "hclk1_div", REG_AHBCLK, 14);

	/* frame switch controller clock*/
	clks[fsc_hclk] = n329_clk_gate("fsc_hclk", "hclk2_clk", REG_AHBCLK, 6);

	/* memory controller clocks */
	clks[dram_clk] = n329_clk_gate("dram_clk", "hclk_div", REG_AHBCLK, 2);
	clks[sram_clk] = n329_clk_gate("sram_clk", "hclk_clk", REG_AHBCLK, 3);
	clks[ddr_clk] = n329_clk_gate("ddr_clk", "sys_clk", REG_AHBCLK, 4);

	/* other HCLK3 derived clocks */
	clks[blt_hclk] = n329_clk_gate("blt_hclk", "hclk3_clk", REG_AHBCLK, 5);
	clks[sic_hclk] = n329_clk_gate("sic_hclk", "hclk3_clk", REG_AHBCLK, 21);
	clks[nand_hclk] = n329_clk_gate("nand_hclk", "hclk3_clk", REG_AHBCLK, 22);

	/* other HCLK4 derived clocks */
	clks[spu_hclk] = n329_clk_gate("spu_hclk", "hclk4_clk", REG_AHBCLK, 25);
	clks[i2s_hclk] = n329_clk_gate("i2s_hclk", "hclk4_clk", REG_AHBCLK, 26);
	clks[spu1_clk] = n329_clk_gate("spu1_clk", "hclk4_clk", REG_AHBCLK, 31);

	/* APB clocks */
	clks[pclk_div] = n329_clk_div("pclk_div", "hclk1_clk", REG_CLKDIV4, 8, 4);
	clks[pclk_clk] = n329_clk_gate("pclk_clk", "pclk_div", REG_AHBCLK, 1);
	clks[adc_pclk] = n329_clk_gate("adc_pclk", "pclk_clk", REG_APBCLK, 0);
	clks[i2c_pclk] = n329_clk_gate("i2c_pclk", "pclk_clk", REG_APBCLK, 1);
	clks[rtc_pclk] = n329_clk_gate("rtc_pclk", "pclk_clk", REG_APBCLK, 2);
	clks[uart0_pclk] = n329_clk_gate("uart0_pclk", "pclk_clk", REG_APBCLK, 3);
	clks[uart1_pclk] = n329_clk_gate("uart1_pclk", "pclk_clk", REG_APBCLK, 4);
	clks[pwm_pclk] = n329_clk_gate("pwm_pclk", "pclk_clk", REG_APBCLK, 5);
	clks[spims0_pclk] = n329_clk_gate("spims0_pclk", "pclk_clk", REG_APBCLK, 6);
	clks[spims1_pclk] = n329_clk_gate("spims1_pclk", "pclk_clk", REG_APBCLK, 7);
	clks[timer0_pclk] = n329_clk_gate("timer0_pclk", "pclk_clk", REG_APBCLK, 8);
	clks[timer1_pclk] = n329_clk_gate("timer1_pclk", "pclk_clk", REG_APBCLK, 9);
	clks[wdt_pclk] = n329_clk_gate("wdt_pclk", "pclk_clk", REG_APBCLK, 15);
	clks[tic_pclk] = n329_clk_gate("tic_pclk", "pclk_clk", REG_APBCLK, 24);
	clks[kpi_pclk] = n329_clk_gate("kpi_pclk", "pclk_clk", REG_APBCLK, 25);

	/* Check for errors */
	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		if (IS_ERR(clks[i])) {
			pr_err("N329 clk %d: register failed with %ld\n",
				i, PTR_ERR(clks[i]));
			return;
		}
	}

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	/* Enable certain clocks */
	for (i = 0; i < ARRAY_SIZE(clks_init_on); i++) {
		clk_prepare_enable(clks[clks_init_on[i]]);
	}

	pr_info("XTL clock = %lu\n", clk_get_rate(clks[xtal_clk]));
	pr_info("RTX clock = %lu\n", clk_get_rate(clks[rtx_clk]));
	pr_info("SYS clock = %lu\n", clk_get_rate(clks[sys_clk]));
	pr_info("CPU clock = %lu\n", clk_get_rate(clks[cpu_clk]));
	pr_info("AHP clock = %lu\n", clk_get_rate(clks[hclk_clk]));
	pr_info("APB clock = %lu\n", clk_get_rate(clks[pclk_clk]));
}

CLK_OF_DECLARE(n329_clk, "nuvoton,clk", n329_clocks_init);
