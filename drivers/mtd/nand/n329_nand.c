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
	spinlock_t lock;

	struct mtd_partition *parts;    /* partition table */
 	int nr_parts;                   /* size of parts */
};

extern struct semaphore  fmi_sem;

static inline u32 n329_nand_read(struct n329_nand_host *host, u32 addr)
{
	return n329_sic_read(host->dev->parent, addr);
}

static inline void n329_nand_write(struct n329_nand_host *host, u32 value, u32 addr)
{
	return n329_sic_write(host->dev->parent, value, addr);
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
	n329_nand_write(host, (n329_nand_read(host, REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_read(host, REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_write(host, FMI_SM_EN, REG_FMICR);

	ret = (unsigned char) n329_nand_read(host, REG_SMDATA);

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
	n329_nand_write(host, (n329_nand_read(host, REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_read(host, REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_write(host, FMI_SM_EN, REG_FMICR);

	for (i = 0; i < len; i++)
		buf[i] = (unsigned char) n329_nand_read(host, REG_SMDATA);

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
	n329_nand_write(host, (n329_nand_read(host, REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_read(host, REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_write(host, FMI_SM_EN, REG_FMICR);

	for (i = 0; i < len; i++)
		n329_nand_write(host, buf[i], REG_SMDATA);

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
	n329_nand_write(host, (n329_nand_read(host, REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_read(host, REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_write(host, FMI_SM_EN, REG_FMICR);

  	up(&fmi_sem);
}

static int n329_nand_check_ready_busy(struct n329_nand_host *host)
{
	unsigned int val;

	spin_lock(&host->lock);

#if defined(ONBOARD_NAND)
	val = n329_nand_read(host, REG_SMISR) & SMISR_RB0;
#endif

#if defined(NANDCARD_NAND)
	val = n329_nand_read(host, REG_SMISR) & SMISR_RB1;
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
	n329_nand_write(host, (n329_nand_read(host, REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_read(host, REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_write(host, FMI_SM_EN, REG_FMICR);

	ready = n329_nand_check_ready_busy(host) ? 1 : 0;

	up(&fmi_sem);

	return ready;
}

int n329_nand_wait_ready_busy(struct n329_nand_host *host)
{
#if defined(ONBOARD_NAND)
	while(1) {
		if (n329_nand_read(host, REG_SMISR) & SMISR_RB0_IF) {
			n329_nand_write(host, SMISR_RB0_IF, REG_SMISR);
			return 1;
		}
	}
#endif

#if defined(NANDCARD_NAND)
	while(1) {
		if (n329_nand_read(host, REG_SMISR) & SMISR_RB1_IF) {
			n329_nand_write(host, SMISR_RB1_IF, REG_SMISR);
			return 1;
		}
	}
#endif

	return 0;
}

static void n329_nand_reset(struct n329_nand_host *host)
{
	unsigned int volatile i;

	n329_nand_write(host, 0xff, REG_SMCMD);

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
	n329_nand_write(host, (n329_nand_read(host, REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS0, REG_SMCSR);
#endif

	if ((n329_nand_read(host, REG_FMICR) & FMI_SM_EN) != FMI_SM_EN)
		n329_nand_write(host, FMI_SM_EN, REG_FMICR);

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

			n329_nand_write(host, readcommand, REG_SMCMD);
		}
	}

	n329_nand_write(host, command, REG_SMCMD);

	if (column != -1 || page_addr != -1) {

#if defined(ONBOARD_NAND)
		/* Clear the R/B flag */
	  	n329_nand_write(host, SMISR_RB0_IF, REG_SMISR);
#endif

#if defined(NANDCARD_NAND)
		/* Clear the R/B flag */
	  	n329_nand_write(host, SMISR_RB1_IF, REG_SMISR);
#endif

		if (column != -1) {
			/* Adjust columns for 16 bit buswidth */
			if (chip->options & NAND_BUSWIDTH_16)
				column >>= 1;

			n329_nand_write(host, (unsigned char) column, 
						REG_SMADDR);

			/* Handle 2K page */
			if (mtd->writesize == 0x800)
			  	n329_nand_write(host, 
			  		(column >> 8) & 0x0f, 
			  		REG_SMADDR);
		}

		if (page_addr != -1) {
			n329_nand_write(host, page_addr & 0xff, REG_SMADDR);

			/* One more address cycle for devices > 128MiB */
			if (chip->chipsize >= (64 << 20))
			{
				n329_nand_write(host, 
					(page_addr >> 8 ) & 0xff,
						REG_SMADDR);
				n329_nand_write(host, 
					((page_addr >> 16) & 0xff) |
					0x80000000,
					REG_SMADDR);
			} else {
				n329_nand_write(host, 
					((page_addr >> 8) & 0xff) | 
					0x80000000,
					REG_SMADDR);
			}
		} else {
			n329_nand_write(host, ((page_addr >> 8) & 0xff) |
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
			n329_nand_write(host, NAND_CMD_RNDOUTSTART, 
					REG_SMCMD);
		break;

	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
		/* Begin command latch cycle */
		if(mtd->writesize == 0x800) {
#if defined(ONBOARD_NAND)
		  	n329_nand_write(host, 0x400, REG_SMISR);
#endif
#if defined(NANDCARD_NAND)
		  	n329_nand_write(host, 0x800, REG_SMISR);
#endif
			n329_nand_write(host, NAND_CMD_READSTART, 
					REG_SMCMD);

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
	
	n329_nand_write(host, n329_nand_read(host, REG_FMICR) | 
		FMI_SM_EN, REG_FMICR);

	n329_nand_write(host, 0x3050b, REG_SMTCR);
  
#if defined(ONBOARD_NAND)		
	/* CS0 is selected */
	n329_nand_write(host, (n329_nand_read(host, REG_SMCSR) &
		~(SMCR_CS1 | SMCR_CS0)) | SMCR_CS1, REG_SMCSR);
#endif		
#if defined(NANDCARD_NAND)
	/* CS1 is selected */
	n329_nand_write(host, (n329_nand_read(host, REG_SMCSR) &
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
	int ret = 0;

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
