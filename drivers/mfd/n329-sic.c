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
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mfd/n329-sic.h>

struct n329_sic {
	void __iomem *base;
	spinlock_t lock;
	u32 (*read)(struct n329_sic *, u32 addr);
	void (*write)(struct n329_sic *, u32 value, u32 addr);
};

#define to_sic(dev)	platform_get_drvdata(to_platform_device(dev))

static u32 n329_sic_read_reg(struct n329_sic *sic, u32 addr)
{
	return __raw_readl(sic->base + addr);
}

static void n329_sic_write_reg(struct n329_sic *sic, u32 value, u32 addr)
{
	__raw_writel(value, sic->base + addr);
}

static void n329_sic_reset(struct n329_sic *sic)
{
	/* Reset DMAC */
	n329_sic_write_reg(sic, DMAC_SWRST, REG_DMACCSR);
	while (n329_sic_read_reg(sic, REG_DMACCSR) & DMAC_SWRST);

	/* Reset FMI */
	n329_sic_write_reg(sic, FMI_SWRST, REG_FMICR);
	while (n329_sic_read_reg(sic, REG_FMICR) & FMI_SWRST);
}

int n329_sic_read(struct device *dev, u32 addr)
{
	struct n329_sic *sic = to_sic(dev);

	return sic->read(sic, addr);
}
EXPORT_SYMBOL_GPL(n329_sic_read);

void n329_sic_write(struct device *dev, u32 value, u32 addr)
{
	struct n329_sic *sic = to_sic(dev);

	sic->write(sic, value, addr);
}
EXPORT_SYMBOL_GPL(n329_sic_write);

static int n329_sic_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *mem_res;
	struct n329_sic *sic;

	sic = devm_kzalloc(&pdev->dev, sizeof(*sic), GFP_KERNEL);
	if (!sic)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sic->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(sic->base))
		return PTR_ERR(sic->base);

	platform_set_drvdata(pdev, sic);

	sic->read = n329_sic_read_reg;
	sic->write = n329_sic_write_reg;

	spin_lock_init(&sic->lock);

	n329_sic_reset(sic);

	return of_platform_populate(np, NULL, NULL, &pdev->dev);
}

static const struct of_device_id n329_sic_dt_ids[] = {
	{
		.compatible = "nuvoton,n32905-sic"
	},
	{ /* sentinel */ }
};

static struct platform_driver n329_sic_driver = {
	.probe		= n329_sic_probe,
	.driver		= {
		.name	= "sic",
		.owner	= THIS_MODULE,
		.of_match_table = n329_sic_dt_ids,
	},
};
module_platform_driver(n329_sic_driver);

MODULE_DESCRIPTION("Nuvoton SIC driver");
MODULE_AUTHOR("Michael P. Thompson <mpthompson@gmail.com>");
MODULE_LICENSE("GPL v2");
