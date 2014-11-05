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
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/module.h>
#include <linux/mfd/n329-sic.h>

#define DRIVER_NAME	"n329-mmc"

#define MCI_BLKSIZE         	512
#define MCI_MAXBLKSIZE      	4096
#define MCI_BLKATONCE       	255
#define MCI_BUFSIZE         	(MCI_BLKSIZE * MCI_BLKATONCE)

#define MCI_VDD_AVAIL		(MMC_VDD_32_33 | MMC_VDD_33_34)

enum n329_sic_type {
	N32905_SIC
};

struct n329_mmc_host {
	struct mmc_host	*mmc;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data	*data;
	struct device *dev;

	dma_addr_t physical_address;
	unsigned *buffer;
	unsigned total_length;
	unsigned xfer_error;
	wait_queue_head_t dma_wait;

	int irq;
	int wp_gpio;
	int sdio_irq_en;
	spinlock_t lock;
	unsigned char bus_width;
	struct clk *sd_clk;
	struct clk *sic_clk;
};

extern struct semaphore  fmi_sem;
extern struct semaphore  dmac_sem;

extern unsigned long n329_clocks_config_sd(unsigned long rate);

static void n329_mmc_start_cmd(struct n329_mmc_host *host,
			struct mmc_command *cmd);

static inline u32 n329_mmc_read(struct n329_mmc_host *host, u32 addr)
{
	return n329_sic_read(host->dev->parent, addr);
}

static inline void n329_mmc_write(struct n329_mmc_host *host, 
			u32 value, u32 addr)
{
	return n329_sic_write(host->dev->parent, value, addr);
}

static irqreturn_t n329_mmc_irq(int irq, void *devid)
{
	struct n329_mmc_host *host = devid;
	unsigned wakeup = 0;
	unsigned sdisr;

	sdisr = n329_mmc_read(host, REG_SDISR);

	/* SDIO interrupt? */
	if (sdisr & SDISR_SDIO_IF) {
		/* Notify the MMC core of the interrupt */
		mmc_signal_sdio_irq(host->mmc);

		/* Clear the interrupt */
		n329_mmc_write(host, SDISR_SDIO_IF, REG_SDISR);
	}

	/* Block transfer done? */
	if (sdisr & SDISR_BLKD_IF) {
		/* Clear the interrupt */
		n329_mmc_write(host, SDISR_BLKD_IF, REG_SDISR);

		/* Wakeup if we are transferring data */
		wakeup = host->data != NULL ? 1 : 0;
	}

	/* Data transfer timeout? */
	if (sdisr & SDISR_DITO_IF) {
		/* Clear the interrupt */
		n329_mmc_write(host, SDISR_DITO_IF, REG_SDISR);

		/* Set a transfer error */
		host->xfer_error = -ETIMEDOUT;

		/* Wakeup if we are transferring data */
		wakeup = host->data != NULL ? 1 : 0;
	}

	/* CRC error during transfer? */
	if (sdisr & SDISR_CRC_IF) {
		/* Clear the interrupt */
		n329_mmc_write(host, SDISR_CRC_IF, REG_SDISR);

		/* Set a transfer error */
		host->xfer_error = -EIO;

		/* Wakeup if we are transferring data */
		wakeup = host->data != NULL ? 1 : 0;
	}

	/* Card insert/remove detected? */
	if (sdisr & SDISR_CD_IF) {
		/* Clear the interrupt */
		n329_mmc_write(host, SDISR_CD_IF, REG_SDISR);

		/* Was the card removed */
		if (sdisr & SDISR_CD_Card) {
			/* Set a transfer error */
			host->xfer_error = -ENODEV;

			/* Wakeup if we are transferring data */
			wakeup = host->data != NULL ? 1 : 0;
		}
	}

	/* Wakeup the transfer? */
	if (wakeup)
		wake_up_interruptible(&host->dma_wait);

	return IRQ_HANDLED;
}

