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

/* UHC Control Registers */
#define	REG_HC_REVISION			(0x000)		/* Revision Register */
#define	REG_HC_CONTROL			(0x004)		/* Control Register */
#define	REG_HC_CMD_STATUS		(0x008)		/* Command Status Register */
#define	REG_HC_INT_STATUS		(0x00C)		/* Interrupt Status  Register */
#define	REG_HC_INT_ENABLE		(0x010)		/* Interrupt Enable Register */
#define	REG_HC_INT_DISABLE		(0x014)		/* Interrupt Disable Regster */
#define	REG_HC_HCCA				(0x018)		/* Communication Area Register */
#define	REG_HC_PERIOD_CURED		(0x01C)		/* HcPeriodCurrentED */
#define	REG_HC_CTRL_HEADED		(0x020)		/* Control Head ED Register */
#define	REG_HC_CTRL_CURED		(0x024)		/* Control Current ED Regist */
#define	REG_HC_BULK_HEADED		(0x028)		/* Bulk Head ED Register */
#define	REG_HC_BULK_CURED		(0x02C)		/* Bulk Current ED Register */
#define	REG_HC_DONE_HEAD		(0x030)		/* Done Head Register */
#define	REG_HC_FM_INTERVAL		(0x034)		/* Frame Interval Register */
#define	REG_HC_FM_REMAINING		(0x038)		/* Frame Remaining Register */
#define	REG_HC_FM_NUMBER		(0x03C)		/* Frame Number Register */
#define	REG_HC_PERIOD_START		(0x040)		/* Periodic Start Register */
#define	REG_HC_LS_THRESHOLD		(0x044)		/* Low Speed Threshold Register */
#define	REG_HC_RH_DESCRIPTORA	(0x048)		/* Root Hub Descriptor A Register */
#define	REG_HC_RH_DESCRIPTORB	(0x04C)		/* Root Hub Descriptor B Register */
#define	REG_HC_RH_STATUS		(0x050)		/* Root Hub Status Register */
#define	REG_HC_RH_PORT_STATUS1	(0x054)		/* Root Hub Port Status [1] */
#define	REG_HC_RH_PORT_STATUS2	(0x058)		/* Root Hub Port Status [2] */
#define	REG_HC_RH_OP_MODE		(0x204)
	#define	DBR16				BIT(0)		/* Data Buffer Region 16 */
	#define	HCABORT				BIT(1)		/* AHB Bus ERROR Response */
	#define	OCALOW				BIT(3)		/* Over Current Active Low */
	#define	PPCALOW				BIT(4)		/* Port Power Control Active Low */
	#define	SIEPDIS				BIT(8)		/* SIE Pipeline Disable */
	#define	DISPRT1				BIT(16)		/* Disable Port 1 */
	#define	DISPRT2				BIT(17)		/* Disable Port 2 */

#define DRIVER_DESC "Nuvoton N329XX OHCI Host Controller"

static struct clk *usb_clk;
static struct clk *usbh_hclk;

static const char hcd_name[] = "ohci-n329";
static struct hc_driver __read_mostly ohci_n329_hc_driver;

extern unsigned long n329_clocks_config_usb(unsigned long rate);

static int n329_ohci_reset(struct usb_hcd *hcd)
{
	int ret;

	hcd->phy = usb_get_phy(USB_PHY_TYPE_USB2);
	if (hcd->phy == NULL) {
		dev_dbg(hcd->self.controller, "%s: usb_get_phy failed\n", __func__);
		return -ENODEV;
	}

	ret = ohci_setup(hcd);
	if (ret < 0)
		return ret;

	return 0;
}

static int n329_ohci_drv_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *iores;
	struct usb_hcd *hcd;
	struct hc_driver *driver;
	void __iomem *hcd_base;
	int irq, retval;

	dev_info(&pdev->dev, "Probing " DRIVER_DESC "\n");

	driver = &ohci_n329_hc_driver;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores) {
		dev_dbg(&pdev->dev, "%s: platform_get_resource failed\n", __func__);
		return -EINVAL;
	}

	usb_clk = of_clk_get(np, 0);
	if (IS_ERR(usb_clk)) {
		dev_dbg(&pdev->dev, "%s: of_clk_get failed\n", __func__);
		return PTR_ERR(usb_clk);
	}
	usbh_hclk = of_clk_get(np, 1);
	if (IS_ERR(usbh_hclk)) {
		clk_put(usb_clk);
		dev_dbg(&pdev->dev, "%s: of_clk_get failed\n", __func__);
		return PTR_ERR(usbh_hclk);
	}

	clk_prepare_enable(usb_clk);
	clk_prepare_enable(usbh_hclk);
	n329_clocks_config_usb(48000000);

	if (clk_get_rate(usb_clk) != 48000000) {
		dev_err(&pdev->dev, "failed to set USB host clock to 48MHz\n");
		retval = -ENXIO;
		goto err1;
	}

	if (!request_mem_region(iores->start,
					resource_size(iores), pdev->name)) {
		dev_dbg(&pdev->dev, "%s: request_mem_region failed\n", __func__);
		retval = -EBUSY;
		goto err1;
	}

	hcd_base = ioremap(iores->start, resource_size(iores));
	if (hcd_base == NULL) {
		dev_dbg(&pdev->dev, "%s: ioremap failed\n", __func__);
		retval = -ENXIO;
		goto err2;
	}

	/* Enable port 1, disable port 2 */
	writel((readl(hcd_base + REG_HC_RH_OP_MODE) & ~(DISPRT2 | DISPRT1)) |
			DISPRT2, hcd_base + REG_HC_RH_OP_MODE);

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_dbg(&pdev->dev, "usb_create_hcd failed\n");
		retval = -ENOMEM;
		goto err3;
	}
	hcd->rsrc_start = iores->start;
	hcd->rsrc_len = resource_size(iores);
	hcd->regs = hcd_base;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_dbg(&pdev->dev, "%s: platform_get_irq failed\n", __func__);
		retval = -ENXIO;
		goto err4;
	}

	retval = usb_add_hcd(hcd, irq, 0);
	if (retval) {
		dev_dbg(&pdev->dev, "%s: platform_get_irq failed\n", __func__);
		goto err4;
	}

	device_wakeup_enable(hcd->self.controller);
	return 0;

err4:
	usb_put_hcd(hcd);
err3:
	iounmap(hcd_base);
err2:
	release_mem_region(iores->start, resource_size(iores));
err1:
	clk_disable_unprepare(usbh_hclk);
	clk_disable_unprepare(usb_clk);
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
