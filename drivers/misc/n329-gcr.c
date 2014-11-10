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
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/n329-gcr.h>

struct n329_gcr {
	void __iomem *base;
	struct semaphore sem;
	u32 (*read)(struct n329_gcr *, u32 addr);
	void (*write)(struct n329_gcr *, u32 value, u32 addr);
};

#define to_gcr(dev)	platform_get_drvdata(to_platform_device(dev))

static u32 n329_gcr_read_reg(struct n329_gcr *gcr, u32 addr)
{
	return __raw_readl(gcr->base + addr);
}

static void n329_gcr_write_reg(struct n329_gcr *gcr, u32 value, u32 addr)
{
	__raw_writel(value, gcr->base + addr);
}

static void n329_gcr_reset(struct n329_gcr *gcr)
{
	/* Do nothing for now */
}

int n329_gcr_read(struct device *dev, u32 addr)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct n329_gcr *gcr = platform_get_drvdata(pdev);

	return gcr->read(gcr, addr);
}
EXPORT_SYMBOL_GPL(n329_gcr_read);

void n329_gcr_write(struct device *dev, u32 value, u32 addr)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct n329_gcr *gcr = platform_get_drvdata(pdev);

	gcr->write(gcr, value, addr);
}
EXPORT_SYMBOL_GPL(n329_gcr_write);

int n329_gcr_down(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct n329_gcr *gcr = platform_get_drvdata(pdev);

	return down_interruptible(&gcr->sem);
}
EXPORT_SYMBOL_GPL(n329_gcr_down);

void n329_gcr_up(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct n329_gcr *gcr = platform_get_drvdata(pdev);

	up(&gcr->sem);
}
EXPORT_SYMBOL_GPL(n329_gcr_up);

int n329_gcr_ahbip_reset(struct device *dev, u32 reset)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct n329_gcr *gcr = platform_get_drvdata(pdev);
	u32 val;
	int ret;

	ret = down_interruptible(&gcr->sem);

	if (!ret) {
		val = gcr->read(gcr, REG_GCR_AHBIPRST);
		val |= reset;
		gcr->write(gcr, val, REG_GCR_AHBIPRST);
		val &= ~reset;
		gcr->write(gcr, val, REG_GCR_AHBIPRST);

		up(&gcr->sem);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(n329_gcr_ahbip_reset);

int n329_gcr_apbip_reset(struct device *dev, u32 reset)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct n329_gcr *gcr = platform_get_drvdata(pdev);
	u32 val;
	int ret;

	ret = down_interruptible(&gcr->sem);

	if (!ret) {
		val = gcr->read(gcr, REG_GCR_APBIPRST);
		val |= reset;
		gcr->write(gcr, val, REG_GCR_APBIPRST);
		val &= ~reset;
		gcr->write(gcr, val, REG_GCR_APBIPRST);

		up(&gcr->sem);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(n329_gcr_apbip_reset);

static int n329_gcr_probe(struct platform_device *pdev)
{
	struct resource *mem_res;
	struct n329_gcr *gcr;

	gcr = devm_kzalloc(&pdev->dev, sizeof(*gcr), GFP_KERNEL);
	if (!gcr)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gcr->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(gcr->base))
		return PTR_ERR(gcr->base);

	platform_set_drvdata(pdev, gcr);

 	sema_init(&gcr->sem, 1);
 
	gcr->read = n329_gcr_read_reg;
	gcr->write = n329_gcr_write_reg;

	n329_gcr_reset(gcr);

	return 0;
}

static int n329_gcr_remove(struct platform_device *pdev)
{
	struct n329_gcr *gcr = platform_get_drvdata(pdev);

	iounmap(gcr->base);
	devm_kfree(&pdev->dev, gcr);

	return 0;
}

static const struct of_device_id n329_gcr_dt_ids[] = {
	{
		.compatible = "nuvoton,n329-gcr"
	},
	{ /* sentinel */ }
};

static struct platform_driver n329_gcr_driver = {
	.driver		= {
		.name	= "gcr",
		.owner	= THIS_MODULE,
		.of_match_table = n329_gcr_dt_ids,
	},
	.probe = n329_gcr_probe,
	.remove = n329_gcr_remove,
};

static int __init n329_gcr_init(void)
{
	return platform_driver_register(&n329_gcr_driver);
}
postcore_initcall(n329_gcr_init);

static void __exit n329_gcr_exit(void)
{
	platform_driver_unregister(&n329_gcr_driver);
}
module_exit(n329_gcr_exit);

MODULE_DESCRIPTION("Nuvoton N329XX GCR driver");
MODULE_AUTHOR("Michael P. Thompson <mpthompson@gmail.com>");
MODULE_LICENSE("GPL v2");
