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
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/highmem.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/stmp_device.h>

#define DRIVER_NAME	"n329-mmc"

#define N329_ADDR(x)		((void __iomem *)0xF0000000 + (x))
#define BITS(start,end)		((0xFFFFFFFF >> (31 - start)) & (0xFFFFFFFF << end))

/* Serial Interface Controller (SIC) Registers */
#define SIC_BASE		0x0000
#define DMAC_BA			(SIC_BASE)			/* DMAC Registers */
#define FMI_BA			(SIC_BASE + 0x800)	/* Flash Memory Card Interface */

#define REG_FB_0		(DMAC_BA + 0x000)	/* Shared Buffer (FIFO) */

#define	REG_DMACCSR		(DMAC_BA + 0x400)	/* DMAC Control and Status Register */
	#define	FMI_BUSY			BIT(9)		/* FMI DMA transfer is in progress */
	#define SG_EN				BIT(3)		/* DMAC Scatter-gather function enable */
	#define DMAC_SWRST			BIT(1)		/* DMAC software reset enable */
	#define DMAC_EN				BIT(0)		/* DMAC enable */

#define REG_DMACSAR		(DMAC_BA + 0x408)	/* DMAC Transfer Starting Address Register */
#define REG_DMACBCR		(DMAC_BA + 0x40C)	/* DMAC Transfer Byte Count Register */
#define REG_DMACIER		(DMAC_BA + 0x410)	/* DMAC Interrupt Enable Register */
	#define	WEOT_IE				BIT(1)		/* Wrong EOT encounterred interrupt enable */
	#define TABORT_IE			BIT(0)		/* DMA R/W target abort interrupt enable */

#define REG_DMACISR		(DMAC_BA + 0x414)	/* DMAC Interrupt Status Register */
	#define	WEOT_IF				BIT(1)		/* Wrong EOT encounterred interrupt flag */
	#define TABORT_IF			BIT(0)		/* DMA R/W target abort interrupt flag */

/* Flash Memory Card Interface Registers */
#define REG_FMICR		(FMI_BA + 0x000)	/* FMI Control Register */
	#define	FMI_SM_EN			BIT(3)		/* Enable FMI SM function */
	#define FMI_SD_EN			BIT(1)		/* Enable FMI SD function */
	#define FMI_SWRST			BIT(0)		/* Enable FMI software reset */

#define REG_FMIIER		(FMI_BA + 0x004)   	/* FMI DMA transfer starting address register */
	#define	FMI_DAT_IE			BIT(0)		/* Enable DMAC READ/WRITE targe abort interrupt generation */

#define REG_FMIISR		(FMI_BA + 0x008)   	/* FMI DMA byte count register */
	#define	FMI_DAT_IF			BIT(0)		/* DMAC READ/WRITE targe abort interrupt flag register */

/* Secure Digit Registers */
#define REG_SDCR		(FMI_BA + 0x020)   	/* SD Control Register */
	#define	SDCR_CLK_KEEP1		BIT(31)		/* SD-1 clock keep control */
	#define	SDCR_SDPORT			BITS(30,29)	/* SD port select */
	#define	SDCR_SDPORT_0		0			/* SD-0 port selected */
	#define	SDCR_SDPORT_1		BIT(29)		/* SD-1 port selected */
	#define	SDCR_SDPORT_2		BIT(30)		/* SD-2 port selected */
	#define	SDCR_CLK_KEEP2		BIT(28)		/* SD-1 clock keep control */
	#define	SDCR_SDNWR			BITS(27,24)	/* Nwr paramter for block write operation */
	#define SDCR_BLKCNT			BITS(23,16)	/* Block count to be transferred or received */
	#define	SDCR_DBW			BIT(15)		/* SD data bus width selection */
	#define	SDCR_SWRST			BIT(14)		/* Enable SD software reset */
	#define	SDCR_CMD_CODE		BITS(13,8)	/* SD command code */
	#define	SDCR_CLK_KEEP		BIT(7)		/* SD clock enable */
	#define SDCR_8CLK_OE		BIT(6)		/* 8 clock cycles output enable */
	#define SDCR_74CLK_OE		BIT(5)		/* 74 clock cycle output enable */
	#define SDCR_R2_EN			BIT(4)		/* Response R2 input enable */
	#define SDCR_DO_EN			BIT(3)		/* Data output enable */
	#define SDCR_DI_EN			BIT(2)		/* Data input enable */
	#define SDCR_RI_EN			BIT(1)		/* Response input enable */
	#define SDCR_CO_EN			BIT(0)		/* Command output enable */

