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
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

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

struct n329_spi_info {
	unsigned num_cs;
	unsigned lsb;
	unsigned txneg;
	unsigned rxneg;
	unsigned divider;
	unsigned sleep;
	unsigned txnum;
	unsigned txbitlen;
	unsigned byte_endin;
	int bus_num;
};

struct n329_spi_host {
	struct spi_bitbang bitbang;
	struct completion done;
	void __iomem *regs;
	int irq;
	int len;
	int count;
	int tx_num;
	const unsigned char *tx;
	unsigned char *rx;
	struct clk *clk;
	struct spi_master *master;
	struct device *dev;
	spinlock_t lock;
	struct resource *res;
	struct n329_spi_info *pdata;
};

static inline struct n329_spi_host *to_host(struct spi_device *sdev)
{
	return spi_master_get_devdata(sdev->master);
}

static void n329_spi_slave_select(struct spi_device *spi, unsigned ssr)
{
	struct n329_spi_host *host = to_host(spi);
	unsigned val;
	unsigned cs = spi->mode & SPI_CS_HIGH ? 1 : 0;
	unsigned cpol = spi->mode & SPI_CPOL ? 1 : 0;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_SSR);

	if (!cs)
		val &= ~SELECTLEV;
	else
		val |= SELECTLEV;

	if (!ssr)
		val &= ~SELECTSLAVE;
	else
		val |= SELECTSLAVE;

	__raw_writel(val, host->regs + REG_USI_SSR);

	val = __raw_readl(host->regs + REG_USI_CNT);

	if (!cpol)
		val &= ~SELECTPOL;
	else
		val |= SELECTPOL;

	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static void n329_spi_chipselect(struct spi_device *spi, int value)
{
	switch (value) {
	case BITBANG_CS_INACTIVE:
		n329_spi_slave_select(spi, 0);
		break;

	case BITBANG_CS_ACTIVE:
		n329_spi_slave_select(spi, 1);
		break;
	}
}

static void n329_spi_set_txnum(struct n329_spi_host *host, unsigned txnum)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	host->tx_num = txnum;

	val = __raw_readl(host->regs + REG_USI_CNT);

	if (!txnum)
		val &= ~TXNUM;
	else
		val |= txnum << 0x08;

	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);

}

static void n329_spi_set_txbitlen(struct n329_spi_host *host, unsigned txbitlen)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_CNT) & ~TXBIT;

	if (txbitlen == 32)
		txbitlen = 0;

	val |= (txbitlen << 0x03);

	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static void n329_spi_setup_byte_endin(struct n329_spi_host *host,
					unsigned endin)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_CNT) & ~BYTEENDIN;

	val |= (endin << 20);

	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static void n329_spi_gobusy(struct n329_spi_host *host)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_CNT);

	val |= GOBUSY;

	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static int n329_spi_setup_transfer(struct spi_device *spi,
				 struct spi_transfer *t)
{
	return 0;
}

static int n329_spi_setup(struct spi_device *spi)
{
	return 0;
}

static inline unsigned hw_txbyte(struct n329_spi_host *host, int count)
{
	return host->tx ? host->tx[count] : 0;
}

static inline unsigned hw_txword(struct n329_spi_host *host, int count)
{
	unsigned *p32tmp;

	if (host->tx == 0)
		return 0;
	else
	{
		p32tmp = (unsigned *)((unsigned )(host->tx) + count);
		return *p32tmp;
	}
}

