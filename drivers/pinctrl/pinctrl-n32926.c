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

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/n329-gcr.h>
#include "core.h"

static int n32926_pinctrl_probe(struct platform_device *pdev)
{
	return -EINVAL;
}

static int n32926_pinctrl_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id n32926_pinctrl_of_match[] = {
	{ .compatible = "nuvoton,n32926-pinctrl", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n32926_pinctrl_of_match);

static struct platform_driver n32926_pinctrl_driver = {
	.driver = {
		.name = "n32926-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = n32926_pinctrl_of_match,
	},
	.probe = n32926_pinctrl_probe,
	.remove = n32926_pinctrl_remove,
};

static int __init n32926_pinctrl_init(void)
{
	return platform_driver_register(&n32926_pinctrl_driver);
}
postcore_initcall(n32926_pinctrl_init);

static void __exit n32926_pinctrl_exit(void)
{
	platform_driver_unregister(&n32926_pinctrl_driver);
}
module_exit(n32926_pinctrl_exit);