#define REG_SDARG 		(FMI_BA + 0x024)   	/* SD command argument register */

#define REG_SDIER		(FMI_BA + 0x028)   	/* SD interrupt enable register */
	#define	SDIER_CDSRC			BIT(30)		/* SD card detection source selection: SD-DAT3 or GPIO */
	#define	SDIER_R1B_IEN		BIT(24)		/* R1b interrupt enable */
	#define	SDIER_WKUP_EN		BIT(14)		/* SDIO wake-up signal geenrating enable */
	#define	SDIER_DITO_IEN		BIT(13)		/* SD data input timeout interrupt enable */
	#define	SDIER_RITO_IEN		BIT(12)		/* SD response input timeout interrupt enable */
	#define SDIER_SDIO_IEN		BIT(10)		/* SDIO interrupt status enable (SDIO issue interrupt via DAT[1] */
	#define SDIER_CD_IEN		BIT(8)		/* CD# interrupt status enable */
	#define SDIER_CRC_IEN		BIT(1)		/* CRC-7, CRC-16 and CRC status error interrupt enable */
	#define SDIER_BLKD_IEN		BIT(0)		/* Block transfer done interrupt interrupt enable */

#define REG_SDISR		(FMI_BA + 0x02C)   	/* SD interrupt status register */
	#define	SDISR_R1B_IF		BIT(24)		/* R1b interrupt flag */
	#define SDISR_SD_DATA1		BIT(18)		/* SD DAT1 pin status */
	#define SDISR_CD_Card		BIT(16)		/* CD detection pin status */
	#define	SDISR_DITO_IF		BIT(13)		/* SD data input timeout interrupt flag */
	#define	SDISR_RITO_IF		BIT(12)		/* SD response input timeout interrupt flag */
	#define	SDISR_SDIO_IF		BIT(10)		/* SDIO interrupt flag (SDIO issue interrupt via DAT[1] */
	#define	SDISR_CD_IF			BIT(8)		/* CD# interrupt flag */
	#define SDISR_SD_DATA0		BIT(7)		/* SD DATA0 pin status */
	#define SDISR_CRC			BITS(6,4)	/* CRC status */
	#define SDISR_CRC_16		BIT(3)		/* CRC-16 Check Result Status */
	#define SDISR_CRC_7			BIT(2)		/* CRC-7 Check Result Status */
	#define	SDISR_CRC_IF		BIT(1)		/* CRC-7, CRC-16 and CRC status error interrupt status */
	#define	SDISR_BLKD_IF		BIT(0)		/* Block transfer done interrupt interrupt status */

#define REG_SDRSP0		(FMI_BA + 0x030)   	/* SD receive response token register 0 */
#define REG_SDRSP1		(FMI_BA + 0x034)   	/* SD receive response token register 1 */
#define REG_SDBLEN		(FMI_BA + 0x038)   	/* SD block length register */
#define REG_SDTMOUT 	(FMI_BA + 0x03C)   	/* SD block length register */

#define MCI_BLKSIZE         512
#define MCI_MAXBLKSIZE      4095
#define MCI_BLKATONCE       255
#define MCI_BUFSIZE         (MCI_BLKSIZE * MCI_BLKATONCE)

#define MCI_VDD_AVAIL		(MMC_VDD_27_28 | MMC_VDD_28_29 | \
							 MMC_VDD_29_30 | MMC_VDD_30_31 | \
							 MMC_VDD_31_32 | MMC_VDD_32_33 | \
							 MMC_VDD_33_34)

enum n329_sic_type {
	N32905_SIC
};

struct n329_mmc_host {
	struct mmc_host *mmc;
	struct mmc_command *cmd;
	struct mmc_request *request;

	u32 bus_mode;   				/* MMC_BUS_WIDTH_1 | MMC_BUS_WIDTH_4 */
	u32 port;						/* SD port 0 | 1 | 2 */

	unsigned int *buffer;			/* DMA buffer used for transmitting */
	dma_addr_t physical_address;	/* DMA physical address */
	unsigned int total_length;

	struct clk *sd_src_clk;
	struct clk *sd_div_clk;
	struct clk *sd_clk;
	struct clk *sic_clk;

#if 0
	struct mmc_request		*mrq;
	struct mmc_command		*cmd;
	struct mmc_data			*data;
#endif

	void __iomem *base;
	int wp_gpio;

	spinlock_t lock;
};

static inline void n329_sd_write(struct n329_mmc_host *host, u32 value, u32 addr)
{
	__raw_writel(value, host->base + addr);
}

