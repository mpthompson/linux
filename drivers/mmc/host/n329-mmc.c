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

#define MCI_VDD_AVAIL		(MMC_VDD_27_28 | MMC_VDD_28_29 | \
							 MMC_VDD_29_30 | MMC_VDD_30_31 | \
							 MMC_VDD_31_32 | MMC_VDD_32_33 | \
							 MMC_VDD_33_34)

#if 0
#define FL_SENT_COMMAND 	(1 << 0)
#define FL_SENT_STOP    	(1 << 1)

/* Driver thread command */
#define SD_EVENT_NONE       0x00000000
#define SD_EVENT_CMD_OUT    0x00000001
#define SD_EVENT_RSP_IN     0x00000010
#define SD_EVENT_RSP2_IN    0x00000100
#define SD_EVENT_CLK_KEEP0  0x00001000
#define SD_EVENT_CLK_KEEP1  0x00010000

#endif

enum n329_sic_type {
	N32905_SIC
};

struct n329_mmc_host {
	struct mmc_host	*mmc;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data	*data;

	int irq;
	int wp_gpio;
	int sdio_irq_en;
	spinlock_t lock;
	unsigned char bus_width;
	struct clk *sd_clk;
	struct clk *sic_clk;
	void __iomem *base;
};

#if 0
/* State variables and queues for SD port 0 */
static struct n329_sd_host *sd_host;
static volatile int sd_event = 0;
static volatile int sd_state = 0;
static volatile int sd_state_xfer = 0;
static volatile int sd_ri_timeout = 0;
static volatile int sd_send_cmd = 0;
static DECLARE_WAIT_QUEUE_HEAD(sd_event_wq);
static DECLARE_WAIT_QUEUE_HEAD(sd_wq);
static DECLARE_WAIT_QUEUE_HEAD(sd_wq_xfer);

extern struct semaphore fmi_sem;
extern struct semaphore dmac_sem;

static inline void n329_sd_write(struct n329_sd_host *host, u32 value, u32 addr)
{
	__raw_writel(value, host->base + addr);
}

static inline u32 n329_sd_read(struct n329_sd_host *host, u32 addr)
{
	return __raw_readl(host->base + addr);
}

/* Config SIC register to select SD port */
static int n329_sd_select_port(struct n329_sd_host *host)
{
	if (host->port == 0)
		n329_sd_write(host, (n329_sd_read(host, REG_SDCR) &
							~SDCR_SDPORT) | SDCR_SDPORT_0, REG_SDCR);
	else
		return -1;

	return 0;
}

static int n329_sd_setup_wp(struct n329_sd_host *host, struct device *dev)
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

static void n329_sd_enable(struct n329_sd_host *host)
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

	/* SDNWR = 9 + 1 clock */
	n329_sd_write(host, (n329_sd_read(host, REG_SDCR) & ~SDCR_SDNWR) |
					0x09000000, REG_SDCR);

	/* SDCR_BLKCNT = 1 */
	n329_sd_write(host, (n329_sd_read(host, REG_SDCR) & ~SDCR_BLKCNT) |
					0x00010000, REG_SDCR);

#if 0
	// set GPA0 to GPIO mode for SD port 0 write protect
	// set GPIO to GPIO mode for write protect
	w55fa93_sd_write(REG_GPAFUN, w55fa93_sd_read(REG_GPAFUN) & (~MF_GPA0));
	// set GPA0 to input mode
	w55fa93_sd_write(REG_GPIOA_OMD, w55fa93_sd_read(REG_GPIOA_OMD) & (~BIT(0)));
#endif
}

static void n329_sd_disable(struct n329_sd_host *host)
{
	/* Write to clear all SDISR */
	n329_sd_write(host, 0xffffffff, REG_SDISR);

	/* Disable SD */
	n329_sd_write(host, n329_sd_read(host, REG_FMICR) &
					(~FMI_SD_EN), REG_FMICR);
}

/* Handle transmitted data */
static void n329_sd_handle_transmitted(struct n329_sd_host *host)
{
	if (n329_sd_read(host, REG_SDISR) & SDISR_CRC_IF)
		n329_sd_write(host, SDISR_CRC_IF, REG_SDISR);

	/* Check read/busy */
	if (host->port == 0)
		n329_sd_write(host, n329_sd_read(host, REG_SDCR) |
						SDCR_CLK_KEEP, REG_SDCR);
	else
		printk("ERROR: Don't support SD port %d to transmitted data !\n", host->port);
}

