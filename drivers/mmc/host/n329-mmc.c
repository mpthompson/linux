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
#include <linux/kthread.h>
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
#define MCI_MAXBLKSIZE      4096
#define MCI_BLKATONCE       255
#define MCI_BUFSIZE         (MCI_BLKSIZE * MCI_BLKATONCE)

#define MCI_VDD_AVAIL		(MMC_VDD_32_33 | MMC_VDD_33_34)

enum n329_sic_type {
	N32905_SIC
};

struct n329_mmc_host {
	struct mmc_host	*mmc;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data	*data;

	dma_addr_t physical_address;
	unsigned int *buffer;
	unsigned int total_length;

	int irq;
	int wp_gpio;
	int sdio_irq_en;
	spinlock_t lock;
	unsigned char bus_width;
	struct clk *sd_clk;
	struct clk *sic_clk;
	void __iomem *base;
};

static void n329_mmc_start_cmd(struct n329_mmc_host *host,
			 struct mmc_command *cmd);

static inline void n329_mmc_write(struct n329_mmc_host *host, u32 value, u32 addr)
{
	__raw_writel(value, host->base + addr);
}

static inline u32 n329_mmc_read(struct n329_mmc_host *host, u32 addr)
{
	return __raw_readl(host->base + addr);
}

static irqreturn_t n329_mmc_irq(int irq, void *devid)
{
	struct n329_mmc_host *host = devid;
	unsigned status;

	status = n329_mmc_read(host, REG_SDISR);

	/* SDIO interrupt? */
	if (status & SDISR_SDIO_IF) {
		/* Notify the MMC core of the interrupt */
		mmc_signal_sdio_irq(host->mmc);

		/* Clear the interrupt */
		n329_mmc_write(host, SDISR_SDIO_IF, REG_SDISR);
	}

	/* Block transfer done? */
	if (status & SDISR_BLKD_IF) {

		/* Do something here */

		/* Clear the interrupt */
		n329_mmc_write(host, SDISR_BLKD_IF, REG_SDISR);
	}

	/* Card insert/remove detected? */
	if (status & SDISR_CD_IF) {

		/* Do something here */

		/* Clear the interrupt */
		n329_mmc_write(host, SDISR_CD_IF, REG_SDISR);
	}

	return IRQ_HANDLED;
}

static int n329_mmc_reset(struct n329_mmc_host *host)
{
	/* Reset DMAC */
	n329_mmc_write(host, DMAC_SWRST, REG_DMACCSR);
	while (n329_mmc_read(host, REG_DMACCSR) & DMAC_SWRST);

	/* Reset FMI */
	n329_mmc_write(host, FMI_SWRST, REG_FMICR);
	while (n329_mmc_read(host, REG_FMICR) & FMI_SWRST);

	/* Enable DMAC engine */
	n329_mmc_write(host, n329_mmc_read(host, REG_DMACCSR) | 
					DMAC_EN, REG_DMACCSR);

	/* Enable SD */
	n329_mmc_write(host, n329_mmc_read(host, REG_FMICR) | 
					FMI_SD_EN, REG_FMICR);

	/* Reset SD internal state */
	n329_mmc_write(host, SDCR_SWRST, REG_SDCR);
	while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);

	/* Enable SD card detect pin */
	n329_mmc_write(host, n329_mmc_read(host, REG_SDIER) |
					SDIER_CDSRC, REG_SDIER);

	/* Write 1 bits to clear all SDISR */
	n329_mmc_write(host, 0xFFFFFFFF, REG_SDISR);

	/* Select SD port 0 */
	n329_mmc_write(host, (n329_mmc_read(host, REG_SDCR) &
					~SDCR_SDPORT) | SDCR_SDPORT_0, REG_SDCR);

	/* SDNWR = 9 + 1 clock */
	n329_mmc_write(host, (n329_mmc_read(host, REG_SDCR) & ~SDCR_SDNWR) |
					0x09000000, REG_SDCR);

	/* SDCR_BLKCNT = 1 */
	n329_mmc_write(host, (n329_mmc_read(host, REG_SDCR) & ~SDCR_BLKCNT) |
					0x00010000, REG_SDCR);

	return 0;
}

/* Copy from sg to a dma block */
static void n329_mmc_sg_to_dma(struct n329_mmc_host *host,
					struct mmc_data *data)
{
	unsigned i, len, size;
	unsigned *dmabuf = host->buffer;

