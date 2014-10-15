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

#ifndef __PINCTRL_N329_H
#define __PINCTRL_N329_H

#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#define N329_BANKS				5

#define N329_PINCTRL_PIN(pin)	PINCTRL_PIN(pin, #pin)
#define PINID(bank, pin)		((bank) * 16 + (pin))

/*
 * pinmux-id bit field definitions
 *
 * bank:	15..12	(4)
 * pin:		11..4	(8)
 * muxsel:	3..0	(4)
 */
#define MUXID_TO_PINID(m)	PINID((m) >> 12 & 0xf, (m) >> 4 & 0xff)
#define MUXID_TO_MUXSEL(m)	((m) & 0xf)

#define PINID_TO_BANK(p)	((p) >> 4)
#define PINID_TO_PIN(p)		((p) % 16)

/*
 * pin config bit field definitions
 *
 * pull-up:	2..0	(2)
 *
 * MSB of each field is presence bit for the config.
 */
#define PULL_PRESENT	(1 << 1)
#define PULL_SHIFT		0
#define CONFIG_TO_PULL(c)	((c) >> PULL_SHIFT & 0x1)

struct n329_function {
	const char *name;
	const char **groups;
	unsigned ngroups;
};

struct n329_group {
	const char *name;
	unsigned int *pins;
	unsigned npins;
	u8 *muxsel;
	u8 config;
};

struct n329_pinctrl_soc_data {
	const struct pinctrl_pin_desc *pins;
	unsigned npins;
	struct n329_function *functions;
	unsigned nfunctions;
	struct n329_group *groups;
	unsigned ngroups;
};

int n329_pinctrl_probe(struct platform_device *pdev,
		      struct n329_pinctrl_soc_data *soc);
int n329_pinctrl_remove(struct platform_device *pdev);

#endif /* __PINCTRL_N329_H */
