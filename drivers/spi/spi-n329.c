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

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/stmp_device.h>
#include <linux/spi/spi.h>

#define DRIVER_NAME		"n329-spi"

#define BITS(start,end)		((0xffffffff >> (31 - start)) & (0xffffffff << end))

#define REG_USI_CNT		(0x00)		/* SPI0 Control and status register */
	#define BYTEENDIN	BIT(20)		/* Byte endian bit flag */
	#define ENINT		BIT(17)		/* Interrupt enable */
	#define ENFLG		BIT(16)		/* Interrupt flag */
	#define SELECTPOL	BIT(11)		/* Clock polarity */
	#define LSB		BIT(10)		/* Send LSB first flag */
	#define TXNUM		BITS(9,8)	/* Transmit/receive in one transfer */
	#define TXBIT		BITS(7,3)	/* Transmit bit length */
	#define TXNEG		BIT(2)		/* Transmit on negative edge flag */
	#define RXNEG		BIT(1)		/* Receive on negative edge flag */
	#define GOBUSY		BIT(0)
#define REG_USI_DIV		(0x04)		/* SPI0 Clock divider register */
#define REG_USI_SSR		(0x08)		/* SPI0 Slave select register */
	#define SELECTSLAVE	BIT(0)		/* Slave select */
	#define SELECTLEV	BIT(2)		/* Defines chip select active level */
#define REG_USI_RX0		(0x10)		/* SPI0 Data receive registers */
#define REG_USI_TX0		(0x10)		/* SPI0 Data transmit registers */

struct n329_spi {
	unsigned reserved;
};

static int n329_spi_probe(struct platform_device *pdev)
{
#if 0
	struct device_node *np = pdev->dev.of_node;
	struct spi_master *master;
	struct n329_spi *spi;
	struct resource *iores;
	struct clk *clk;
	void __iomem *base;
	int devid, clk_freq;
	int ret = 0, irq_err;

	/*
	 * Default clock speed for the SPI core. 160MHz seems to
	 * work reasonably well with most SPI flashes, so use this
	 * as a default. Override with "clock-frequency" DT prop.
	 */
	const int clk_freq_default = 160000000;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_err = platform_get_irq(pdev, 0);
	if (irq_err < 0)
		return irq_err;

	base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	devid = (enum mxs_ssp_id) of_id->data;
	ret = of_property_read_u32(np, "clock-frequency",
				   &clk_freq);
	if (ret)
		clk_freq = clk_freq_default;

	master = spi_alloc_master(&pdev->dev, sizeof(*spi));
	if (!master)
		return -ENOMEM;

	master->transfer_one_message = mxs_spi_transfer_one;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->mode_bits = SPI_CPOL | SPI_CPHA;
	master->num_chipselect = 3;
	master->dev.of_node = np;
	master->flags = SPI_MASTER_HALF_DUPLEX;

	spi = spi_master_get_devdata(master);
	ssp = &spi->ssp;
	ssp->dev = &pdev->dev;
	ssp->clk = clk;
	ssp->base = base;
	ssp->devid = devid;

	init_completion(&spi->c);

	ret = devm_request_irq(&pdev->dev, irq_err, mxs_ssp_irq_handler, 0,
			       DRIVER_NAME, ssp);
	if (ret)
		goto out_master_free;

	ssp->dmach = dma_request_slave_channel(&pdev->dev, "rx-tx");
	if (!ssp->dmach) {
		dev_err(ssp->dev, "Failed to request DMA\n");
		ret = -ENODEV;
		goto out_master_free;
	}

	ret = clk_prepare_enable(ssp->clk);
	if (ret)
		goto out_dma_release;

	clk_set_rate(ssp->clk, clk_freq);

	ret = stmp_reset_block(ssp->base);
	if (ret)
		goto out_disable_clk;

	platform_set_drvdata(pdev, master);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret) {
		dev_err(&pdev->dev, "Cannot register SPI master, %d\n", ret);
		goto out_disable_clk;
	}

	return 0;

out_disable_clk:
	clk_disable_unprepare(ssp->clk);
out_dma_release:
	dma_release_channel(ssp->dmach);
out_master_free:
	spi_master_put(master);
	return ret;
#endif
	return 0;
}

static int n329_spi_remove(struct platform_device *pdev)
{
#if 0
	struct spi_master *master;
	struct n329_spi *spi;

	master = platform_get_drvdata(pdev);
	spi = spi_master_get_devdata(master);
#endif

	return 0;
}

static const struct of_device_id n329_spi_dt_ids[] = {
	{ .compatible = "nuvoton,n329-spi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n329_spi_dt_ids);

static struct platform_driver n329_spi_driver = {
	.probe	= n329_spi_probe,
	.remove	= n329_spi_remove,
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = n329_spi_dt_ids,
	},
};

module_platform_driver(n329_spi_driver);

MODULE_AUTHOR("Mike Thompson <mpthompson@gmail.com>");
MODULE_DESCRIPTION("Nuvoton N329XX SPI master driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:n329-spi");