static inline u32 n329_sd_read(struct n329_mmc_host *host, u32 addr)
{
	return __raw_readl(host->base + addr);
}

static int n329_sd_select_port(struct n329_mmc_host *host)
{
	if (host->port == 0)
		n329_sd_write(host, (n329_sd_read(host, REG_SDCR) & ~SDCR_SDPORT) | 
						SDCR_SDPORT_0, REG_SDCR);
#if 0
	else if (host->port == 1)
		n329_sd_write(host, (n329_sd_read(host, REG_SDCR) & ~SDCR_SDPORT) | 
						SDCR_SDPORT_1, REG_SDCR);
	else if (host->port == 2)
		n329_sd_write(host, (n329_sd_read(host, REG_SDCR) & ~SDCR_SDPORT) | 
						SDCR_SDPORT_2, REG_SDCR);
#endif
	else
	{
		pr_err("ERROR: Don't support SD port %d!\n", host->port);
		return -1;
	}
   
	return 0;
}

static int n329_sd_setup_wp(struct n329_mmc_host *host, struct device *dev)
{
	int error;

	if (!gpio_is_valid(host->wp_gpio))
		return -ENODEV;

	error = devm_gpio_request_one(dev, host->wp_gpio,
						GPIOF_IN, DRIVER_NAME);
	if (error < 0) {
		dev_err(dev, "Failed to request GPIO %d, error %d\n",
					host->wp_gpio, error);
		return error;
	}

	error = gpio_direction_input(host->wp_gpio);
	if (error < 0) {
		dev_err(dev, "Failed to configure GPIO %d as input, error %d\n",
					host->wp_gpio, error);
		return error;
	}
	
	return 0;
}

static void n329_sd_enable(struct n329_mmc_host *host)
{
	/* Enable SD card detect pin */
	n329_sd_write(host, n329_sd_read(host, REG_SDIER) | 
					SDIER_CDSRC, REG_SDIER);

	/* Enable DMAC for FMI */
	n329_sd_write(host, n329_sd_read(host, REG_DMACCSR) | 
					DMAC_EN, REG_DMACCSR);

	/* Enable SD */
	n329_sd_write(host, FMI_SD_EN, REG_FMICR);

	/* Write bit 1 to clear all SDISR */
	n329_sd_write(host, 0xFFFFFFFF, REG_SDISR);

	/* Select SD port */
	if (n329_sd_select_port(host) != 0)
		return;

	/* SDNWR = 9+1 clock */
	n329_sd_write(host, (n329_sd_read(host, REG_SDCR) & ~SDCR_SDNWR) | 
					0x09000000, REG_SDCR);

	/* SDCR_BLKCNT = 1 */
	n329_sd_write(host, (n329_sd_read(host, REG_SDCR) & ~SDCR_BLKCNT) | 
					0x00010000, REG_SDCR);

#if 0
	// set GPA0 to GPIO mode for SD port 0 write protect
	w55fa93_sd_write(REG_GPAFUN, w55fa93_sd_read(REG_GPAFUN) & (~MF_GPA0));     // set GPIO to GPIO mode for write protect
	w55fa93_sd_write(REG_GPIOA_OMD, w55fa93_sd_read(REG_GPIOA_OMD) & (~BIT(0)));  // set GPA0 to input mode
#endif
}

static void n329_sd_disable(struct n329_mmc_host *host)
{
	/* Write to clear all SDISR */
	n329_sd_write(host, 0xffffffff, REG_SDISR);

	/* Disable SD */
	n329_sd_write(host, n329_sd_read(host, REG_FMICR) & 
					(~FMI_SD_EN), REG_FMICR);
}

static int n329_mmc_get_cd(struct mmc_host *mmc)
{
	return -ENOSYS;
}

static void n329_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
}

static void n329_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
}

static void n329_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
}

static const struct mmc_host_ops n329_mmc_ops = {
	.request = n329_mmc_request,
	.get_ro = mmc_gpio_get_ro,
	.get_cd = n329_mmc_get_cd,
	.set_ios = n329_mmc_set_ios,
	.enable_sdio_irq = n329_mmc_enable_sdio_irq,
};