/* Handle after a dma read */
static void n329_sd_post_dma_read(struct n329_sd_host *host)
{
	struct mmc_data *data;
	struct mmc_command *cmd;
	unsigned i, len, size;
	unsigned *dmabuf = host->buffer;

	cmd = host->cmd;
	if (!cmd) {
		return;
	}

	data = cmd->data;
	if (!data) {
		return;
	}

	size = data->blksz * data->blocks;
	len = data->sg_len;

	for (i = 0; i < len; i++) {
		int amount;
		unsigned *sgbuffer;
		struct scatterlist *sg;
		char *tmpv;

		sg = &data->sg[i];

		sgbuffer = kmap_atomic(sg_page(sg)) + sg->offset;
		amount = min(size, sg->length);
		size -= amount;

		tmpv = (char *) dmabuf;
		memcpy(sgbuffer, tmpv, amount);
		tmpv += amount;
		dmabuf = (unsigned *) tmpv;

		flush_kernel_dcache_page(sg_page(sg));

		kunmap_atomic(sgbuffer);

		data->bytes_xfered += amount;

		if (size == 0)
			break;
	}
}

static irqreturn_t n329_sd_irq(int irq, void *devid)
{
	struct n329_sd_host *host = devid;
	unsigned status;

	status = n329_sd_read(host, REG_SDISR);

	/* SDIO interrupt? */
	if (status & SDISR_SDIO_IF) {
		/* Clear the interrupt */
		n329_sd_write(host, SDISR_SDIO_IF, REG_SDISR);

		/* Wakeup the IRQ thread */
		mmc_signal_sdio_irq(host->mmc);
	}

	/* Block transfer done? */
	if (status & SDISR_BLKD_IF) {

		unsigned port_select = (n329_sd_read(host, REG_SDCR) &
										SDCR_SDPORT) >> 29;

		/* Ignore if not from this host device */
		if (host->port != port_select)
			return IRQ_NONE;

		/* We better know about the command and data */
		if ((host->cmd == 0) || (host->cmd->data == 0))
			return IRQ_NONE;

		/* Read or write? */
		if (host->cmd->data->flags & MMC_DATA_READ)
			n329_sd_post_dma_read(host);
		else if (host->cmd->data->flags & MMC_DATA_WRITE)
			n329_sd_handle_transmitted(host);

		/* Clear the interrupt */
		n329_sd_write(host, SDISR_BLKD_IF, REG_SDISR);

		if (host->port == 0)
		{
			sd_state_xfer = 1;
			wake_up_interruptible(&sd_wq_xfer);
		}
	}

	/* Card insert/remove detected? */
	if (status & SDISR_CD_IF) {

		if (host->port == 0) {
			/* Is the card present? */
			host->present = status & SDISR_CD_Card;

			/* If not present, reset the sd card bus to 1 bit width */
			if (!host->present)
				n329_sd_write(host, n329_sd_read(host, REG_SDCR) &
								(~SDCR_DBW), REG_SDCR);

			/* 0.5s needed because of early card detect switch firing */
			mmc_detect_change(host->mmc, msecs_to_jiffies(500));
		}

		/* Clear the interrupt */
		n329_sd_write(host, SDISR_CD_IF, REG_SDISR);
	}

	return IRQ_HANDLED;
}

/* Copy from sg to a dma block */
static inline void n329_sd_sg_to_dma(struct n329_sd_host *host,
					struct mmc_data *data)
{
	unsigned i, len, size;
	unsigned *dmabuf = host->buffer;

	len = data->sg_len;
	size = data->blksz * data->blocks;

	/* Just loop through all entries. Size might not
	 * be the entire list though so make sure that
	 * we do not transfer too much. */
	for (i = 0; i < len; i++) {
		int amount;
		char *tmpv;
		struct scatterlist *sg;
		unsigned int *sgbuffer;

		sg = &data->sg[i];

		sgbuffer = kmap_atomic(sg_page(sg)) + sg->offset;
		amount = min(size, sg->length);
		size -= amount;

		tmpv = (char *) dmabuf;
		memcpy(tmpv, sgbuffer, amount);
		tmpv += amount;
		dmabuf = (unsigned *) tmpv;

		/* XXX Should we pass back the same buffer returned above? */
		kunmap_atomic(sgbuffer);

		data->bytes_xfered += amount;

		if (size == 0)
			break;
	}