static int n329_spi_txrx_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	int i;
	struct n329_spi_host *host = to_host(spi);

	host->tx = t->tx_buf;
	host->rx = t->rx_buf;
	host->len = t->len;
	host->count = 0;

	//printk("host->len %d 0x%x\n", host->len, host->rx);

	if(host->len < 4)
	{
		n329_spi_setup_byte_endin(host, 0);
		n329_spi_set_txbitlen(host, 8);
		n329_spi_set_txnum(host, 0);
		__raw_writel(hw_txbyte(host, 0x0), host->regs + REG_USI_TX0);

	}
	else
	{
		n329_spi_setup_byte_endin(host, 1);
		n329_spi_set_txbitlen(host, 32);

		if(host->len >= 16)
		{
			n329_spi_set_txnum(host, 3);
			for(i=0;i<4;i++)
				__raw_writel(hw_txword(host, i * 4), 
					host->regs + REG_USI_TX0 + i * 4);
		}
		else
		{
			n329_spi_set_txnum(host, 0);
			__raw_writel(hw_txword(host, 0x0), 
					host->regs + REG_USI_TX0);
		}
	}

	n329_spi_gobusy(host);

	wait_for_completion(&host->done);

	return host->count;
}

static irqreturn_t n329_spi_irq(int irq, void *dev)
{
	struct n329_spi_host *host = dev;
	unsigned count = host->count;
	unsigned status;
	unsigned val,i;
	unsigned *p32tmp;

	status = __raw_readl(host->regs + REG_USI_CNT);
	__raw_writel(status, host->regs + REG_USI_CNT);

	if (status & ENFLG) {

		val = __raw_readl(host->regs + REG_USI_CNT) & BYTEENDIN;

		if(val)
		{
			host->count = host->count + (host->tx_num + 1) * 4;

			if (host->rx)
			{
				p32tmp = (unsigned *)((unsigned )(host->rx) + count);

				for(i = 0; i < (host->tx_num + 1); i++)
				{
					*p32tmp = __raw_readl(host->regs + 
							REG_USI_TX0 + i * 4);
					p32tmp++;
				}
			}

			count = count + (host->tx_num + 1) * 4;

			if (count < host->len)
			{
				if ((count+16) <= host->len)
				{
					for(i = 0; i < 4; i++)
						__raw_writel(hw_txword(host, 
							(count + i * 4)), 
							host->regs + REG_USI_TX0 + i * 4);
				}
				else if ((count+4) <= host->len)
				{
					n329_spi_set_txnum(host, 0);
					__raw_writel(hw_txword(host, count), 
							host->regs + REG_USI_TX0);
				}
				else
				{
					n329_spi_setup_byte_endin(host, 0);
					n329_spi_set_txbitlen(host, 8);
					n329_spi_set_txnum(host, 0);
					__raw_writel(hw_txbyte(host, count), 
							host->regs + REG_USI_TX0);
				}
				n329_spi_gobusy(host);
			}
			else
			{
				complete(&host->done);
			}
		}
		else
		{
			host->count++;

			if (host->rx)
				host->rx[count] = __raw_readl(host->regs + 
								REG_USI_RX0);
			count++;

			if (count < host->len) {
				__raw_writel(hw_txbyte(host, count), 
							host->regs + REG_USI_TX0);
				n329_spi_gobusy(host);
			} else {
				complete(&host->done);
			}
		}

		return IRQ_HANDLED;
	}

	complete(&host->done);

	return IRQ_HANDLED;
}

static void n329_spi_tx_edge(struct n329_spi_host *host, unsigned edge)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_CNT);

	if (edge)
		val |= TXNEG;
	else
		val &= ~TXNEG;
	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static void n329_spi_rx_edge(struct n329_spi_host *host, unsigned edge)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_CNT);
	if (edge)
		val |= RXNEG;
	else
		val &= ~RXNEG;
	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static void n329_send_first(struct n329_spi_host *host, unsigned lsb)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_CNT);
	if (lsb)
		val |= LSB;
	else
		val &= ~LSB;
	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static void n329_spi_set_sleep(struct n329_spi_host *host, unsigned sleep)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_CNT);
	if (sleep)
		val |= (sleep << 12);
	else
		val &= ~(0x0f << 12);
	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static void n329_spi_enable_int(struct n329_spi_host *host)
{
	unsigned val;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	val = __raw_readl(host->regs + REG_USI_CNT);
	val |= ENINT;
	__raw_writel(val, host->regs + REG_USI_CNT);

	spin_unlock_irqrestore(&host->lock, flags);
}

