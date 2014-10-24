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

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "n329_udc.h"

#define DRIVER_DESC "Nuvoton N329XX USB Gadget Driver"

#define BITS(start,end)		((0xffffffff >> (31 - start)) & (0xffffffff << end))

#define USBD_BASE	0x000

/* USB Device Control Registers */
#define REG_USBD_IRQ_STAT_L		(USBD_BASE + 0x00)	/* Interrupt status low register */
#define REG_USBD_IRQ_ENB_L		(USBD_BASE + 0x08)	/* Interrupt enable low register */
#define REG_USBD_IRQ_STAT		(USBD_BASE + 0x10)	/* USB interrupt status register */
#define REG_USBD_IRQ_ENB		(USBD_BASE + 0x14)	/* USB interrupt enable register */
#define REG_USBD_OPER			(USBD_BASE + 0x18)	/* USB operation register */
#define REG_USBD_FRAME_CNT		(USBD_BASE + 0x1c)	/* USB frame count register */
#define REG_USBD_ADDR			(USBD_BASE + 0x20)	/* USB address register */
#define REG_USBD_MEM_TEST		(USBD_BASE + 0x24)	/* USB test mode register */
#define REG_USBD_CEP_DATA_BUF	(USBD_BASE + 0x28)	/* Control-ep data buffer register */
#define REG_USBD_CEP_CTRL_STAT	(USBD_BASE + 0x2c)	/* Control-ep control and status register */
#define REG_USBD_CEP_IRQ_ENB	(USBD_BASE + 0x30)	/* Control-ep interrupt enable register */
#define REG_USBD_CEP_IRQ_STAT	(USBD_BASE + 0x34)	/* Control-ep interrupt status register */
#define REG_USBD_IN_TRNSFR_CNT	(USBD_BASE + 0x38)	/* In-transfer data count register */
#define REG_USBD_OUT_TRNSFR_CNT	(USBD_BASE + 0x3c)	/* Out-transfer data count register */
#define REG_USBD_CEP_CNT		(USBD_BASE + 0x40)	/* Control-ep data count register */
#define REG_USBD_SETUP1_0		(USBD_BASE + 0x44)	/* Setup byte1 & byte0 register */
#define REG_USBD_SETUP3_2		(USBD_BASE + 0x48)	/* Setup byte3 & byte2 register */
#define REG_USBD_SETUP5_4		(USBD_BASE + 0x4c)	/* Setup byte5 & byte4 register */
#define REG_USBD_SETUP7_6		(USBD_BASE + 0x50)	/* Setup byte7 & byte6 register */
#define REG_USBD_CEP_START_ADDR	(USBD_BASE + 0x54)	/* Control-ep ram start address register */
#define REG_USBD_CEP_END_ADDR	(USBD_BASE + 0x58)	/* Control-ep ram end address register */
#define REG_USBD_DMA_CTRL_STS	(USBD_BASE + 0x5c)	/* DMA control and status register */
#define REG_USBD_DMA_CNT		(USBD_BASE + 0x60)	/* DMA count register */
/* Endpoint A */
#define REG_USBD_EPA_DATA_BUF	(USBD_BASE + 0x64)	/* Endpoint A data buffer register */
#define REG_USBD_EPA_IRQ_STAT	(USBD_BASE + 0x68)	/* Endpoint A interrupt status register */
#define REG_USBD_EPA_IRQ_ENB	(USBD_BASE + 0x6c)	/* Endpoint A interrupt enable register */
#define REG_USBD_EPA_DATA_CNT	(USBD_BASE + 0x70)	/* Data count available in endpoint A buffer */
#define REG_USBD_EPA_RSP_SC		(USBD_BASE + 0x74)	/* Endpoint A response register set/clear */
#define REG_USBD_EPA_MPS		(USBD_BASE + 0x78)	/* Endpoint A max packet size register */
#define REG_USBD_EPA_TRF_CNT	(USBD_BASE + 0x7c)	/* Endpoint A transfer count register */
#define REG_USBD_EPA_CFG		(USBD_BASE + 0x80)	/* Endpoint A configuration register */
#define REG_USBD_EPA_START_ADDR	(USBD_BASE + 0x84)	/* Endpoint A ram start address register */
#define REG_USBD_EPA_END_ADDR	(USBD_BASE + 0x88)	/* Endpoint A ram end address register */
/* Endpoint B */
#define REG_USBD_EPB_DATA_BUF	(USBD_BASE + 0x8c)	/* Endpoint B data buffer register */
#define REG_USBD_EPB_IRQ_STAT	(USBD_BASE + 0x90)	/* Endpoint B interrupt status register */
#define REG_USBD_EPB_IRQ_ENB	(USBD_BASE + 0x94)	/* Endpoint B interrupt enable register */
#define REG_USBD_EPB_DATA_CNT	(USBD_BASE + 0x98)	/* Data count available in endpoint B buffer */
#define REG_USBD_EPB_RSP_SC		(USBD_BASE + 0x9c)	/* Endpoint B response register set/clear */
#define REG_USBD_EPB_MPS		(USBD_BASE + 0xa0)	/* Endpoint B max packet size register */
#define REG_USBD_EPB_TRF_CNT	(USBD_BASE + 0xa4)	/* Endpoint B transfer count register */
#define REG_USBD_EPB_CFG		(USBD_BASE + 0xa8)	/* Endpoint B configuration register */
#define REG_USBD_EPB_START_ADDR	(USBD_BASE + 0xac)	/* Endpoint B ram start address register */
#define REG_USBD_EPB_END_ADDR	(USBD_BASE + 0xb0)	/* Endpoint B ram end address register */
/* Endpoint C */
#define REG_USBD_EPC_DATA_BUF	(USBD_BASE + 0xb4)	/* Endpoint C data buffer register */
#define REG_USBD_EPC_IRQ_STAT	(USBD_BASE + 0xb8)	/* Endpoint C interrupt status register */
#define REG_USBD_EPC_IRQ_ENB	(USBD_BASE + 0xbc)	/* Endpoint C interrupt enable register */
#define REG_USBD_EPC_DATA_CNT	(USBD_BASE + 0xc0)	/* Data count available in endpoint C buffer */
#define REG_USBD_EPC_RSP_SC		(USBD_BASE + 0xc4)	/* Endpoint C response register set/clear */
#define REG_USBD_EPC_MPS		(USBD_BASE + 0xc8)	/* Endpoint C max packet size register */
#define REG_USBD_EPC_TRF_CNT	(USBD_BASE + 0xcc)	/* Endpoint C transfer count register */
#define REG_USBD_EPC_CFG		(USBD_BASE + 0xd0)	/* Endpoint C configuration register */
#define REG_USBD_EPC_START_ADDR	(USBD_BASE + 0xd4)	/* Endpoint C ram start address register */
#define REG_USBD_EPC_END_ADDR	(USBD_BASE + 0xd8)	/* Endpoint C ram end address register */
/* Endpoint D */
#define REG_USBD_EPD_DATA_BUF	(USBD_BASE + 0xdc)	/* Endpoint D data buffer register */
#define REG_USBD_EPD_IRQ_STAT	(USBD_BASE + 0xe0)	/* Endpoint D interrupt status register */
#define REG_USBD_EPD_IRQ_ENB	(USBD_BASE + 0xe4)	/* Endpoint D interrupt enable register */
#define REG_USBD_EPD_DATA_CNT	(USBD_BASE + 0xe8)	/* Data count available in endpoint D buffer */
#define REG_USBD_EPD_RSP_SC		(USBD_BASE + 0xec)	/* Endpoint D response register set/clear */
#define REG_USBD_EPD_MPS		(USBD_BASE + 0xf0)	/* Endpoint D max packet size register */
#define REG_USBD_EPD_TRF_CNT	(USBD_BASE + 0xf4)	/* Endpoint D transfer count register */
#define REG_USBD_EPD_CFG		(USBD_BASE + 0xf8)	/* Endpoint D configuration register */
#define REG_USBD_EPD_START_ADDR	(USBD_BASE + 0xfc)	/* Endpoint D ram start address register */
#define REG_USBD_EPD_END_ADDR	(USBD_BASE + 0x100)	/* Endpoint D ram end address register */
/* Endpoint E */
#define REG_USBD_EPE_DATA_BUF	(USBD_BASE + 0x104)	/* Endpoint E data buffer register */
#define REG_USBD_EPE_IRQ_STAT	(USBD_BASE + 0x108)	/* Endpoint E interrupt status register */
#define REG_USBD_EPE_IRQ_ENB	(USBD_BASE + 0x10c)	/* Endpoint E interrupt enable register */
#define REG_USBD_EPE_DATA_CNT	(USBD_BASE + 0x110)	/* Data count available in endpoint E buffer */
#define REG_USBD_EPE_RSP_SC		(USBD_BASE + 0x114)	/* Endpoint E response register set/clear */
#define REG_USBD_EPE_MPS		(USBD_BASE + 0x118)	/* Endpoint E max packet size register */
#define REG_USBD_EPE_TRF_CNT	(USBD_BASE + 0x11c)	/* Endpoint E transfer count register */
#define REG_USBD_EPE_CFG		(USBD_BASE + 0x120)	/* Endpoint E configuration register */
#define REG_USBD_EPE_START_ADDR	(USBD_BASE + 0x124)	/* Endpoint E ram start address register */
#define REG_USBD_EPE_END_ADDR	(USBD_BASE + 0x128)	/* Endpoint E ram end address register */
/* Endpoint F */
#define REG_USBD_EPF_DATA_BUF	(USBD_BASE + 0x12c)	/* Endpoint F data buffer register */
#define REG_USBD_EPF_IRQ_STAT	(USBD_BASE + 0x130)	/* Endpoint F interrupt status register */
#define REG_USBD_EPF_IRQ_ENB	(USBD_BASE + 0x134)	/* Endpoint F interrupt enable register */
#define REG_USBD_EPF_DATA_CNT	(USBD_BASE + 0x138)	/* Data count available in endpoint F buffer */
#define REG_USBD_EPF_RSP_SC		(USBD_BASE + 0x13c)	/* Endpoint F response register set/clear */
#define REG_USBD_EPF_MPS		(USBD_BASE + 0x140)	/* Endpoint F max packet size register */
#define REG_USBD_EPF_TRF_CNT	(USBD_BASE + 0x144)	/* Endpoint F transfer count register */
#define REG_USBD_EPF_CFG		(USBD_BASE + 0x148)	/* Endpoint F configuration register */
#define REG_USBD_EPF_START_ADDR	(USBD_BASE + 0x14c)	/* Endpoint F ram start address register */
#define REG_USBD_EPF_END_ADDR	(USBD_BASE + 0x150)	/* Endpoint F ram end address register */
#define REG_USBD_AHB_DMA_ADDR	(USBD_BASE + 0x700)	/* AHB_DMA address register */
/* Phy */
#define REG_USBD_PHY_CTL    	(USBD_BASE + 0x704)    /* USB PHY control register */
	#define BISTEN		BIT(0)
	#define BISTERR		BIT(1)
	#define SIDDQ		BIT(2)
	#define XO_ON		BIT(3)
	#define CLK_SEL		BITS(5,4)
	#define REFCLK		BIT(6)
	#define CLK48		BIT(7)
	#define VBUS_DETECT	BIT(8)
	#define PHY_SUSPEND	BIT(9)								
	#define VBUS_STATUS	BIT(31)