	/* Check that we didn't get a request to transfer
	 * more data than can fit into the SG list. */
	BUG_ON(size != 0);
}

/* Detect if the SD card is present or absent */
static int n329_sd_card_detect(struct n329_sd_host *host)
{
	int ret;

	if (n329_sd_read(host, REG_FMICR) != FMI_SD_EN)
		n329_sd_write(host, FMI_SD_EN, REG_FMICR);

	if (host->port == 0)
		host->present = n329_sd_read(host, REG_SDISR) & SDISR_CD_Card;

	/* Return 0 for card absent, 1 for card present */
	ret = host->present ? 0 : 1;

	return ret;
}

/* Wait for SD card to become READY */
static void n329_sd_wait_card_ready(struct n329_sd_host *host)
{
	/* Check DATA0 pin. High means READY; LOW means BUSY. */
	while (!(n329_sd_read(host, REG_SDISR) & SDISR_SD_DATA0))
	{
		/* If SD card is busy, keep waiting or exit if SD card removed */
		if (n329_sd_card_detect(host) == 0)
			break;

		/* Generate 8 clocks and wait for completion */
		n329_sd_write(host, n329_sd_read(host, REG_SDCR) |
					SDCR_8CLK_OE, REG_SDCR);
		while (n329_sd_read(host, REG_SDCR) & SDCR_8CLK_OE)
		{
			schedule();
		}
	}
}

/* Update bytes tranfered count during a write operation */
static void n329_sd_update_bytes_xferred(struct n329_sd_host *host)
{
	struct mmc_data *data;

	/* Always deal with the effective request (and not the current cmd) */
	if (host->request->cmd && host->request->cmd->error != 0)
		return;

	if (host->request->data) {
		data = host->request->data;
		if (data->flags & MMC_DATA_WRITE) {
			/* card is in IDLE mode now */
			data->bytes_xfered = data->blksz * data->blocks;
		}
	}
}

/* Send a command */
static void n329_sd_send_command(struct n329_sd_host *host,
				struct mmc_command *cmd)
{
	unsigned csr;
	unsigned volatile blocks;
	unsigned volatile block_length;
	struct mmc_data *data = cmd->data;

	host->cmd = cmd;
	if (host->port == 0)
	{
		sd_host = host;
		sd_state = 0;
		sd_state_xfer = 0;
	}

	// Get fmi_sem for whole SD command, include data read/write
	if (down_interruptible(&fmi_sem))
		return;

	if(n329_sd_read(host, REG_FMICR) != FMI_SD_EN)
		n329_sd_write(host, FMI_SD_EN, REG_FMICR);

	if (n329_sd_select_port(host) != 0)
		return;

	/* Clear BLK_CNT, CMD_CODE, and all xx_EN fields */
	csr = n329_sd_read(host, REG_SDCR) & 0xff00c080;

	/* Set command code and enable command out */
	csr = csr | (cmd->opcode << 8) | SDCR_CO_EN;

	if (host->port == 0)
		sd_event |= SD_EVENT_CMD_OUT;

	if (host->bus_mode == MMC_BUS_WIDTH_4)
		csr |= SDCR_DBW;

	/* If a response is expected then allow maximum response latancy */
	if (mmc_resp_type(cmd) != MMC_RSP_NONE) {

		/* Set 136 bit response for R2, 48 bit response otherwise */
		if (mmc_resp_type(cmd) == MMC_RSP_R2) {
			csr |= SDCR_R2_EN;
			if (host->port == 0)
				sd_event |= SD_EVENT_RSP2_IN;
		} else {
			csr |= SDCR_RI_EN;
			if (host->port == 0)
				sd_event |= SD_EVENT_RSP_IN;
		}

		n329_sd_write(host, SDISR_RITO_IF, REG_SDISR);

		if (host->port == 0)
			sd_ri_timeout = 0;

		/* Timeout for STOP CMD */
		n329_sd_write(host, 0x1fff, REG_SDTMOUT);
	}

	if (data) {
		/* Enable SD interrupt & select GPIO detect */
		n329_sd_write(host, n329_sd_read(host, REG_SDIER) |
						SDIER_BLKD_IEN, REG_SDIER);
		block_length = data->blksz;
		blocks = data->blocks;

		n329_sd_write(host, block_length - 1, REG_SDBLEN);

		if ((block_length > 512) || (blocks >= 256))
			printk("ERROR: don't support read/write 256 blocks in one SD CMD !\n");
		else
			csr = (csr & (~SDCR_BLKCNT)) | (blocks << 16);
	} else {
		/* Disable SD interrupt & select GPIO detect */
		n329_sd_write(host, n329_sd_read(host, REG_SDIER) &
						~SDIER_BLKD_IEN, REG_SDIER);
		block_length = 0;
		blocks = 0;
	}