	len = data->sg_len;
	size = data->blksz * data->blocks;

	/* Loop over each scatter gather entry */
	for (i = 0; i < len; i++) {
		struct scatterlist *sg;
		unsigned amount;
		void *sgbuffer;
		char *tmpv;

		sg = &data->sg[i];

		/* Bytes in this scatter gather segment */
		amount = min(size, sg->length);

		/* Copy into the host DMA buffer */
		sgbuffer = kmap_atomic(sg_page(sg));
		tmpv = (char *) dmabuf;
		memcpy(tmpv, sgbuffer + sg->offset, amount);
		tmpv += amount;
		dmabuf = (unsigned *) tmpv;
		kunmap_atomic(sgbuffer);

		/* Adjust our counts */
		size -= amount;
		data->bytes_xfered += amount;

		/* Be sure not to transfer too much */
		if (size == 0)
			break;
	}

	/* Was request for more than can fit into scatter gather list? */
	BUG_ON(size != 0);
}

/* Copy from dma block to sg */
static void n329_mmc_dma_to_sg(struct n329_mmc_host *host,
					struct mmc_data *data)
{
	unsigned i, len, size;
	unsigned *dmabuf = host->buffer;

	len = data->sg_len;
	size = data->blksz * data->blocks;

	for (i = 0; i < size / 4; i += 4)
	{
		dev_info(mmc_dev(host->mmc), "%04x: %08x %08x %08x %08x\n", 
				i * 4, dmabuf[i], dmabuf[i + 1], dmabuf[i + 2], dmabuf[i + 3]);
	}

	/* Loop over each scatter gather entry */
	for (i = 0; i < len; i++) {
		struct scatterlist *sg;
		unsigned amount;
		void *sgbuffer;
		char *tmpv;

		sg = &data->sg[i];

		/* Bytes in this scatter gather segment */
		amount = min(size, sg->length);

		/* Copy from the host DMA buffer */
		sgbuffer = kmap_atomic(sg_page(sg));
		tmpv = (char *) dmabuf;
		memcpy(sgbuffer + sg->offset, tmpv, amount);
		tmpv += amount;
		dmabuf = (unsigned *) tmpv;
		flush_kernel_dcache_page(sg_page(sg));
		kunmap_atomic(sgbuffer);

		/* Adjust our counts */
		size -= amount;
		data->bytes_xfered += amount;

		/* Be sure not to transfer too much */
		if (size == 0)
			break;
	}

	/* Was request for more than can fit into scatter gather list? */
	BUG_ON(size != 0);
}

static int n329_mmc_setup_wp(struct n329_mmc_host *host, struct device *dev)
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

static void n329_mmc_get_response(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;

	if (mmc_resp_type(cmd) & MMC_RSP_136) {
		/* Read big-endian R2 response from DMA buffer */
		unsigned int tmp[5];
		tmp[0] = be32_to_cpu(n329_mmc_read(host, REG_FB_0));
		tmp[1] = be32_to_cpu(n329_mmc_read(host, REG_FB_0 + 4));
		tmp[2] = be32_to_cpu(n329_mmc_read(host, REG_FB_0 + 8));
		tmp[3] = be32_to_cpu(n329_mmc_read(host, REG_FB_0 + 12));
		tmp[4] = be32_to_cpu(n329_mmc_read(host, REG_FB_0 + 16));
		cmd->resp[0] = ((tmp[0] & 0x00ffffff) << 8) |
					   ((tmp[1] & 0xff000000) >> 24);
		cmd->resp[1] = ((tmp[1] & 0x00ffffff) << 8) |
					   ((tmp[2] & 0xff000000) >> 24);
		cmd->resp[2] = ((tmp[2] & 0x00ffffff) << 8) |
					   ((tmp[3] & 0xff000000) >> 24);
		cmd->resp[3] = ((tmp[3] & 0x00ffffff) << 8) |
					   ((tmp[4] & 0xff000000) >> 24);
	} else if (mmc_resp_type(cmd) & MMC_RSP_PRESENT) {
 		cmd->resp[0] = (n329_mmc_read(host, REG_SDRSP0) << 8) |
					   (n329_mmc_read(host, REG_SDRSP1) & 0xff);
		cmd->resp[1] = cmd->resp[2] = cmd->resp[3] = 0;
	}
}

