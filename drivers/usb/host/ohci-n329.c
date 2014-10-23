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
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb/otg.h>
#include <linux/platform_device.h>
#include <linux/signal.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#include <asm/io.h>
#include <asm/mach-types.h>

#define DRIVER_DESC "Nuvoton N329XX USB OHCI host driver"

static const char hcd_name[] = "ohci-n329";
static struct hc_driver __read_mostly ohci_n329_hc_driver;

static int n329_ohci_reset(struct usb_hcd *hcd)
{
	return 0;
}

static int n329_ohci_drv_probe(struct platform_device *pdev)
{
	/* TBD */
	dev_info(&pdev->dev, "Probing " DRIVER_DESC "\n");
	return 0;
}

static int n329_ohci_drv_remove(struct platform_device *pdev)
{
	// XXX struct usb_hcd *hcd = platform_get_drvdata(pdev);

	/* TBD */
	dev_info(&pdev->dev, "Removing " DRIVER_DESC "\n");

	return 0;
}

#ifdef	CONFIG_PM

static int n329_ohci_suspend(struct platform_device *pdev, pm_message_t message)
{
	/* TBD */
	return 0;
}

static int n329_ohci_resume(struct platform_device *dev)
{
	/* TBD */
	return 0;
}

#endif

/* Driver definition to register */
static struct platform_driver n329_ohci_driver = {
	.probe		= n329_ohci_drv_probe,
	.remove		= n329_ohci_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM
	.suspend	= n329_ohci_suspend,
	.resume		= n329_ohci_resume,
#endif
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "ohci",
	},
};

static const struct ohci_driver_overrides n329_overrides __initconst = {
	.product_desc	= "N329 OHCI",
	.reset		= n329_ohci_reset
};

static int __init n329_ohci_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ohci_init_driver(&ohci_n329_hc_driver, &n329_overrides);
	return platform_driver_register(&n329_ohci_driver);
	return 0;
}

static void __exit n329_ohci_exit(void)
{
	platform_driver_unregister(&n329_ohci_driver);
}

module_init(n329_ohci_init);
module_exit(n329_ohci_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:n329-uhc");