	/* Set the arguments and send the command */
	if (data) {
		data->bytes_xfered = 0;
		host->transfer_index = 0;
		host->in_use_index = 0;
		if (data->flags & MMC_DATA_READ) {
			host->total_length = 0;
			n329_sd_write(host, host->physical_address, REG_DMACSAR);
		} else if (data->flags & MMC_DATA_WRITE) {
			if (down_interruptible(&dmac_sem))
				return;
			host->total_length = block_length * blocks;
			n329_sd_sg_to_dma(host, data);
			n329_sd_write(host, host->physical_address, REG_DMACSAR);
			csr = csr | SDCR_DO_EN;
		}
	}

	/* Set the arguments and send the command.  Send the command and then
	 * enable the PDC - not the other way round as the data sheet says */
	n329_sd_write(host, cmd->arg, REG_SDARG);
	n329_sd_write(host, csr, REG_SDCR);

	if (host->port == 0)
	{
		sd_send_cmd = 1;
		wake_up_interruptible(&sd_event_wq);
		wait_event_interruptible(sd_wq, (sd_state != 0));
	}

	if (data && (data->flags & MMC_DATA_WRITE)) {
		/* Waiting for SD card write completed and become ready. */
		n329_sd_wait_card_ready(host);

		/* SD clock won't free run any more */
		if (host->port == 0)
			n329_sd_write(host, n329_sd_read(host, REG_SDCR) &
						(~SDCR_CLK_KEEP), REG_SDCR);
		else
			printk("ERROR: Don't support SD port %d to stop free run SD clock !\n", host->port);

		/* Release dmac_sem for data writing */
		up(&dmac_sem);

		n329_sd_update_bytes_xferred(host);
	}

	/* Release fmi_sem for whole SD command, include data read/write */
	up(&fmi_sem);

	mmc_request_done(host->mmc, host->request);
}

/* Send stop command */
static void n329_sd_send_stop(struct n329_sd_host *host,
				struct mmc_command *cmd)
{
	unsigned csr;
	unsigned volatile blocks;
	unsigned volatile block_length;

	host->cmd = cmd;

	if (host->port == 0)
	{
		sd_host = host;
		sd_state = 0;
		sd_state_xfer = 0;
	}

	if (n329_sd_read(host, REG_FMICR) != FMI_SD_EN)
		n329_sd_write(host, FMI_SD_EN, REG_FMICR);

	if (n329_sd_select_port(host) != 0)
		return;

	/* Prepare SDCR register contents below */

	/* Clear BLK_CNT, CMD_CODE, and all xx_EN fields */
	csr = n329_sd_read(host, REG_SDCR) & 0xff00c080;

	/* Set command code and enable command out */
	csr = csr | (cmd->opcode << 8) | SDCR_CO_EN;

	if (host->port == 0)
		sd_event |= SD_EVENT_CMD_OUT;

	if (host->bus_mode == MMC_BUS_WIDTH_4)
		csr |= SDCR_DBW;

	/* If a response is expected then allow maximum response latancy */
	if (mmc_resp_type(cmd) != MMC_RSP_NONE) {

		/* Set 136 bit response for R2, 48 bit response otherwise */
		if (mmc_resp_type(cmd) == MMC_RSP_R2) {
			csr |= SDCR_R2_EN;
			if (host->port == 0)
				sd_event |= SD_EVENT_RSP2_IN;
		} else {
			csr |= SDCR_RI_EN;
			if (host->port == 0)
				sd_event |= SD_EVENT_RSP_IN;
		}

		n329_sd_write(host, SDISR_RITO_IF, REG_SDISR);

		if (host->port == 0)
			sd_ri_timeout = 0;

		/* Timeout for STOP CMD */
		n329_sd_write(host, 0x1fff, REG_SDTMOUT);
	}

	/* Disable SD interrupt and select GPIO detect */
	n329_sd_write(host, n329_sd_read(host, REG_SDIER) &
					(~SDIER_BLKD_IEN), REG_SDIER);
	block_length = 0;
	blocks = 0;