static void n329_mmc_bc(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	unsigned int error = 0;
	u32 csr;

	/* Make sure DMAC engine is enabled */
	n329_mmc_write(host, n329_mmc_read(host, REG_DMACCSR) | 
					DMAC_EN, REG_DMACCSR);

	/* Make sure SD functionality is enabled */
	n329_mmc_write(host, n329_mmc_read(host, REG_FMICR) | 
					FMI_SD_EN, REG_FMICR);

	/* Read the SDCR register */
	csr = n329_mmc_read(host, REG_SDCR);

	/* Clear port, BLK_CNT, CMD_CODE, and all xx_EN fields */
	csr &= 0x9f00c080;

	/* Set the port selection bits */
	csr |= SDCR_SDPORT_0;

	/* Set command code and enable command out */
	csr |= (cmd->opcode << 8) | SDCR_CO_EN;

	/* Set the bus width bit */
	csr |= host->bus_width == 1 ? SDCR_DBW : 0;

	/* No data so disable blk transfer done interrupt */
	n329_mmc_write(host, n329_mmc_read(host, REG_SDIER) &
						~SDIER_BLKD_IEN, REG_SDIER);

	/* Set the command argument */
	n329_mmc_write(host, cmd->arg, REG_SDARG);

	/* Initiate the command */
	n329_mmc_write(host, csr, REG_SDCR);

	/* Wait for command to complete */
	while (n329_mmc_read(host, REG_SDCR) & SDCR_CO_EN) {
		/* Look for card removal */
		if (n329_mmc_read(host, REG_SDISR) & SDISR_CD_Card) {
			error = -ENODEV;
			break;
		}
	}

	/* If an error, reset the SD internal state */
	if (error) {
		n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) |
							SDCR_SWRST, REG_SDCR);
		while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);
	}

	/* Pass back any error */
	cmd->error = error;

	/* The request is done */
	mmc_request_done(host->mmc, host->mrq);

	/* Reset any request data */
	host->mrq = NULL;
}

static void n329_mmc_ac(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	unsigned int error = 0;
	u32 csr;

	/* Make sure DMAC engine is enabled */
	n329_mmc_write(host, n329_mmc_read(host, REG_DMACCSR) | 
					DMAC_EN, REG_DMACCSR);

	/* Make sure SD functionality is enabled */
	n329_mmc_write(host, n329_mmc_read(host, REG_FMICR) | 
					FMI_SD_EN, REG_FMICR);

	/* Read the SDCR register */
	csr = n329_mmc_read(host, REG_SDCR);

	/* Clear port, BLK_CNT, CMD_CODE, and all xx_EN fields */
	csr &= 0x9f00c080;

	/* Set the port selection bits */
	csr |= SDCR_SDPORT_0;

	/* Set command code and enable command out */
	csr |= (cmd->opcode << 8) | SDCR_CO_EN;

	/* Set the bus width bit */
	csr |= host->bus_width == 1 ? SDCR_DBW : 0;

	/* Do we need to capture a response? */
	if (mmc_resp_type(host->cmd) != MMC_RSP_NONE) {

		/* Set 136 bit response for R2, 48 bit response otherwise */
		if (mmc_resp_type(cmd) == MMC_RSP_R2) {
			csr |= SDCR_R2_EN;
		} else {
			csr |= SDCR_RI_EN;
		}

		/* Clear the response timeout flag */
		n329_mmc_write(host, SDISR_RITO_IF, REG_SDISR);

		/* Set the timeout for the command */
		n329_mmc_write(host, 0x1fff, REG_SDTMOUT);
	}

	/* No data so disable block transfer done interrupt */
	n329_mmc_write(host, n329_mmc_read(host, REG_SDIER) &
						~SDIER_BLKD_IEN, REG_SDIER);

	/* Set the command argument */
	n329_mmc_write(host, cmd->arg, REG_SDARG);

	/* Initiate the command */
	n329_mmc_write(host, csr, REG_SDCR);

	/* Do we need to collect a response? */
	if (mmc_resp_type(host->cmd) != MMC_RSP_NONE) {
		/* Wait for response to complete */
		while (n329_mmc_read(host, REG_SDCR) & (SDCR_R2_EN | SDCR_RI_EN)) {
			u32 sdisr = n329_mmc_read(host, REG_SDISR);
			/* Look for timeouts */
			if (sdisr & SDISR_RITO_IF) {
				error = -ETIMEDOUT;
				break;
			}
			/* Look for card removal */
			if (sdisr & SDISR_CD_Card) {
				error = -ENODEV;
				break;
			}
		}

		/* Get the response */
		n329_mmc_get_response(host);

		/* Check for CRC errors */
		if (!error && (mmc_resp_type(cmd) & MMC_RSP_CRC)) {
			if (n329_mmc_read(host, REG_SDISR) & SDISR_CRC_7) {
				cmd->error = -EIO;
			}
		}

		/* Clear the timeout register and error flags */
		n329_mmc_write(host, 0x0, REG_SDTMOUT);
		n329_mmc_write(host, SDISR_RITO_IF | SDISR_CRC_7, REG_SDISR);
	} else {
		/* Wait for command to complete */
		while (n329_mmc_read(host, REG_SDCR) & SDCR_CO_EN) {
			/* Look for card removal */
			if (n329_mmc_read(host, REG_SDISR) & SDISR_CD_Card) {
				error = -ENODEV;
				break;
			}
		}
	}

	/* If an error, reset the SD internal state */
	if (error) {
		n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) |
							SDCR_SWRST, REG_SDCR);
		while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);
	}

	/* Pass back any error */
	cmd->error = error;

	/* The request is done */
	mmc_request_done(host->mmc, host->mrq);

	/* Reset any request data */
	host->mrq = NULL;
}

