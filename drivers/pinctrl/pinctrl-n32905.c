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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include "pinctrl-n329.h"

enum n32905_pin_enum {
	MF_GPA00	= PINID(0, 0),
	MF_GPA01	= PINID(0, 1),
	MF_GPA02	= PINID(0, 2),
	MF_GPA03	= PINID(0, 3),
	MF_GPA04	= PINID(0, 4),
	MF_GPA05	= PINID(0, 5),
	MF_GPA06	= PINID(0, 6),
	MF_GPA07	= PINID(0, 7),
	MF_GPA08	= PINID(0, 8),
	MF_GPA09	= PINID(0, 9),
	MF_GPA10	= PINID(0, 10),
	MF_GPA11	= PINID(0, 11),

	MF_GPB00	= PINID(1, 0),
	MF_GPB01	= PINID(1, 1),
	MF_GPB02	= PINID(1, 2),
	MF_GPB03	= PINID(1, 3),
	MF_GPB04	= PINID(1, 4),
	MF_GPB05	= PINID(1, 5),
	MF_GPB06	= PINID(1, 6),
	MF_GPB07	= PINID(1, 7),
	MF_GPB08	= PINID(1, 8),
	MF_GPB09	= PINID(1, 9),
	MF_GPB10	= PINID(1, 10),
	MF_GPB11	= PINID(1, 11),
	MF_GPB12	= PINID(1, 12),
	MF_GPB13	= PINID(1, 13),
	MF_GPB14	= PINID(1, 14),
	MF_GPB15	= PINID(1, 15),

	MF_GPC00	= PINID(2, 0),
	MF_GPC01	= PINID(2, 1),
	MF_GPC02	= PINID(2, 2),
	MF_GPC03	= PINID(2, 3),
	MF_GPC04	= PINID(2, 4),
	MF_GPC05	= PINID(2, 5),
	MF_GPC06	= PINID(2, 6),
	MF_GPC07	= PINID(2, 7),
	MF_GPC08	= PINID(2, 8),
	MF_GPC09	= PINID(2, 9),
	MF_GPC10	= PINID(2, 10),
	MF_GPC11	= PINID(2, 11),
	MF_GPC12	= PINID(2, 12),
	MF_GPC13	= PINID(2, 13),
	MF_GPC14	= PINID(2, 14),
	MF_GPC15	= PINID(2, 15),

	MF_GPD00	= PINID(3, 0),
	MF_GPD01	= PINID(3, 1),
	MF_GPD02	= PINID(3, 2),
	MF_GPD03	= PINID(3, 3),
	MF_GPD04	= PINID(3, 4),
	MF_GPD05	= PINID(3, 5),
	MF_GPD06	= PINID(3, 6),
	MF_GPD07	= PINID(3, 7),
	MF_GPD08	= PINID(3, 8),
	MF_GPD09	= PINID(3, 9),
	MF_GPD10	= PINID(3, 10),
	MF_GPD11	= PINID(3, 11),
	MF_GPD12	= PINID(3, 12),
	MF_GPD13	= PINID(3, 13),
	MF_GPD14	= PINID(3, 14),
	MF_GPD15	= PINID(3, 15),

	MF_GPE00	= PINID(4, 0),
	MF_GPE01	= PINID(4, 1),
	MF_GPE02	= PINID(4, 2),
	MF_GPE03	= PINID(4, 3),
	MF_GPE04	= PINID(4, 4),
	MF_GPE05	= PINID(4, 5),
	MF_GPE06	= PINID(4, 6),
	MF_GPE07	= PINID(4, 7),
	MF_GPE08	= PINID(4, 8),
	MF_GPE09	= PINID(4, 9),
	MF_GPE10	= PINID(4, 10),
	MF_GPE11	= PINID(4, 11),
};