static int n329_mmc_reset(struct n329_mmc_host *host)
{
	unsigned error;

	/* Hold the FMI semaphore for the following operations */
    	error = down_interruptible(&fmi_sem);
	if (error)
	        return error;

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
	n329_mmc_write(host, 0xffffffff, REG_SDISR);

	/* Select SD port 0 */
	n329_mmc_write(host, (n329_mmc_read(host, REG_SDCR) &
				~SDCR_SDPORT) | SDCR_SDPORT_0, REG_SDCR);

	/* SDNWR = 9 + 1 clock */
	n329_mmc_write(host, (n329_mmc_read(host, REG_SDCR) & ~SDCR_SDNWR) |
				0x09000000, REG_SDCR);

	/* SDCR_BLKCNT = 1 */
	n329_mmc_write(host, (n329_mmc_read(host, REG_SDCR) & ~SDCR_BLKCNT) |
				0x00010000, REG_SDCR);

	/* Release the FMI semaphore */
	up(&fmi_sem);

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

static unsigned n329_mmc_do_command(struct n329_mmc_host *host)
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
	n329_mmc_write(host, 0xffffffff, REG_SDISR);

	/* Set the command argument */
	n329_mmc_write(host, cmd->arg, REG_SDARG);

	/* Initiate the command */
	n329_mmc_write(host, csr, REG_SDCR);

	/* Do we need to collect a response? */
	if (mmc_resp_type(host->cmd) != MMC_RSP_NONE) {
		/* Wait for response to complete */
		while (n329_mmc_read(host, REG_SDCR) &
				(SDCR_R2_EN | SDCR_RI_EN)) {
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

			/* Voluntarily relinquish the CPU while waiting */
			schedule();
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

			/* Voluntarily relinquish the CPU while waiting */
			schedule();
		}
	}

	if (error) {
		/* If an error, reset the SD internal state */
		n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) |
							SDCR_SWRST, REG_SDCR);
		while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);
	}

	return error;
}

static unsigned n329_mmc_do_transfer(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = cmd->data;
	unsigned error = 0;
	u32 block_count, block_length;
	u32 csr;

	/* Sanity check that we have data */
	if (!data) {
		dev_err(mmc_dev(host->mmc), "Invalid data\n");
		return -EINVAL;
	}

	/* Sanity check block size and block count */
	block_count = data->blocks;
	block_length = data->blksz;
	if (block_length > 512) {
		dev_err(mmc_dev(host->mmc), "Block length too large: %d\n",
						(int) data->blksz);
		return -EINVAL;
	}
	if (block_count >= 256) {
		dev_err(mmc_dev(host->mmc), "Block count too large: %d\n",
						(int) data->blocks);
		return -EINVAL;
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

	/* Keep track of the host data */
	WARN_ON(host->data != NULL);
	host->data = data;

	/* Read the SDCR register */
	csr = n329_mmc_read(host, REG_SDCR);

	/* Clear port, BLK_CNT, CMD_CODE, and all xx_EN fields */
	csr &= 0x9f00c080;

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

	/* Update the timeout to be suitable data transfer */
	n329_mmc_write(host, 0xfffff, REG_SDTMOUT);

	/* Write 1 bits to clear all SDISR */
	n329_mmc_write(host, 0xffffffff, REG_SDISR);

	/* Enable the interrupt conditions that end a transfer */
	n329_mmc_write(host, n329_mmc_read(host, REG_SDIER) |
					SDIER_DITO_IEN | SDIER_CD_IEN |
					SDIER_CRC_IEN | SDIER_BLKD_IEN,
					REG_SDIER);

	/* Clear any transfer error */
	host->xfer_error = 0;

	/* Initiate the transfer */
	n329_mmc_write(host, csr, REG_SDCR);

	/* Wait for data transfer complete */
	wait_event_interruptible(host->dma_wait,
				((n329_mmc_read(host, REG_SDCR) &
				(SDCR_DO_EN | SDCR_DI_EN)) == 0));

	/* Collect any error that occurred */
	error = host->xfer_error;

	/* Disable the interrupt conditions that end a transfer */
	n329_mmc_write(host, n329_mmc_read(host, REG_SDIER) &
				~(SDIER_DITO_IEN | SDIER_CD_IEN |
				SDIER_CRC_IEN | SDIER_BLKD_IEN),
				REG_SDIER);

	/* Clear the timeout register and error flags */
	n329_mmc_write(host, 0x0, REG_SDTMOUT);

	if (!error) {
		/* Transfer from the DMA buffer to the scatter gather segs */
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

	/* Reset the command data */
	host->data = NULL;

	return error;
}

static void n329_mmc_bc(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;

	/* Hold the FMI semaphore for the whole SD command */
    	cmd->error = down_interruptible(&fmi_sem);
	if (cmd->error)
	        return;

	/* Perform a command which should have no response */
	cmd->error = n329_mmc_do_command(host);

	/* Release the FMI semaphore */
	up(&fmi_sem);

	/* The request is done */
	mmc_request_done(host->mmc, host->mrq);

	/* Reset any request data */
	host->mrq = NULL;
}

static void n329_mmc_ac(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;

	/* Hold the FMI semaphore for the whole SD command */
    	cmd->error = down_interruptible(&fmi_sem);
	if (cmd->error)
	        return;

	/* Perform a command which should include a response */
	cmd->error = n329_mmc_do_command(host);

	/* Release the FMI semaphore */
	up(&fmi_sem);

	/* The request is done */
	mmc_request_done(host->mmc, host->mrq);

	/* Reset any request data */
	host->mrq = NULL;
}

static void n329_mmc_adtc(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = cmd->data;

	/* Sanity check that we have data */
	if (!data) {
		dev_err(mmc_dev(host->mmc), "Invalid data\n");
		cmd->error = -EINVAL;
		return;
	}

	/* Hold the FMI semaphore for the whole SD command */
    	cmd->error = down_interruptible(&fmi_sem);
	if (cmd->error)
	        return;

	/* Initialize the data transferred */
	data->bytes_xfered = 0;

	/* Perform a command which should include a response */
	cmd->error = n329_mmc_do_command(host);

	/* Perform the transfer of data */
	if (!cmd->error) {
		data->error = n329_mmc_do_transfer(host);
	}

	/* Release the FMI semaphore */
	up(&fmi_sem);

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
		/* Addressed point-to-point command (ac), no data */
		n329_mmc_ac(host);
		break;
	case MMC_CMD_ADTC:
		/* Addressed point-to-point command (adtc), data transfer */
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

	/* Hold the FMI semaphore for the whole SD command */
    	if (down_interruptible(&fmi_sem))
	        return 0;

	/* Make sure SD functionality is enabled */
	if (n329_mmc_read(host, REG_FMICR) | FMI_SD_EN)
		n329_mmc_write(host, n329_mmc_read(host, REG_FMICR) |
					FMI_SD_EN, REG_FMICR);

	/* Return 0 for card absent, 1 for card present */
	present = n329_mmc_read(host, REG_SDISR) & SDISR_CD_Card ? 0 : 1;

	dev_dbg(mmc_dev(host->mmc), "%s: present=%d\n", __func__,
				(int) present);

	up(&fmi_sem);

	return present;
}

static void n329_mmc_request(struct mmc_host *mmc,
			struct mmc_request *mrq)
{
	struct n329_mmc_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);
	host->mrq = mrq;
	n329_mmc_start_cmd(host, mrq->cmd);
}

static void n329_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct n329_mmc_host *host = mmc_priv(mmc);

	dev_dbg(mmc_dev(host->mmc), "%s: clock=%d\n", __func__,
			(int) ios->clock);

	if (down_interruptible(&fmi_sem))
		return;

	if (ios->bus_width == MMC_BUS_WIDTH_8) {
		dev_err(mmc_dev(host->mmc), "Unsupported bus width: %d\n",
			(int) ios->bus_width);
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
		n329_clocks_config_sd(ios->clock);

		/* Delay a bit */
		udelay(1000);

		/* Wait for 74 clocks to complete */
		n329_mmc_write(host, n329_mmc_read(host, REG_SDCR) |
						SDCR_74CLK_OE, REG_SDCR);
		while (n329_mmc_read(host, REG_SDCR) & SDCR_74CLK_OE);
	}

	up(&fmi_sem);
}