static void n329_mmc_adtc(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = cmd->data;
	unsigned int error = 0;
	u32 block_count, block_length;
	u32 csr;

	dev_dbg(mmc_dev(host->mmc), "%s: cmd=%d\n", __func__, (int) cmd->opcode);

	/* Sanity check that we have data */
	if (!data) {
		dev_err(mmc_dev(host->mmc), "Invalid data\n");
		return;
	}

	/* Sanity check block size and block count */
	block_count = data->blocks;
	block_length = data->blksz;
	if (block_length > 512) {
		dev_err(mmc_dev(host->mmc), "Block length too large: %d\n",
						(int) data->blksz);
		return;
	}
	if (block_count >= 256) {
		dev_err(mmc_dev(host->mmc), "Block count too large: %d\n",
						(int) data->blocks);
		return;
	}

	/* Initialize the data transferred */
	data->bytes_xfered = 0;

	/* Make sure DMAC engine is enabled */
	n329_mmc_write(host, n329_mmc_read(host, REG_DMACCSR) | 
					DMAC_EN, REG_DMACCSR);

	/* Make sure SD functionality is enabled */
	n329_mmc_write(host, n329_mmc_read(host, REG_FMICR) | 
					FMI_SD_EN, REG_FMICR);

	/* Disable DMAC and FMI interrupts */
	n329_mmc_write(host, 0, REG_DMACIER);
	n329_mmc_write(host, 0, REG_FMIIER);

	/* Read the SDCR register */
	csr = n329_mmc_read(host, REG_SDCR);

	/* Clear port, BLK_CNT, CMD_CODE, and all xx_EN fields */
	csr &= 0x9f00c080;

	/* Set the port selection bits */
	csr |= SDCR_SDPORT_0;

	/* Set command code and enable command out */
	csr |= (cmd->opcode << 8) | SDCR_CO_EN;

	/* Set the bus width bit */
	csr |= host->bus_width == 1 ? SDCR_DBW : 0;

	/* Do we need to capture a response? */
	if (mmc_resp_type(host->cmd) != MMC_RSP_NONE) {

		/* Set 136 bit response for R2, 48 bit response otherwise */
		if (mmc_resp_type(cmd) == MMC_RSP_R2) {
			csr |= SDCR_R2_EN;
		} else {
			csr |= SDCR_RI_EN;
		}

		/* Clear the response timeout flag */
		n329_mmc_write(host, SDISR_RITO_IF, REG_SDISR);

		/* Set the timeout for the command */
		n329_mmc_write(host, 0x1fff, REG_SDTMOUT);
	}

	/* Write 1 bits to clear all SDISR */
	n329_mmc_write(host, 0xFFFFFFFF, REG_SDISR);

	/* Enable the block transfer done interrupt */
	// XXX n329_mmc_write(host, n329_mmc_read(host, REG_SDIER) |
	// XXX 					SDIER_BLKD_IEN, REG_SDIER);

	/* Set the command argument */
	n329_mmc_write(host, cmd->arg, REG_SDARG);

	/* Initiate the command */
	n329_mmc_write(host, csr, REG_SDCR);

	/* Do we need to collect a response? */
	if (mmc_resp_type(host->cmd) != MMC_RSP_NONE) {
		/* Wait for response to complete */
		while (n329_mmc_read(host, REG_SDCR) & (SDCR_R2_EN | SDCR_RI_EN)) {
			u32 sdisr = n329_mmc_read(host, REG_SDISR);
			/* Look for timeouts */
			if (sdisr & SDISR_RITO_IF) {
				error = -ETIMEDOUT;
				break;
			}
			/* Look for card removal */
			if (sdisr & SDISR_CD_Card) {
				error = -ENODEV;
				break;
			}
		}

		/* Get the response */
		n329_mmc_get_response(host);

		/* Check for CRC errors */
		if (!error && (mmc_resp_type(cmd) & MMC_RSP_CRC)) {
			if (n329_mmc_read(host, REG_SDISR) & SDISR_CRC_7) {
				cmd->error = -EIO;
			}
		}

		/* Clear the timeout register and error flags */
		n329_mmc_write(host, 0x0, REG_SDTMOUT);
		n329_mmc_write(host, SDISR_RITO_IF | SDISR_CRC_7, REG_SDISR);
	} else {
		/* Wait for command to complete */
		while (n329_mmc_read(host, REG_SDCR) & SDCR_CO_EN) {
			/* Look for card removal */
			if (n329_mmc_read(host, REG_SDISR) & SDISR_CD_Card) {
				error = -ENODEV;
				break;
			}
		}
	}

	/* Pass back any error */
	cmd->error = error;

	/* Keep track of the host data */
	WARN_ON(host->data != NULL);
	host->data = data;

	/* If an error, reset the SD internal state */
	if (error) {
		n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) |
							SDCR_SWRST, REG_SDCR);
		while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);
	} else {

		/* Read the SDCR register */
		csr = n329_mmc_read(host, REG_SDCR);

		/* Clear port, BLK_CNT, CMD_CODE, and all xx_EN fields */
		csr &= 0x9f00c080;

		/* Set command code and enable command out */
		csr |= (cmd->opcode << 8);

		/* Set the port selection bits */
		csr |= SDCR_SDPORT_0;

		/* Set the bus width bit */
		csr |= host->bus_width == 1 ? SDCR_DBW : 0;

		/* Set the DI/DO bits and configure buffer for DMA write transfer */
		if (data->flags & MMC_DATA_READ) {
			csr = csr | SDCR_DI_EN;
		} else if (data->flags & MMC_DATA_WRITE) {
			n329_mmc_sg_to_dma(host, data);
			csr = csr | SDCR_DO_EN;
		}
		n329_mmc_write(host, host->physical_address, REG_DMACSAR);

		/* Set the block length */
		n329_mmc_write(host, block_length - 1, REG_SDBLEN);

		/* Set the block count */
		csr |= block_count << 16;

		/* Write 1 bits to clear all SDISR */
		n329_mmc_write(host, 0xFFFFFFFF, REG_SDISR);

		/* Update the timeout to be suitable data transfer */
		n329_mmc_write(host, 0xfffff, REG_SDTMOUT);

		/* Initiate the transfer */
		n329_mmc_write(host, csr, REG_SDCR);

		/* Wait for data transfer complete */
		while (n329_mmc_read(host, REG_SDCR) & (SDCR_DO_EN | SDCR_DI_EN)) {
			u32 sdisr = n329_mmc_read(host, REG_SDISR);
			/* Look for CRC error */
			if (sdisr & SDISR_CRC_IF) {
				dev_info(mmc_dev(host->mmc), "%s: CRC error\n", __func__);
				error = -EIO;
				break;
			}
			/* Look for timeouts */
			if (sdisr & SDISR_DITO_IF) {
				dev_info(mmc_dev(host->mmc), "%s: timeout error\n", __func__);
				error = -ETIMEDOUT;
				break;
			}
			/* Look for card removal */
			if (sdisr & SDISR_CD_Card) {
				dev_info(mmc_dev(host->mmc), "%s: card removal error\n", __func__);
				error = -ENODEV;
				break;
			}

			schedule();
		}

		dev_info(mmc_dev(host->mmc), "DMACISR: %08x\n", 
			n329_mmc_read(host, REG_DMACISR));
		dev_info(mmc_dev(host->mmc), "FMIISR: %08x\n", 
			n329_mmc_read(host, REG_FMIISR));
		dev_info(mmc_dev(host->mmc), "error: %d\n", error);

		/* Clear the timeout register and error flags */
		n329_mmc_write(host, 0x0, REG_SDTMOUT);
		n329_mmc_write(host, SDISR_DITO_IF | SDISR_CRC_IF, REG_SDISR);

		/* Set the request error */
		data->error = error;

		if (!error) {
			/* Put the scatter data */
			if (data->flags & MMC_DATA_READ)
				n329_mmc_dma_to_sg(host, data);
		} else {
			/* Mark all data blocks as error */ 
			data->bytes_xfered = 0;

			/* Reset the SD internal state */
			n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) |
								SDCR_SWRST, REG_SDCR);
			while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);
		}
	}

	/* Reset the command data */
	host->data = NULL;

	/* Do a stop command? */
	if (!cmd->error && host->mrq->stop) 
		n329_mmc_start_cmd(host, host->mrq->stop);
	else
		mmc_request_done(host->mmc, host->mrq);

	/* Reset the request data */
	host->mrq = NULL;
}

