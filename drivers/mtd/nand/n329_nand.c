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

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_mtd.h>
#include <linux/mfd/n329-sic.h>

#include <asm/mach/flash.h>

#define DRIVER_NAME "n329-nand"

#if 0
#define BITS(start,end)		((0xffffffff >> (31 - start)) & (0xffffffff << end))

/* Serial Interface Controller (SIC) Registers */
#define REG_FB_0		(0x000)		/* Shared Buffer (FIFO) */

#define	REG_DMACCSR		(0x400)		/* DMAC Control and Status Register */
	#define	FMI_BUSY	BIT(9)		/* FMI DMA transfer is in progress */
	#define SG_EN		BIT(3)		/* DMAC Scatter-gather function enable */
	#define DMAC_SWRST	BIT(1)		/* DMAC software reset enable */
	#define DMAC_EN		BIT(0)		/* DMAC enable */

#define REG_DMACSAR		(0x408)		/* DMAC Transfer Starting Address Register */
#define REG_DMACBCR		(0x40C)		/* DMAC Transfer Byte Count Register */
#define REG_DMACIER		(0x410)		/* DMAC Interrupt Enable Register */
	#define	WEOT_IE		BIT(1)		/* Wrong EOT encounterred interrupt enable */
	#define TABORT_IE	BIT(0)		/* DMA R/W target abort interrupt enable */

#define REG_DMACISR		(0x414)		/* DMAC Interrupt Status Register */
	#define	WEOT_IF		BIT(1)		/* Wrong EOT encounterred interrupt flag */
	#define TABORT_IF	BIT(0)		/* DMA R/W target abort interrupt flag */

/* Flash Memory Card Interface Registers */
#define REG_FMICR		(0x800)		/* FMI Control Register */
	#define	FMI_SM_EN	BIT(3)		/* Enable FMI SM function */
	#define FMI_SD_EN	BIT(1)		/* Enable FMI SD function */
	#define FMI_SWRST	BIT(0)		/* Enable FMI software reset */

#define REG_FMIIER		(0x804)   	/* FMI DMA transfer starting address register */
	#define	FMI_DAT_IE	BIT(0)		/* Enable DMAC READ/WRITE targe abort interrupt generation */

#define REG_FMIISR		(0x808)   	/* FMI DMA byte count register */
	#define	FMI_DAT_IF	BIT(0)		/* DMAC READ/WRITE targe abort interrupt flag register */

/* Smark-Media NAND Registers */
#define REG_SMCSR		(0x8A0)   	/* NAND Flash Control and Status Register */
	#define SMCR_CS1	BIT(26)		/* SM chip select */
	#define SMCR_CS0	BIT(25)		/* SM chip select */
	#define SMCR_CS		BIT(25)		/* SM chip select */
	#define SMCR_ECC_EN	BIT(23)		/* SM chip select */
	#define SMCR_BCH_TSEL 	BITS(22,19)	/* BCH T4/8/12/15 selection */
	  #define BCH_T15 	BIT(22)		/* BCH T15 selected */
	  #define BCH_T12 	BIT(21)		/* BCH T12 selected */
	  #define BCH_T8	BIT(20)		/* BCH T8 selected */
	  #define BCH_T4	BIT(19)		/* BCH T4 selected */
	#define SMCR_PSIZE	BITS(17,16)	/* SM page size selection */
	  #define PSIZE_8K 	BIT(17)+BIT(16)	/* Page size 8K selected */
	  #define PSIZE_4K 	BIT(17)		/* Page size 4K selected */
	  #define PSIZE_2K 	BIT(16)		/* Page size 2K selected */
	  #define PSIZE_512 	0		/* Page size 512 selected */
	#define SMCR_SRAM_INIT	BIT(9)		/* SM RA0_RA1 initial bit (to 0xFFFF_FFFF) */
	#define SMCR_ECC_3B_PR	BIT(8)		/* ECC protect redundant 3 bytes */
	#define SMCR_ECC_CHK	BIT(7)		/* ECC parity check enable bit during read page */
	#define SMCR_REDUN_WEN 	BIT(4)		/* Redundant auto write enable */
	#define SMCR_REDUN_REN 	BIT(3)		/* Redundant read enable */
	#define SMCR_DWR_EN 	BIT(2)		/* DMA write data enable */
	#define SMCR_DRD_EN 	BIT(1)		/* DMA read data enable */
	#define SMCR_SM_SWRST 	BIT(0)		/* SM software reset */

