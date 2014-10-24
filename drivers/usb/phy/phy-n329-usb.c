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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/stmp_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#define DRIVER_NAME "n329_phy"

#define to_n329_phy(p) container_of((p), struct n329_phy, phy)

struct n329_phy_data {
	unsigned int flags;
};

static const struct n329_phy_data n32905_phy_data = {
	.flags = 0,
};

static const struct of_device_id n329_phy_dt_ids[] = {
	{ .compatible = "nuvoton,n32905-usbphy", .data = &n32905_phy_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n329_phy_dt_ids);

struct n329_phy {
	struct usb_phy phy;
	const struct n329_phy_data *data;
};


static int n329_phy_init(struct usb_phy *phy)
{
	// struct n329_phy *n329_phy = to_n329_phy(phy);
	dev_dbg(phy->dev, "%s\n", __func__);
	return 0;
}

static void n329_phy_shutdown(struct usb_phy *phy)
{
	// struct n329_phy *n329_phy = to_n329_phy(phy);
	dev_dbg(phy->dev, "%s\n", __func__);
}

static int n329_phy_suspend(struct usb_phy *x, int suspend)
{
	// struct n329_phy *n329_phy = to_n329_phy(phy);
	dev_dbg(phy->dev, "%s\n", __func__);
	return 0;
}

static int n329_phy_set_wakeup(struct usb_phy *x, bool enabled)
{
	// struct n329_phy *n329_phy = to_n329_phy(phy);
	dev_dbg(phy->dev, "%s\n", __func__);
	return 0;
}

static int n329_phy_on_connect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	// struct n329_phy *n329_phy = to_n329_phy(phy);
	dev_dbg(phy->dev, "%s: %s device has connected\n",
		__func__, (speed == USB_SPEED_HIGH) ? "HS" : "FS/LS");

	return 0;
}

static int n329_phy_on_disconnect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	// struct n329_phy *n329_phy = to_n329_phy(phy);
	dev_dbg(phy->dev, "%s: %s device has disconnected\n",
		__func__, (speed == USB_SPEED_HIGH) ? "HS" : "FS/LS");

	return 0;
}

static int n329_phy_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *base;
	struct clk *clk;
	struct n329_phy *n329_phy;
	int ret;
	const struct of_device_id *of_id =
			of_match_device(n329_phy_dt_ids, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev,
			"can't get the clock, err=%ld", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	n329_phy = devm_kzalloc(&pdev->dev, sizeof(*n329_phy), GFP_KERNEL);
	if (!n329_phy) {
		dev_err(&pdev->dev, "Failed to allocate USB PHY structure!\n");
		return -ENOMEM;
	}

	ret = of_alias_get_id(np, "usbphy");
	if (ret < 0)
		dev_dbg(&pdev->dev, "failed to get alias id, errno %d\n", ret);
	n329_phy->port_id = ret;

	n329_phy->phy.io_priv = base;
	n329_phy->phy.dev = &pdev->dev;
	n329_phy->phy.label = DRIVER_NAME;
	n329_phy->phy.init = n329_phy_init;
	n329_phy->phy.shutdown = n329_phy_shutdown;
	n329_phy->phy.set_suspend = n329_phy_suspend;
	n329_phy->phy.notify_connect = n329_phy_on_connect;
	n329_phy->phy.notify_disconnect = n329_phy_on_disconnect;
	n329_phy->phy.type =USB_PHY_TYPE_USB2 ;
	n329_phy->phy.set_wakeup = n329_phy_set_wakeup;

	n329_phy->clk = clk;
	n329_phy->data = of_id->data;

	platform_set_drvdata(pdev, n329_phy);

	device_set_wakeup_capable(&pdev->dev, true);

	ret = usb_add_phy_dev(&n329_phy->phy);
	if (ret)
		return ret;

	return 0;
}

static int n329_phy_remove(struct platform_device *pdev)
{
	struct n329_phy *n329_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&n329_phy->phy);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int n329_phy_system_suspend(struct device *dev)
{
	// struct n329_phy *n329_phy = dev_get_drvdata(dev);
	return 0;
}

static int n329_phy_system_resume(struct device *dev)
{
	// struct n329_phy *n329_phy = dev_get_drvdata(dev);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(n329_phy_pm, 
		n329_phy_system_suspend,
		n329_phy_system_resume);

static struct platform_driver n329_phy_driver = {
	.probe = n329_phy_probe,
	.remove = n329_phy_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = n329_phy_dt_ids,
		.pm = &n329_phy_pm,
	 },
};

static int __init n329_phy_module_init(void)
{
	return platform_driver_register(&n329_phy_driver);
}
postcore_initcall(n329_phy_module_init);

static void __exit n329_phy_module_exit(void)
{
	platform_driver_unregister(&n329_phy_driver);
}
module_exit(n329_phy_module_exit);

MODULE_ALIAS("platform:n329-usb-phy");
MODULE_AUTHOR("Mike Thomspon <mpthompson@gmail.com>");
MODULE_DESCRIPTION("Nuvoton N329XX USB PHY driver");
MODULE_LICENSE("GPL");