static void n329_mmc_start_cmd(struct n329_mmc_host *host,
				  struct mmc_command *cmd)
{
	host->cmd = cmd;

	switch (mmc_cmd_type(cmd)) {
	case MMC_CMD_BC:
		/* Broadcast command (bc), no response */
		n329_mmc_bc(host);
		break;
	case MMC_CMD_BCR:
		/* Broadcast command (bcr), with response */
		n329_mmc_ac(host);
		break;
	case MMC_CMD_AC:
		/* Addressed point-to-point command (ac), no data or DAT lines */
		n329_mmc_ac(host);
		break;
	case MMC_CMD_ADTC:
		/* Addressed point-to-point command (adtc), data transfer on DAT lines */
		n329_mmc_adtc(host);
		break;
	default:
		dev_warn(mmc_dev(host->mmc),
			 "%s: unknown MMC command\n", __func__);
		break;
	}
}

static int n329_mmc_get_ro(struct mmc_host *mmc)
{
	struct n329_mmc_host *host = mmc_priv(mmc);
	int wp_value = 0;

	/* Is a write protect GPIO implemented? */
	if (host->wp_gpio > -1) {
		wp_value = gpio_get_value_cansleep(host->wp_gpio);
	}

	/* Returns 0 for read/write, 1 for read-only card */
	return wp_value ? 1 : 0;
}