#define REG_SMTCR		(0x8A4)   	/* NAND Flash Timing Control Register */

#define REG_SMIER		(0x0A8)   	/* NAND Flash Interrupt Control Register */
	#define	SMIER_RB1_IE	BIT(11)		/* RB1 pin rising-edge detection interrupt enable */
	#define	SMIER_RB0_IE	BIT(10)		/* RB0 pin rising-edge detection interrupt enable */
	#define	SMIER_RB_IE	BIT(10)		/* RB0 pin rising-edge detection interrupt enable */
	#define SMIER_ECC_FD_IE BIT(2)		/* ECC field error check interrupt enable */
	#define SMIER_DMA_IE	BIT(0)		/* DMA RW data complete interrupr enable */

#define REG_SMISR		(0x8AC)   	/* NAND Flash Interrupt Status Register */
	#define	SMISR_RB1	BIT(19)		/* RB1 pin status */
	#define	SMISR_RB0 	BIT(18)		/* RB0 pin status */
	#define	SMISR_RB 	BIT(18)		/* RB pin status */
	#define	SMISR_RB1_IF	BIT(11)		/* RB pin rising-edge detection interrupt flag */
	#define	SMISR_RB0_IF	BIT(10)		/* RB pin rising-edge detection interrupt flag */
	#define SMISR_ECC_FD_IF	BIT(2)		/* ECC field error check interrupt flag */
	#define SMISR_DMA_IF	BIT(0)		/* DMA RW data complete interrupr flag */

#define REG_SMCMD		(0x8B0)   	/* NAND Flash Command Port Register */

#define REG_SMADDR		(0x8B4)   	/* NAND Flash Address Port Register */
	#define	EOA_SM		BIT(31)		/* End of SM address for last SM address */

#define REG_SMDATA		(0x8B8)   	/* NAND Flash Data Port Register */

#define REG_SMREAREA_CTL 	(0x8BC)  	/* NAND Flash redundnat area control register */
	#define SMRE_MECC 	BITS(31,16)	/* Mask ECC parity code to NAND during Write Page Data to NAND by DMAC */
	#define	SMRE_REA128_EXT	BITS(8,0)	/* Redundant area enabled byte number */
#endif

/* #define OPT_NANDCARD_DETECT */
#ifdef OPT_NANDCARD_DETECT
	/* External NAND card */
	#define NANDCARD_NAND
#else
	/* On-board NAND card */
	#define ONBOARD_NAND
#endif

struct n329_nand_host {
	struct mtd_info mtd;
	struct nand_chip nand;
	struct device *dev;

	struct clk *sic_clk;
	struct clk *nand_clk;
	void __iomem *base;
	spinlock_t lock;

	struct mtd_partition *parts;    /* partition table */
 	int nr_parts;                   /* size of parts */
};

extern struct semaphore  fmi_sem;

static void __iomem *sic_base;

static inline void n329_nand_writel(u32 value, u32 addr)
{
	__raw_writel(value, sic_base + addr);
}

static inline u32 n329_nand_readl(u32 addr)
{
	return __raw_readl(sic_base + addr);
}

