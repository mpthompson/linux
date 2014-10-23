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

#define DRIVER_DESC "Nuvoton N329XX OHCI Host Controller"

static struct clk *usb_clk;
static struct clk *usbh_hclk;

static const char hcd_name[] = "ohci-n329";
static struct hc_driver __read_mostly ohci_n329_hc_driver;

static int n329_ohci_reset(struct usb_hcd *hcd)
{
	return 0;
}

extern unsigned long n329_clocks_config_usb(unsigned long rate);

static int n329_ohci_drv_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *iores;
	struct usb_hcd *hcd;
	struct hc_driver *driver;
	int irq, retval;

	dev_info(&pdev->dev, "Probing " DRIVER_DESC "\n");

	driver = &ohci_n329_hc_driver;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -EINVAL;

	usb_clk = of_clk_get(np, 0);
	if (IS_ERR(usb_clk))
		return PTR_ERR(usb_clk);
	usbh_hclk = of_clk_get(np, 1);
	if (IS_ERR(usbh_hclk)) {
		clk_put(usb_clk);
		return PTR_ERR(usbh_hclk);
	}

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_dbg(&pdev->dev, "usb_create_hcd failed\n");
		retval = -ENOMEM;
		goto err0;
	}
	hcd->rsrc_start = iores->start;
	hcd->rsrc_len = iores->end - iores->start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_dbg(&pdev->dev, "request_mem_region failed\n");
		retval = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "can't ioremap OHCI HCD\n");
		retval = -ENOMEM;
		goto err2;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		retval = -ENXIO;
		goto err3;
	}

	clk_prepare_enable(usb_clk);
	clk_prepare_enable(usbh_hclk);
	n329_clocks_config_usb(48000000);

	if (clk_get_rate(usb_clk) != 48000000) {
		dev_err(&pdev->dev, "failed to set USB host clock to 48MHz\n");
		retval = -ENXIO;
		goto err4;
	}

	retval = usb_add_hcd(hcd, irq, 0);
	if (retval)
		goto err3;

	device_wakeup_enable(hcd->self.controller);
	return 0;

err4:
	clk_disable_unprepare(usbh_hclk);
	clk_disable_unprepare(usb_clk);
err3:
	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	usb_put_hcd(hcd);
err0:
	clk_put(usb_clk);
	clk_put(usbh_hclk);
	return retval;
}

static int n329_ohci_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	dev_dbg(hcd->self.controller, "stopping USB Controller\n");

	usb_remove_hcd(hcd);
	if (!IS_ERR_OR_NULL(hcd->phy)) {
		(void) otg_set_host(hcd->phy->otg, 0);
		usb_put_phy(hcd->phy);
	}
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	clk_disable_unprepare(usbh_hclk);
	clk_disable_unprepare(usb_clk);
	clk_put(usbh_hclk);
	clk_put(usb_clk);

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

static const struct of_device_id ohci_hcd_n329_match[] = {
	{ .compatible = "nuvoton,ohci-n329" },
	{},
};
MODULE_DEVICE_TABLE(of, ohci_hcd_n329_match);

/* Driver definition to register */
static struct platform_driver n329_ohci_driver = {
	.probe		= n329_ohci_drv_probe,
	.remove		= n329_ohci_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM
	.suspend	= n329_ohci_suspend,
	.resume		= n329_ohci_resume,
#endif
	.driver = {
		.name = "usb-ohci",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ohci_hcd_n329_match),
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
}
module_init(n329_ohci_init);

static void __exit n329_ohci_exit(void)
{
	platform_driver_unregister(&n329_ohci_driver);
}
module_exit(n329_ohci_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:n329-uhc");