static void n329_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct n329_mmc_host *host = mmc_priv(mmc);
	unsigned long flags;
	u32 ier;

	if (down_interruptible(&fmi_sem))
		return;

	spin_lock_irqsave(&host->lock, flags);

	dev_dbg(mmc_dev(host->mmc), "%s: enable=%d\n", __func__, enable);

	host->sdio_irq_en = enable;

	ier = n329_mmc_read(host, REG_SDIER);

	if (enable)
		ier |= SDIER_SDIO_IEN;
	else
		ier &= ~SDIER_SDIO_IEN;

	n329_mmc_write(host, ier, REG_SDIER);

	spin_unlock_irqrestore(&host->lock, flags);

	up(&fmi_sem);
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
	int ret = 0;

	mmc = mmc_alloc_host(sizeof(struct n329_mmc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;
	host->dev = &pdev->dev;

	host->bus_width = 0;
	host->sdio_irq_en = 0;

	spin_lock_init(&host->lock);

	/* Allocate the buffer for DMA transfers */
	host->buffer = dma_alloc_coherent(&pdev->dev, MCI_BUFSIZE,
				&host->physical_address,
				GFP_KERNEL);
	if (!host->buffer) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Can't allocate transmit buffer\n");
		goto out_mmc_free;
	}
	init_waitqueue_head(&host->dma_wait);

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