static unsigned char n329_nand_read_byte(struct mtd_info *mtd)
{
	struct n329_nand_host *host;
	unsigned char ret;

	host = container_of(mtd, struct n329_nand_host, mtd);

	if (down_interruptible(&fmi_sem))
	{
		dev_err(host->dev, "%s: semaphore error\n", __func__);
		return -1;
	}

#if defined(NANDCARD_NAND)
	/* CS1 is selected */
	n329_nand_writel((n329_nand_readl(REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_readl(REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_writel(FMI_SM_EN, REG_FMICR);

	ret = (unsigned char) n329_nand_readl(REG_SMDATA);

	up(&fmi_sem);

	return ret;
}

static void n329_nand_read_buf(struct mtd_info *mtd,
				 unsigned char *buf, int len)
{
	struct n329_nand_host *host;
	int i;

	host = container_of(mtd, struct n329_nand_host, mtd);

	if (down_interruptible(&fmi_sem))
	{
		dev_err(host->dev, "%s: semaphore error\n", __func__);
		return;
	}

#if defined(NANDCARD_NAND)
	/* CS1 is selected */
	n329_nand_writel((n329_nand_readl(REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_readl(REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_writel(FMI_SM_EN, REG_FMICR);

	for (i = 0; i < len; i++)
		buf[i] = (unsigned char) n329_nand_readl(REG_SMDATA);

	up(&fmi_sem);
}

static void n329_nand_write_buf(struct mtd_info *mtd,
				  const unsigned char *buf, int len)
{
	struct n329_nand_host *host;
	int i;

	host = container_of(mtd, struct n329_nand_host, mtd);

	if (down_interruptible(&fmi_sem))
	{
		dev_err(host->dev, "%s: semaphore error\n", __func__);
		return;
	}

#if defined(NANDCARD_NAND)
	/* CS1 is selected */
	n329_nand_writel((n329_nand_readl(REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_readl(REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_writel(FMI_SM_EN, REG_FMICR);

	for (i = 0; i < len; i++)
		n329_nand_writel(buf[i], REG_SMDATA);

	up(&fmi_sem);
}

static void n329_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct n329_nand_host *host;

	host = container_of(mtd, struct n329_nand_host, mtd);

	if (down_interruptible(&fmi_sem))
	{
		dev_err(host->dev, "%s: semaphore error\n", __func__);
		return;
	}

#if defined(NANDCARD_NAND)
	/* CS1 is selected */
	n329_nand_writel((n329_nand_readl(REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_readl(REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_writel(FMI_SM_EN, REG_FMICR);

  	up(&fmi_sem);
}

static int n329_nand_check_ready_busy(struct n329_nand_host *host)
{
	unsigned int val;

	spin_lock(&host->lock);

#if defined(ONBOARD_NAND)
	val = n329_nand_readl(REG_SMISR) & SMISR_RB0;
#endif

#if defined(NANDCARD_NAND)
	val = n329_nand_readl(REG_SMISR) & SMISR_RB1;
#endif

	spin_unlock(&host->lock);

	return val;
}

static int n329_nand_devready(struct mtd_info *mtd)
{
	struct n329_nand_host *host;
	int ready;

	host = container_of(mtd, struct n329_nand_host, mtd);

	if (down_interruptible(&fmi_sem)) {
		dev_err(host->dev, "%s: semaphore error\n", __func__);
		return -1;
	}

#if defined(NANDCARD_NAND)
	/* CS1 is selected */
	n329_nand_writel((n329_nand_readl(REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_readl(REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_writel(FMI_SM_EN, REG_FMICR);

	ready = n329_nand_check_ready_busy(host) ? 1 : 0;

	up(&fmi_sem);

	return ready;
}

int n329_nand_wait_ready_busy(struct n329_nand_host *host)
{
#if defined(ONBOARD_NAND)
	while(1) {
		if (n329_nand_readl(REG_SMISR) & SMISR_RB0_IF) {
			n329_nand_writel(SMISR_RB0_IF, REG_SMISR);
			return 1;
		}
	}
#endif

#if defined(NANDCARD_NAND)
	while(1) {
		if (n329_nand_readl(REG_SMISR) & SMISR_RB1_IF) {
			n329_nand_writel(SMISR_RB1_IF, REG_SMISR);
			return 1;
		}
	}
#endif

	return 0;
}

static void n329_nand_reset(struct n329_nand_host *host)
{
	unsigned int volatile i;

	n329_nand_writel(0xff, REG_SMCMD);

	for (i = 100; i > 0; i--);

	while(!n329_nand_check_ready_busy(host));
}

static void n329_nand_command(struct mtd_info *mtd, unsigned command,
		int column, int page_addr)
{
	register struct nand_chip *chip = mtd->priv;
	struct n329_nand_host *host;

	host = container_of(mtd, struct n329_nand_host, mtd);

	if (down_interruptible(&fmi_sem)) {
		dev_err(host->dev, "%s: semaphore error\n", __func__);
		return;
	}

#if defined(NANDCARD_NAND)
	/* CS1 is selected */
	n329_nand_writel((n329_nand_readl(REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_readl(REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_writel(FMI_SM_EN, REG_FMICR);

	/* Emulate NAND_CMD_READOOB */
	if (command == NAND_CMD_READOOB) {

		column += mtd->writesize;

		if (mtd->writesize == 0x200) {
			column = 0;
		}
		else {
			command = NAND_CMD_READ0;
		}
	}

	if (command == NAND_CMD_SEQIN)
	{
		unsigned readcommand;

		if (mtd->writesize == 0x200)
		{
			if (column < 0x100)
  			{
				readcommand = NAND_CMD_READ0;
			}
			else if (column >= 0x200)
			{
  				column -= 0x200;
				readcommand = NAND_CMD_READOOB;
			}
			else
			{
				column -= 0x100;
				readcommand = NAND_CMD_READ1;
			}

			n329_nand_writel(readcommand, REG_SMCMD);
		}
	}

	n329_nand_writel(command, REG_SMCMD);

	if (column != -1 || page_addr != -1) {

#if defined(ONBOARD_NAND)
		/* Clear the R/B flag */
	  	n329_nand_writel(SMISR_RB0_IF, REG_SMISR);
#endif

#if defined(NANDCARD_NAND)
		/* Clear the R/B flag */
	  	n329_nand_writel(SMISR_RB1_IF, REG_SMISR);
#endif

		if (column != -1) {
			/* Adjust columns for 16 bit buswidth */
			if (chip->options & NAND_BUSWIDTH_16)
				column >>= 1;

			n329_nand_writel((unsigned char) column, REG_SMADDR);

			/* Handle 2K page */
			if (mtd->writesize == 0x800)
			  	n329_nand_writel((column >> 8) & 0x0f, 
			  			REG_SMADDR);
		}

		if (page_addr != -1) {
			n329_nand_writel((page_addr & 0xff), REG_SMADDR);

			/* One more address cycle for devices > 128MiB */
			if (chip->chipsize >= (64 << 20))
			{
				n329_nand_writel((page_addr >> 8 ) & 0xff,
						REG_SMADDR);
				n329_nand_writel(((page_addr >> 16) & 0xff) |
						0x80000000,
						REG_SMADDR);
			} else {
				n329_nand_writel(((page_addr >> 8) & 0xff) | 
						0x80000000,
						REG_SMADDR);
			}
		} else {
			n329_nand_writel(((page_addr >> 8) & 0xff) |
					0x80000000, REG_SMADDR);
		}
	}

	/*
	 * Program and erase have their own busy handlers
	 * status, sequential in, and deplete1 need no delay */
	switch (command) {
	case NAND_CMD_PAGEPROG:
		if (!n329_nand_wait_ready_busy(host))
			printk("check RB error\n");
		break;

	case NAND_CMD_CACHEDPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_RNDIN:
	case NAND_CMD_STATUS:
		break;

	case NAND_CMD_RESET:
		if (chip->dev_ready)
			break;
		udelay(chip->chip_delay);
		n329_nand_reset(host);
		break;

	 case NAND_CMD_RNDOUT:
		if (mtd->writesize == 0x800)
			n329_nand_writel(NAND_CMD_RNDOUTSTART, REG_SMCMD);
		break;

	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
		/* Begin command latch cycle */
		if(mtd->writesize == 0x800) {
#if defined(ONBOARD_NAND)
		  	n329_nand_writel(0x400, REG_SMISR);
#endif
#if defined(NANDCARD_NAND)
		  	n329_nand_writel(0x800, REG_SMISR);
#endif
			n329_nand_writel(NAND_CMD_READSTART, REG_SMCMD);

			if (!n329_nand_wait_ready_busy(host))
				printk("check RB error\n");
		} else if(mtd->writesize == 0x200) {
			if (!n329_nand_wait_ready_busy(host))
				printk("check RB error\n");
		}

		/* This applies to read commands */
		if (!chip->dev_ready)
			udelay(chip->chip_delay);

		break;

	case NAND_CMD_READOOB:
		if (mtd->writesize == 0x800) {
			if (!n329_nand_wait_ready_busy(host))
				printk("check RB error\n");
		} else if(mtd->writesize == 0x200) {
			if (!n329_nand_wait_ready_busy(host))
				printk("check RB error\n");
		}

		if (!chip->dev_ready)
			udelay(chip->chip_delay);

		break;

	default:
		/*
		 * If we don't have access to the busy pin, we apply the given
		 * command delay */
		if (!chip->dev_ready) {
			udelay (chip->chip_delay);
		}
		break;
	}

	up(&fmi_sem);

	/* Apply chip short delay always to ensure that we do wait tWB in
	 * any case on any machine. */
	ndelay (100);
}

static void n329_nand_enable(struct n329_nand_host *host)
{
	if (down_interruptible(&fmi_sem)) {
		dev_err(host->dev, "%s: semaphore error\n", __func__);
		return;
	}
	
	spin_lock(&host->lock);
	
	n329_nand_writel(n329_nand_readl(REG_FMICR) | FMI_SM_EN, REG_FMICR);

	n329_nand_writel(0x3050b, REG_SMTCR);
  
#if defined(ONBOARD_NAND)		
	/* CS0 is selected */
	n329_nand_writel((n329_nand_readl(REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS1, REG_SMCSR);
#endif		
#if defined(NANDCARD_NAND)
	/* CS1 is selected */
	n329_nand_writel((n329_nand_readl(REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif		

	spin_unlock(&host->lock);

	up(&fmi_sem);
}

static const char * const part_probes[] = {
	"cmdlinepart", "RedBoot", "ofpart", NULL };

static int n329_nand_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct n329_nand_host *host;
	struct resource *iores;
	int ret = 0;

	/* Get the memory resources for this device */
	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -EINVAL;

	/* Allocate memory for MTD device structure and private data */
	host = devm_kzalloc(&pdev->dev, sizeof(struct n329_nand_host),
			GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = &pdev->dev;

	/* Structures must be linked */
	chip = &host->nand;
	mtd = &host->mtd;
	mtd->priv = chip;
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = &pdev->dev;
	mtd->name = DRIVER_NAME;

	spin_lock_init(&host->lock);

	sic_base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(sic_base)) {
		ret = PTR_ERR(sic_base);
		goto out_mem_free;
	}
	host->base = sic_base;
	
	host->nand_clk = of_clk_get(np, 0);
	host->sic_clk = of_clk_get(np, 1);
	if (IS_ERR(host->nand_clk) || IS_ERR(host->sic_clk)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "%s: Failed to get clocks\n", __func__);
		goto out_mem_free;
	}
	clk_prepare_enable(host->nand_clk);
	clk_prepare_enable(host->sic_clk);

	chip->cmdfunc = n329_nand_command;
	chip->dev_ready = n329_nand_devready;
	chip->read_byte = n329_nand_read_byte;
	chip->write_buf = n329_nand_write_buf;
	chip->read_buf = n329_nand_read_buf;
	chip->select_chip = n329_nand_select_chip;
	chip->chip_delay = 50;
	chip->options = 0;
	chip->ecc.mode = NAND_ECC_SOFT;

	n329_nand_enable(host);

	if (nand_scan(&host->mtd, 1)) {
		ret = -ENXIO;
		goto out_clk_disable;
	}

	/* Register the partitions */
	mtd_device_parse_register(mtd, part_probes,
			&(struct mtd_part_parser_data) {
				.of_node = pdev->dev.of_node,
			},
			host->parts,
			host->nr_parts);

	platform_set_drvdata(pdev, host);

	return 0;

out_clk_disable:
	clk_disable_unprepare(host->sic_clk);
	clk_disable_unprepare(host->nand_clk);
out_mem_free:
	kfree(host);
 	return ret;
}

static int n329_nand_remove(struct platform_device *pdev)
{
	struct n329_nand_host *host = platform_get_drvdata(pdev);

	nand_release(&host->mtd);
	
	clk_disable_unprepare(host->sic_clk);
	clk_disable_unprepare(host->nand_clk);

	kfree(host);

	return 0;
}

static const struct of_device_id n329_nand_dt_ids[] = {
	{
		.compatible = "nuvoton,n32905-nand"
	},
	{ /* sentinel */ }
};

static struct platform_driver n329_nand_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(n329_nand_dt_ids),
	},
	.probe = n329_nand_probe,
	.remove = n329_nand_remove,
};
module_platform_driver(n329_nand_driver);

MODULE_DESCRIPTION("Nuvoton NAND MTD driver");
MODULE_AUTHOR("Michael P. Thompson <mpthompson@gmail.com>");
MODULE_LICENSE("GPL v2");