static int n329_mmc_get_cd(struct mmc_host *mmc)
{
	struct n329_mmc_host *host = mmc_priv(mmc);
	int present;

	/* Make sure SD functionality is enabled */
	if (n329_mmc_read(host, REG_FMICR) | FMI_SD_EN)
		n329_mmc_write(host, n329_mmc_read(host, REG_FMICR) |
					FMI_SD_EN, REG_FMICR);

	/* Return 0 for card absent, 1 for card present */
	present = n329_mmc_read(host, REG_SDISR) & SDISR_CD_Card ? 0 : 1;

	dev_dbg(mmc_dev(host->mmc), "%s: present=%d\n", __func__, (int) present);

	return present;
}

static void n329_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct n329_mmc_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);
	host->mrq = mrq;
	n329_mmc_start_cmd(host, mrq->cmd);
}

static void n329_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct n329_mmc_host *host = mmc_priv(mmc);

	dev_dbg(mmc_dev(host->mmc), "%s: clock=%d\n", __func__, (int) ios->clock);

	if (ios->bus_width == MMC_BUS_WIDTH_8) {
		dev_err(mmc_dev(host->mmc), "Unsupported bus width: %d\n", (int) ios->bus_width);
	} else if (ios->bus_width == MMC_BUS_WIDTH_4) {
		host->bus_width = 1;
		n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) |
							SDCR_DBW, REG_SDCR);
	} else {
		host->bus_width = 0;
		n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) &
							~SDCR_DBW, REG_SDCR);
	}

	if (ios->clock) {
		/* Set the clock rate of the SD clock */
		clk_set_rate(host->sd_clk, ios->clock);

		/* Delay a bit */
		udelay(1000);

		/* Wait for 74 clocks to complete */
		n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) |
							SDCR_74CLK_OE, REG_SDCR);
		while (n329_mmc_read(host, REG_SDCR) & SDCR_74CLK_OE);
	}
}