static struct platform_device_id n329_ssp_ids[] = {
	{
		.name = "n32905-mmc",
		.driver_data = N32905_SIC,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, n329_ssp_ids);

static const struct of_device_id n329_mmc_dt_ids[] = {
	{ .compatible = "nuvoton,n32905-mmc", .data = (void *) N32905_SIC, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n329_mmc_dt_ids);

static int n329_mmc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct n329_mmc_host *host;
	struct mmc_host *mmc;
	struct resource *iores;
	void __iomem *base;
	int irq_err, ret = 0;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_err = platform_get_irq(pdev, 0);
	if (!iores || irq_err < 0)
		return -EINVAL;

	mmc = mmc_alloc_host(sizeof(struct n329_mmc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out_mmc_free;
	}

	mmc->ops = 	&n329_mmc_ops;
	mmc->f_min = 300000;
	mmc->f_max = 24000000;
	mmc->ocr_avail = MCI_VDD_AVAIL;
	mmc->caps = MMC_CAP_4_BIT_DATA;

	mmc->max_seg_size  = MCI_BUFSIZE;
	mmc->max_segs      = MCI_BLKATONCE;
	mmc->max_req_size  = MCI_BUFSIZE;
	mmc->max_blk_size  = MCI_MAXBLKSIZE;
	mmc->max_blk_count = MCI_BLKATONCE;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->bus_mode = MMC_BUS_WIDTH_1;
	host->port = 0;
	host->base = base;
	spin_lock_init(&host->lock);

	host->buffer = dma_alloc_coherent(&pdev->dev, MCI_BUFSIZE, 
							&host->physical_address, GFP_KERNEL);
	if (!host->buffer) {
		ret = -ENOMEM;
		goto out_mmc_free;
	}

	if (of_find_property(np, "gpios", NULL)) {
		int gpio = of_get_gpio(np, 0);
		if (gpio < 0) {
			ret = gpio;
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"Failed to get gpio flags, error: %d\n", ret);
			goto out_dma_free;
		}
		host->wp_gpio = gpio;
		ret = n329_sd_setup_wp(host, &pdev->dev);
		if (ret < 0)
			goto out_dma_free;
	} else {
		host->wp_gpio = -1;
	}

	host->sd_src_clk = of_clk_get(np, 0);
	host->sd_div_clk = of_clk_get(np, 1);
	host->sd_clk = of_clk_get(np, 2);
	host->sic_clk = of_clk_get(np, 3);
	if (IS_ERR(host->sd_src_clk) || IS_ERR(host->sd_div_clk) ||
		IS_ERR(host->sd_clk) || IS_ERR(host->sic_clk)) {
		ret = -ENODEV;
		goto out_dma_free;
	}
	clk_prepare_enable(host->sd_src_clk);
	clk_prepare_enable(host->sd_div_clk);
	clk_prepare_enable(host->sd_clk);
	clk_prepare_enable(host->sic_clk);

	pr_info("SD SRC clock = %lu\n", clk_get_rate(host->sd_src_clk));
	pr_info("SD DIV clock = %lu\n", clk_get_rate(host->sd_div_clk));
	pr_info("SD clock = %lu\n", clk_get_rate(host->sd_clk));

	n329_sd_disable(host);
	n329_sd_enable(host);

	goto out_dma_free;

#if 0
	ret = mmc_add_host(mmc);
	if (ret)
		goto out_free_dma;

	return 0;
#endif
out_dma_free:
	dma_free_coherent(&pdev->dev, MCI_BUFSIZE, host->buffer, host->physical_address);
out_mmc_free:
	mmc_free_host(mmc);
	return ret;

}

static int n329_mmc_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct n329_mmc_host *host;

	if (!mmc)
		return -1;

	host = mmc_priv(mmc);

	clk_disable_unprepare(host->sic_clk);
	clk_disable_unprepare(host->sd_clk);
	clk_disable_unprepare(host->sd_div_clk);
	clk_disable_unprepare(host->sd_src_clk);

	if (host->buffer)
		dma_free_coherent(&pdev->dev, MCI_BUFSIZE, host->buffer, host->physical_address);

#if 0
	w55fa93_sd_disable(host);
	del_timer_sync(&host->timer);
	mmc_remove_host(mmc);
	free_irq(host->irq, host);

	clk_disable(host->sd_clk);
	clk_put(host->sd_clk);
#endif

	mmc_free_host(mmc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver n329_mmc_driver = {
	.probe		= n329_mmc_probe,
	.remove		= n329_mmc_remove,
	.id_table	= n329_ssp_ids,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = n329_mmc_dt_ids,
	},
};

module_platform_driver(n329_mmc_driver);

MODULE_DESCRIPTION("Nuvoton N329XX SD card peripheral");
MODULE_AUTHOR("Michael P. Thompson <mpthompson@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