	/* Set the arguments and send the command */
	n329_sd_write(host, cmd->arg, REG_SDARG);
	n329_sd_write(host, csr, REG_SDCR);

	if (host->port == 0)
	{
		sd_send_cmd = 1;
		wake_up_interruptible(&sd_event_wq);
	}

	mmc_request_done(host->mmc, host->request);
}

/* Process the send request */
static void n329_sd_send_request(struct n329_sd_host *host)
{
	if (!(host->flags & FL_SENT_COMMAND)) {
		host->flags |= FL_SENT_COMMAND;
		n329_sd_send_command(host, host->request->cmd);
	} else if ((!(host->flags & FL_SENT_STOP)) && host->request->stop) {
		host->flags |= FL_SENT_STOP;
		n329_sd_send_stop(host, host->request->stop);
	} else {
		if (host->port == 0)
		{
			sd_state = 1;
			wake_up_interruptible(&sd_wq);
		}
		del_timer(&host->timer);
	}
}

/* Handle a command that has been completed */
static void n329_sd_completed_command(struct n329_sd_host *host,
				unsigned status)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = cmd->data;
	unsigned int i, j, tmp[5], err;
	unsigned char *ptr;

	err = n329_sd_read(host, REG_SDISR);

	if ((err & SDISR_RITO_IF) || (cmd->error)) {
		n329_sd_write(host, 0x0, REG_SDTMOUT);
		n329_sd_write(host, SDISR_RITO_IF, REG_SDISR);
		cmd->error = -ETIMEDOUT;
		cmd->resp[0] = cmd->resp[1] = cmd->resp[2] = cmd->resp[3] = 0;
	} else {
		if (status & SD_EVENT_RSP_IN) {
			cmd->resp[0] = (n329_sd_read(host, REG_SDRSP0) << 8) |
						   (n329_sd_read(host, REG_SDRSP1) & 0xff);
			cmd->resp[1] = cmd->resp[2] = cmd->resp[3] = 0;
		} else if (status & SD_EVENT_RSP2_IN) {
			/* Point to DMA buffer */
			ptr = (unsigned char *) REG_FB_0;
			for (i = 0, j = 0; j < 5; i += 4, j++)
				tmp[j] = (*(ptr + i) << 24) | (*(ptr + i + 1) << 16) |
						 (*(ptr + i + 2) << 8) | (*(ptr + i + 3));
			for (i = 0; i < 4; i++)
				cmd->resp[i] = ((tmp[i] & 0x00ffffff) << 8) |
							   ((tmp[i + 1] & 0xff000000) >> 24);
		}
	}

	if (!cmd->error) {
		if ((err & SDISR_CRC_7) == 0) {
			if (!(mmc_resp_type(cmd) & MMC_RSP_CRC)) {
				cmd->error = 0;
				n329_sd_write(host, SDISR_CRC_IF, REG_SDISR);
			} else
				cmd->error = -EIO;
		} else
			cmd->error = 0;

		if (data) {
			data->bytes_xfered = 0;
			host->transfer_index = 0;
			host->in_use_index = 0;
			if (data->flags & MMC_DATA_READ) {
				/* Get dmac_sem for data reading */
				if (down_interruptible(&dmac_sem))
					return;

				/* Longer timeout to read more data */
				n329_sd_write(host, 0xffffff, REG_SDTMOUT);
				n329_sd_write(host, n329_sd_read(host, REG_SDCR) |
								SDCR_DI_EN, REG_SDCR);
			}

			if (host->port == 0)
				wait_event_interruptible(sd_wq_xfer, (sd_state_xfer != 0));

			/* Release dmac_sem for data reading */
			if (data->flags & MMC_DATA_READ)
				up(&dmac_sem);
		}
	}

	n329_sd_send_request(host);
}

static int n329_sd_event_thread(void *unused)
{
	for (;;) {
		unsigned event = 0;
		unsigned completed = 0;

		/* Wait for an event on the event queue */
		wait_event_interruptible(sd_event_wq,
						(sd_event != SD_EVENT_NONE) && (sd_send_cmd));
		event = sd_event;

		/* Reset the event state */
		sd_event = SD_EVENT_NONE;
		sd_send_cmd = 0;

		if (event & SD_EVENT_CMD_OUT) {
			while (n329_sd_read(sd_host, REG_SDCR) & SDCR_CO_EN);
			completed = 1;
		}

		if (event & SD_EVENT_RSP_IN) {
			while (n329_sd_read(sd_host, REG_SDCR) & SDCR_RI_EN) {
				if (n329_sd_read(sd_host, REG_SDISR) & SDISR_RITO_IF) {
					n329_sd_write(sd_host, 0x0, REG_SDTMOUT);
					n329_sd_write(sd_host, SDISR_RITO_IF, REG_SDISR);
					sd_host->cmd->error = -ETIMEDOUT;
					break;
				}
			}
			completed = 1;
		}

		if (event & SD_EVENT_RSP2_IN) {
			while (n329_sd_read(sd_host, REG_SDCR) & SDCR_R2_EN);
			completed = 1;
		}

		if (completed)
			n329_sd_completed_command(sd_host, event);
	}

	return 0;
}