static void n329_spi_set_divider(struct n329_spi_host *host)
{
	__raw_writel(host->pdata->divider, host->regs + REG_USI_DIV);
}

static void n329_spi_init(struct n329_spi_host *host)
{
	clk_enable(host->clk);

	// XXX writel(readl(REG_APBIPRST) | SPI0RST, REG_APBIPRST);
	// XXX writel(readl(REG_APBIPRST) & ~SPI0RST, REG_APBIPRST);
	spin_lock_init(&host->lock);

	n329_spi_tx_edge(host, host->pdata->txneg);
	n329_spi_rx_edge(host, host->pdata->rxneg);
	n329_send_first(host, host->pdata->lsb);
	n329_spi_set_sleep(host, host->pdata->sleep);
	n329_spi_set_txbitlen(host, host->pdata->txbitlen);
	n329_spi_set_txnum(host, host->pdata->txnum);
	n329_spi_set_divider(host);
	n329_spi_enable_int(host);
}

static struct n329_spi_info spi_info = {
	.num_cs = 1,
	.lsb = 0,
	.txneg = 1,
	.rxneg = 0,
	.divider = 0,
	.sleep = 0,
	.txnum = 0,
	.txbitlen = 8,
	.byte_endin = 0,
	.bus_num = 0,
};

static int n329_spi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct n329_spi_host *host;
	struct spi_master *master;
	struct resource *iores;
	int ret = 0;

	master = spi_alloc_master(&pdev->dev, sizeof(*host));
	if (!master)
		return -ENOMEM;

	host = spi_master_get_devdata(master);
	memset(host, 0, sizeof(struct n329_spi_host));

	platform_set_drvdata(pdev, host);

	host->pdata  = &spi_info;
	host->dev = &pdev->dev;
	init_completion(&host->done);

	host->master = spi_master_get(master);
	host->master->mode_bits = SPI_MODE_0;
	host->master->num_chipselect = host->pdata->num_cs;
	host->master->bus_num = host->pdata->bus_num;

	host->bitbang.master = host->master;
	host->bitbang.master->setup = n329_spi_setup;
	host->bitbang.txrx_bufs = n329_spi_txrx_bufs;
	host->bitbang.chipselect = n329_spi_chipselect;
	host->bitbang.setup_transfer = n329_spi_setup_transfer;

	host->irq = platform_get_irq(pdev, 0);
	ret = request_irq(host->irq, n329_spi_irq, 0, pdev->name, host);
	if (ret) {
		dev_err(&pdev->dev, "Failed to claim IRQ\n");
		goto out_master_free;
	}

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->regs = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(host->regs)) {
		ret = PTR_ERR(host->regs);
		dev_err(&pdev->dev, "Failed to map registers\n");
		goto out_irq_free;
	}

	host->clk = of_clk_get(np, 0);
	if (IS_ERR(host->clk)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "Failed to get clock\n");
		goto out_io_free;
	}
	clk_prepare_enable(host->clk);

	n329_spi_init(host);

	ret = spi_bitbang_start(&host->bitbang);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register SPI master\n");
		goto out_clk_free;
	}

	return 0;

out_clk_free:
	clk_disable_unprepare(host->clk);
out_io_free:
	iounmap(host->regs);
out_irq_free:
	free_irq(host->irq, host);
out_master_free:
	spi_master_put(master);
	return ret;
}

static int n329_spi_remove(struct platform_device *pdev)
{
	struct n329_spi_host *host;

	host = platform_get_drvdata(pdev);

	free_irq(host->irq, host);

	spi_unregister_master(host->master);
	
	clk_disable_unprepare(host->clk);

	iounmap(host->regs);

	spi_master_put(host->master);

	platform_set_drvdata(pdev, NULL);

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