static const struct pinctrl_pin_desc n32905_pins[] = {
	N329_PINCTRL_PIN(MF_GPA00),
	N329_PINCTRL_PIN(MF_GPA01),
	N329_PINCTRL_PIN(MF_GPA02),
	N329_PINCTRL_PIN(MF_GPA03),
	N329_PINCTRL_PIN(MF_GPA04),
	N329_PINCTRL_PIN(MF_GPA05),
	N329_PINCTRL_PIN(MF_GPA06),
	N329_PINCTRL_PIN(MF_GPA07),
	N329_PINCTRL_PIN(MF_GPA08),
	N329_PINCTRL_PIN(MF_GPA09),
	N329_PINCTRL_PIN(MF_GPA10),
	N329_PINCTRL_PIN(MF_GPA11),

	N329_PINCTRL_PIN(MF_GPB00),
	N329_PINCTRL_PIN(MF_GPB01),
	N329_PINCTRL_PIN(MF_GPB02),
	N329_PINCTRL_PIN(MF_GPB03),
	N329_PINCTRL_PIN(MF_GPB04),
	N329_PINCTRL_PIN(MF_GPB05),
	N329_PINCTRL_PIN(MF_GPB06),
	N329_PINCTRL_PIN(MF_GPB07),
	N329_PINCTRL_PIN(MF_GPB08),
	N329_PINCTRL_PIN(MF_GPB09),
	N329_PINCTRL_PIN(MF_GPB10),
	N329_PINCTRL_PIN(MF_GPB11),
	N329_PINCTRL_PIN(MF_GPB12),
	N329_PINCTRL_PIN(MF_GPB13),
	N329_PINCTRL_PIN(MF_GPB14),
	N329_PINCTRL_PIN(MF_GPB15),

	N329_PINCTRL_PIN(MF_GPC00),
	N329_PINCTRL_PIN(MF_GPC01),
	N329_PINCTRL_PIN(MF_GPC02),
	N329_PINCTRL_PIN(MF_GPC03),
	N329_PINCTRL_PIN(MF_GPC04),
	N329_PINCTRL_PIN(MF_GPC05),
	N329_PINCTRL_PIN(MF_GPC06),
	N329_PINCTRL_PIN(MF_GPC07),
	N329_PINCTRL_PIN(MF_GPC08),
	N329_PINCTRL_PIN(MF_GPC09),
	N329_PINCTRL_PIN(MF_GPC10),
	N329_PINCTRL_PIN(MF_GPC11),
	N329_PINCTRL_PIN(MF_GPC12),
	N329_PINCTRL_PIN(MF_GPC13),
	N329_PINCTRL_PIN(MF_GPC14),
	N329_PINCTRL_PIN(MF_GPC15),

	N329_PINCTRL_PIN(MF_GPD00),
	N329_PINCTRL_PIN(MF_GPD01),
	N329_PINCTRL_PIN(MF_GPD02),
	N329_PINCTRL_PIN(MF_GPD03),
	N329_PINCTRL_PIN(MF_GPD04),
	N329_PINCTRL_PIN(MF_GPD05),
	N329_PINCTRL_PIN(MF_GPD06),
	N329_PINCTRL_PIN(MF_GPD07),
	N329_PINCTRL_PIN(MF_GPD08),
	N329_PINCTRL_PIN(MF_GPD09),
	N329_PINCTRL_PIN(MF_GPD10),
	N329_PINCTRL_PIN(MF_GPD11),
	N329_PINCTRL_PIN(MF_GPD12),
	N329_PINCTRL_PIN(MF_GPD13),
	N329_PINCTRL_PIN(MF_GPD14),
	N329_PINCTRL_PIN(MF_GPD15),

	N329_PINCTRL_PIN(MF_GPE00),
	N329_PINCTRL_PIN(MF_GPE01),
	N329_PINCTRL_PIN(MF_GPE02),
	N329_PINCTRL_PIN(MF_GPE03),
	N329_PINCTRL_PIN(MF_GPE04),
	N329_PINCTRL_PIN(MF_GPE05),
	N329_PINCTRL_PIN(MF_GPE06),
	N329_PINCTRL_PIN(MF_GPE07),
	N329_PINCTRL_PIN(MF_GPE08),
	N329_PINCTRL_PIN(MF_GPE09),
	N329_PINCTRL_PIN(MF_GPE10),
	N329_PINCTRL_PIN(MF_GPE11),
};

static struct n329_pinctrl_soc_data n32905_pinctrl_data = {
	.pins = n32905_pins,
	.npins = ARRAY_SIZE(n32905_pins),
};

static int n32905_pinctrl_probe(struct platform_device *pdev)
{
	return n329_pinctrl_probe(pdev, &n32905_pinctrl_data);
}

static int n32905_pinctrl_remove(struct platform_device *pdev)
{
	return n329_pinctrl_remove(pdev);
}

static struct of_device_id n32905_pinctrl_of_match[] = {
	{ .compatible = "nuvoton,n32905-pinctrl", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n32905_pinctrl_of_match);

static struct platform_driver n32905_pinctrl_driver = {
	.driver = {
		.name = "n32905-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = n32905_pinctrl_of_match,
	},
	.probe = n32905_pinctrl_probe,
	.remove = n32905_pinctrl_remove,
};

static int __init n32905_pinctrl_init(void)
{
	return platform_driver_register(&n32905_pinctrl_driver);
}
postcore_initcall(n32905_pinctrl_init);

static void __exit n32905_pinctrl_exit(void)
{
	platform_driver_unregister(&n32905_pinctrl_driver);
}
module_exit(n32905_pinctrl_exit);

MODULE_AUTHOR("Michael P. Thompson <mpthompson@gmail.com>");
MODULE_DESCRIPTION("Nuvoton N32905 pinctrl driver");
MODULE_LICENSE("GPL v2");