/* Reset the controller and restore most of the state */
static void n329_sd_reset_host(struct n329_sd_host *host)
{
	unsigned long flags;

	local_irq_save(flags);

	/* Enable DMAC for FMI */
	n329_sd_write(host, n329_sd_read(host, REG_DMACCSR) |
					DMAC_EN | DMAC_SWRST, REG_DMACCSR);

	/* Enable SD functionality of FMI */
	n329_sd_write(host, FMI_SD_EN, REG_FMICR);

	local_irq_restore(flags);
}

static void n329_sd_timeout_timer(unsigned long data)
{
	struct n329_sd_host *host;
	host = (struct n329_sd_host *) data;

	if (host->request) {
		dev_err(host->mmc->parent, "Timeout waiting end of packet\n");

		if (host->cmd && host->cmd->data) {
			host->cmd->data->error = -ETIMEDOUT;
		} else {
			if (host->cmd)
				host->cmd->error = -ETIMEDOUT;
			else
				host->request->cmd->error = -ETIMEDOUT;
		}

		n329_sd_reset_host(host);
		mmc_request_done(host->mmc, host->request);
	}
}

static void n329_sd_set_clock(struct n329_sd_host *host,
				unsigned long clockrate)
{
	/* Don't allow a zero rate */
	if (clockrate == 0)
		return;

	/* Set the clock rate of the SD clock */
	clk_set_rate(host->sd_clk, clockrate);
}

/* Check write protect pin */
static int n329_sd_get_ro(struct mmc_host *mmc)
{
#if 0
	struct w55fa93_sd_host *host = mmc_priv(mmc);

	// use GPA0 as the write protect pin for SD port 0
	if (host->port == 0)
	{
		if ((w55fa93_sd_read(REG_GPIOA_PIN) & BIT0) == 0)
		{
			//w55fa93_sd_debug("w55fa93_sd_get_ro(): SD port %d is write unprotected.\n", host->port);
			return 0;   // write unprotected
		}
		else
		{
			//w55fa93_sd_debug("w55fa93_sd_get_ro(): SD port %d is write protected.\n", host->port);
			return 1;   // write protected
		}
	}
#endif

	/* No write protect */
	return 0;
}

static int n329_sd_get_cd(struct mmc_host *mmc)
{
	struct n329_sd_host *host = mmc_priv(mmc);
	int ret;

	if (down_interruptible(&fmi_sem))
		return -ENOSYS;

	ret = n329_sd_card_detect(host);

	up(&fmi_sem);

	return ret;
}

static void n329_sd_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct n329_sd_host *host = mmc_priv(mmc);
	int card_present;

	host->request = mrq;
	host->flags = 0;

	/* More than 1s timeout needed with slow SD cards */
	/* mod_timer(&host->timer, jiffies +  msecs_to_jiffies(2000)); */

	if (down_interruptible(&fmi_sem))
		return;

	card_present = n329_sd_card_detect(host);

	up(&fmi_sem);

	if (!card_present) {
		host->request->cmd->error = -ENOMEDIUM;
		mmc_request_done(host->mmc, host->request);
	} else
		n329_sd_send_request(host);
}