static void n329_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct n329_mmc_host *host = mmc_priv(mmc);
	unsigned long flags;
	u32 ier;

	dev_dbg(mmc_dev(host->mmc), "%s: enable=%d\n", __func__, enable);

	spin_lock_irqsave(&host->lock, flags);

	host->sdio_irq_en = enable;

	ier = n329_mmc_read(host, REG_SDIER);

	if (enable)
		ier |= SDIER_SDIO_IEN;
	else
		ier &= ~SDIER_SDIO_IEN;

	n329_mmc_write(host, ier, REG_SDIER);

	spin_unlock_irqrestore(&host->lock, flags);
}

static const struct mmc_host_ops n329_mmc_ops = {
	.request = n329_mmc_request,
	.get_ro = n329_mmc_get_ro,
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
	{
		.compatible = "nuvoton,n32905-mmc",
		.data = (void *) N32905_SIC,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, n329_mmc_dt_ids);

static int n329_mmc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct n329_mmc_host *host;
	struct mmc_host *mmc;
	struct resource *iores;
	int irq_err, ret = 0;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_err = platform_get_irq(pdev, 0);
	if (!iores || irq_err < 0)
		return -EINVAL;

	mmc = mmc_alloc_host(sizeof(struct n329_mmc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	host->bus_width = 0;
	host->sdio_irq_en = 0;

	spin_lock_init(&host->lock);

	host->base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		goto out_mmc_free;
	}

	/* Allocate the buffer for DMA transfers */
	host->buffer = dma_alloc_coherent(&pdev->dev, MCI_BUFSIZE, 
										&host->physical_address, 
										GFP_KERNEL);
	if (!host->buffer) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Can't allocate transmit buffer\n");
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
		ret = n329_mmc_setup_wp(host, &pdev->dev);
		if (ret < 0)
			goto out_dma_free;
	} else {
		host->wp_gpio = -1;
	}

	host->sd_clk = of_clk_get(np, 0);
	host->sic_clk = of_clk_get(np, 1);
	if (IS_ERR(host->sd_clk) || IS_ERR(host->sic_clk)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "Failed to get clocks\n");
		goto out_dma_free;
	}
	clk_prepare_enable(host->sd_clk);
	clk_prepare_enable(host->sic_clk);

	ret = n329_mmc_reset(host);
	if (ret) {
		dev_err(&pdev->dev, "Failed to to reset device\n");
		goto out_clk_disable;
	}

	mmc->ops = &n329_mmc_ops;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ | MMC_CAP_NEEDS_POLL;
	mmc->f_min = 300000;
	mmc->f_max = 24000000;

	/* Set the generic mmc flags and parameters */
	ret = mmc_of_parse(mmc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to to reset device\n");
		goto out_clk_disable;
	}

	mmc->ocr_avail = MCI_VDD_AVAIL;

	mmc->max_segs = MCI_BLKATONCE;
	mmc->max_blk_size = MCI_MAXBLKSIZE;
	mmc->max_blk_count = MCI_BLKATONCE;
	mmc->max_req_size = MCI_BUFSIZE;
	mmc->max_seg_size = MCI_BUFSIZE;

	platform_set_drvdata(pdev, mmc);

	host->irq = platform_get_irq(pdev, 0);
	ret = request_irq(host->irq, n329_mmc_irq, IRQF_SHARED, 
							mmc_hostname(mmc), host);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request interrupt\n");
		goto out_clk_disable;
	}

	ret = mmc_add_host(mmc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add host\n");
		goto out_clk_disable;
	}

	return 0;

out_clk_disable:
	clk_disable_unprepare(host->sic_clk);
	clk_disable_unprepare(host->sd_clk);
out_dma_free:
	if (host->buffer)
		dma_free_coherent(&pdev->dev, MCI_BUFSIZE, host->buffer, 
							host->physical_address);
out_mmc_free:
	mmc_free_host(mmc);
	return ret;

}

static int n329_mmc_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct n329_mmc_host *host = mmc_priv(mmc);
	if (!mmc)
		return -1;

	host = mmc_priv(mmc);

	mmc_remove_host(mmc);

	if (host->buffer)
		dma_free_coherent(&pdev->dev, MCI_BUFSIZE, host->buffer, 
							host->physical_address);

	clk_disable_unprepare(host->sic_clk);
	clk_disable_unprepare(host->sd_clk);

	mmc_free_host(mmc);

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