extern unsigned long n329_clocks_config_usb20(unsigned long rate);

static struct clk *usb20_clk;
static struct clk *usb20_hclk;

static int __init n329_udc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	usb20_clk = of_clk_get(np, 0);
	if (IS_ERR(usb20_clk))
		return PTR_ERR(usb20_clk);
	usb20_hclk = of_clk_get(np, 1);
	if (IS_ERR(usb20_hclk)) {
		clk_put(usb20_clk);
		return PTR_ERR(usb20_hclk);
	}

	clk_prepare_enable(usb20_clk);
	clk_prepare_enable(usb20_hclk);
	n329_clocks_config_usb20(12000000);

	dev_info(&pdev->dev, "Probing " DRIVER_DESC "\n");
	return 0;
}

static int __exit n329_udc_remove(struct platform_device *pdev)
{
	// struct usba_udc *udc = platform_get_drvdata(pdev);
	dev_info(&pdev->dev, "Removing " DRIVER_DESC "\n");

	clk_disable_unprepare(usb20_hclk);
	clk_disable_unprepare(usb20_clk);
	clk_put(usb20_hclk);
	clk_put(usb20_clk);

	return 0;
}

static const struct of_device_id n329_udc_dt_ids[] = {
	{ .compatible = "nuvoton,udc" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, n329_udc_dt_ids);

static struct platform_driver udc_driver = {
	.remove		= __exit_p(n329_udc_remove),
	.driver		= {
		.name		= "nuvoton_usb_udc",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(n329_udc_dt_ids),
	},
};

module_platform_driver_probe(udc_driver, n329_udc_probe);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Thompson (mpthompson@gmail.com)");
MODULE_ALIAS("platform:n329-udc");