static void n329_sd_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct n329_sd_host *host = mmc_priv(mmc);

	host->bus_mode = ios->bus_width;

	if (down_interruptible(&fmi_sem))
		return;

	/* maybe switch power to the card */
	switch (ios->power_mode) {
		case MMC_POWER_OFF:
			/* Disable the SD function in FMI controller */
			n329_sd_write(host, n329_sd_read(host, REG_FMICR) &
							~FMI_SD_EN, REG_FMICR);
			break;
		case MMC_POWER_UP:
		case MMC_POWER_ON:
			/* Enable the SD function in FMI controller */
			n329_sd_write(host, FMI_SD_EN, REG_FMICR);

			if (ios->clock == 0)
				/* Default SD clock 300000 Hz */
				n329_sd_set_clock(host, 300000);
			else
				/* ios->clock unit is Hz */
				n329_sd_set_clock(host, ios->clock);

			udelay(1000);

			n329_sd_write(host, n329_sd_read(host, REG_SDCR) |
							SDCR_74CLK_OE, REG_SDCR);

			/* Wait for 74 clocks to complete */
			while (n329_sd_read(host, REG_SDCR) & SDCR_74CLK_OE);

			break;
		default:
			WARN_ON(1);
	}

	if (ios->bus_width == MMC_BUS_WIDTH_4) {
		n329_sd_write(host, n329_sd_read(host, REG_SDCR) |
							SDCR_DBW, REG_SDCR);
	} else {
		n329_sd_write(host, n329_sd_read(host, REG_SDCR) &
							~SDCR_DBW, REG_SDCR);
	}

	up(&fmi_sem);
}

static void n329_sd_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct n329_sd_host *host = mmc_priv(mmc);

	/* First disable SDIO interrupt */
	n329_sd_write(host, n329_sd_read(host, REG_SDIER) &
						~SDIER_SDIO_IEN, REG_SDIER);

	/* Then clear the SDIO interrupt */
	n329_sd_write(host, SDISR_SDIO_IF, REG_SDISR);

	if (enable)
	{
		/* Enable Wake-Up interrupt */
		n329_sd_write(host, n329_sd_read(host, REG_SDIER) |
						SDIER_WKUP_EN, REG_SDIER);

		/* Enable SDIO interrupt */
		n329_sd_write(host, n329_sd_read(host, REG_SDIER) |
						SDIER_SDIO_IEN, REG_SDIER);
	}
	else
	{
		/* Disable Wake-Up interrupt */
		n329_sd_write(host, n329_sd_read(host, REG_SDIER) &
						~SDIER_WKUP_EN, REG_SDIER);
	}
}
#endif

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

	/* Enable DMAC */
	n329_mmc_write(host, DMAC_EN, REG_DMACCSR);

	/* Enable SD */
	n329_mmc_write(host, FMI_SD_EN, REG_FMICR);

	/* Reset SD internal state */
	n329_mmc_write(host, SDCR_SWRST, REG_SDCR);
	while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);

	/* Enable SD card detect pin */
	n329_mmc_write(host, n329_mmc_read(host, REG_SDIER) |
					SDIER_CDSRC, REG_SDIER);

	/* Write bit 1 to clear all SDISR */
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

static void n329_mmc_bc(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_request *mrq = host->mrq;
	u32 csr;

	dev_dbg(mmc_dev(host->mmc), "%s: cmd=%d\n", __func__, 
					(int) mmc_cmd_type(cmd));

	/* Make sure SD functionality is enabled */
	if (~n329_mmc_read(host, REG_FMICR) | FMI_SD_EN)
		n329_mmc_write(host, FMI_SD_EN, REG_FMICR);

	/* Reset SD internal state clearing RI, R2, DI and DO registers */
	n329_mmc_write(host, SDCR_SWRST, REG_SDCR);
	while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);

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
		/* XXX Look for card removal */
	}

	/* No error to pass back */
	cmd->error = 0;

	/* Reset the request data */
	host->mrq = NULL;

	/* The request is done */
	mmc_request_done(host->mmc, mrq);
}

static void n329_mmc_ac(struct n329_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_request *mrq = host->mrq;
	unsigned int error = 0;
	u32 csr;

	dev_dbg(mmc_dev(host->mmc), "%s: cmd=%d\n", __func__, 
					(int) mmc_cmd_type(cmd));

	/* Make sure SD functionality is enabled */
	if (~n329_mmc_read(host, REG_FMICR) | FMI_SD_EN)
		n329_mmc_write(host, FMI_SD_EN, REG_FMICR);

	/* Reset SD internal state clearing RI, R2, DI and DO registers */
	n329_mmc_write(host, SDCR_SWRST, REG_SDCR);
	while (n329_mmc_read(host, REG_SDCR) & SDCR_SWRST);

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
		// XXX n329_mmc_write(host, 0x1fff, REG_SDTMOUT);
		n329_mmc_write(host, 0xffff, REG_SDTMOUT);
	}

	/* No data so disable blk transfer done interrupt */
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
			/* Look for timeouts */
			if (n329_mmc_read(host, REG_SDISR) & SDISR_RITO_IF) {
				error = -ETIMEDOUT;
				break;
			}
			/* XXX Look for card removal */
		}

		/* Clear the timeout register and flag */
		n329_mmc_write(host, 0x0, REG_SDTMOUT);
		n329_mmc_write(host, SDISR_RITO_IF, REG_SDISR);

		/* Read the response if no error so far */
		if (!error) {
			if (mmc_resp_type(cmd) == MMC_RSP_R2) {
				/* Read from DMA buffer */
				unsigned int tmp[5];
				tmp[0] = n329_mmc_read(host, REG_FB_0);
				tmp[1] = n329_mmc_read(host, REG_FB_0 + 4);
				tmp[2] = n329_mmc_read(host, REG_FB_0 + 8);
				tmp[3] = n329_mmc_read(host, REG_FB_0 + 12);
				tmp[4] = n329_mmc_read(host, REG_FB_0 + 16);
				cmd->resp[0] = ((tmp[0] & 0x00ffffff) << 8) |
							   ((tmp[1] & 0xff000000) >> 24);
				cmd->resp[1] = ((tmp[1] & 0x00ffffff) << 8) |
							   ((tmp[2] & 0xff000000) >> 24);
				cmd->resp[2] = ((tmp[2] & 0x00ffffff) << 8) |
							   ((tmp[3] & 0xff000000) >> 24);
				cmd->resp[3] = ((tmp[3] & 0x00ffffff) << 8) |
							   ((tmp[4] & 0xff000000) >> 24);
			} else {
				cmd->resp[0] = (n329_mmc_read(host, REG_SDRSP0) << 8) |
							   (n329_mmc_read(host, REG_SDRSP1) & 0xff);
				cmd->resp[1] = cmd->resp[2] = cmd->resp[3] = 0;
			}
		}
		
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
			/* XXX Look for card removal */
		}
	}

	/* Pass back any error */
	cmd->error = error;

	/* Reset the request data */
	host->mrq = NULL;

	/* The request is done */
	mmc_request_done(host->mmc, mrq);
}

static void n329_mmc_adtc(struct n329_mmc_host *host)
{
	dev_dbg(mmc_dev(host->mmc), "%s: cmd=%d\n", __func__, (int) mmc_cmd_type(host->cmd));
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

	if (of_find_property(np, "gpios", NULL)) {
		int gpio = of_get_gpio(np, 0);
		if (gpio < 0) {
			ret = gpio;
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"Failed to get gpio flags, error: %d\n", ret);
			goto out_mmc_free;
		}
		host->wp_gpio = gpio;
		ret = n329_mmc_setup_wp(host, &pdev->dev);
		if (ret < 0)
			goto out_mmc_free;
	} else {
		host->wp_gpio = -1;
	}

	host->sd_clk = of_clk_get(np, 0);
	host->sic_clk = of_clk_get(np, 1);
	if (IS_ERR(host->sd_clk) || IS_ERR(host->sic_clk)) {
		ret = -ENODEV;
		goto out_mmc_free;
	}
	clk_prepare_enable(host->sd_clk);
	clk_prepare_enable(host->sic_clk);

	ret = n329_mmc_reset(host);
	if (ret) {
		goto out_clk_disable;
	}

	mmc->ops = &n329_mmc_ops;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ | MMC_CAP_NEEDS_POLL;
	mmc->f_min = 300000;
	mmc->f_max = 24000000;

	ret = mmc_of_parse(mmc);
	if (ret)
		goto out_clk_disable;

	mmc->ocr_avail = MCI_VDD_AVAIL;

	mmc->max_segs = MCI_BLKATONCE;
	mmc->max_blk_size = MCI_MAXBLKSIZE;
	mmc->max_blk_count = MCI_BLKATONCE;
	mmc->max_req_size = MCI_BUFSIZE;
	mmc->max_seg_size = MCI_BUFSIZE;

	platform_set_drvdata(pdev, mmc);

	host->irq = platform_get_irq(pdev, 0);
	ret = request_irq(host->irq, n329_mmc_irq, IRQF_SHARED, mmc_hostname(mmc), host);
	if (ret)
		goto out_clk_disable;

	ret = mmc_add_host(mmc);
	if (ret)
		goto out_clk_disable;

	return 0;

out_clk_disable:
	clk_disable_unprepare(host->sic_clk);
	clk_disable_unprepare(host->sd_clk);
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
