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

#define USBD_INTERVAL_TIME  100	

extern unsigned long n329_clocks_config_usb20(unsigned long rate);

static struct clk *usb20_clk;
static struct clk *usb20_hclk;

static int volatile usb_pc_status = 0;
static int volatile usb_pc_status_ckeck = 0;
static struct timer_list usbd_timer;
static u32 volatile g_USB_Mode_Check = 0;
static int volatile g_usbd_access = 0;
static int volatile usb_eject_flag = 0;
static void timer_check_usbd_access(unsigned long dummy);

static const char gadget_name [] = "w55fa93-udc";
static const char driver_desc [] = DRIVER_DESC;
static const char ep0name [] = "ep0";

static const char *const ep_name[] = {
	ep0name,                                /* everyone has ep0 */
	"ep1", "ep2", "ep3", "ep4", "ep5", "ep6"
};

#define EP0_FIFO_SIZE           64
#define EP_FIFO_SIZE            512

static struct n329_udc controller;

static void udc_isr_rst(struct n329_udc *dev);
static void udc_isr_dma(struct n329_udc *dev);
static void udc_isr_ctrl_pkt(struct n329_udc *dev);
static void udc_isr_update_dev(struct n329_udc *dev);
static u32 udc_transfer(struct n329_ep *ep, u8* buf, size_t size, u32 mode);

static void __iomem *udc_base;

static inline void n329_udc_write(u32 value, u32 addr)
{
	__raw_writel(value, udc_base + addr);
}

static inline u32 n329_udc_read(u32 addr)
{
	return __raw_readl(udc_base + addr);
}

static void n329_udc_nuke(struct n329_udc *udc, struct n329_ep *ep)
{
	while (!list_empty (&ep->queue)) {
		struct n329_request  *req;
		req = list_entry (ep->queue.next, struct n329_request, queue);
		list_del_init (&req->queue);
		req->req.status = -ESHUTDOWN;
		spin_unlock (&udc->lock);
		req->req.complete (&ep->ep, &req->req);
		spin_lock (&udc->lock);
	}
}

static void n329_udc_done(struct n329_ep *ep, struct n329_request *req, int status)
{
	struct n329_udc *udc = &controller;

	list_del_init(&req->queue); //del req->queue from ep->queue

	if (list_empty(&ep->queue)) {
		if (ep->index)
			n329_udc_write(0,  REG_USBD_EPA_IRQ_ENB + 0x28*(ep->index-1));
	} else {
		n329_udc_write(ep->irq_enb,  REG_USBD_EPA_IRQ_ENB + 0x28*(ep->index-1));
	}

	if (likely (req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	if (req->dma_mapped) {
		dma_unmap_single(&udc->pdev->dev, req->req.dma, 
				req->req.length, ep->EP_Dir ? 
						DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->req.dma = DMA_ADDR_INVALID;
		req->dma_mapped = 0;
	}

	req->req.complete(&ep->ep, &req->req);
}


static void n329_udc_start_write(struct n329_ep *ep, u8* buf, u32 length)
{
	struct n329_udc *dev = ep->dev;
	u32 volatile reg;
	
	if (dev->usb_dma_trigger) {
		printk("*** dma trigger ***\n");
		return;
	}
	g_usbd_access++;
	dev->usb_dma_trigger = 1;
	dev->usb_dma_cnt = length;
	dev->usb_dma_owner = ep->index;

	n329_udc_write(USB_DMA_REQ | USB_RST_STS | 
				   USB_SUS_REQ | USB_VBUS_STS,
				   REG_USBD_IRQ_ENB);

	/* Give DMA the memory physcal address */
	n329_udc_write((u32)buf, REG_USBD_AHB_DMA_ADDR);
	n329_udc_write(length,  REG_USBD_DMA_CNT);

	reg = n329_udc_read(REG_USBD_DMA_CTRL_STS);
	if ((reg & 0x40) != 0x40)
		n329_udc_write((reg | 0x00000020), REG_USBD_DMA_CTRL_STS);

	return ;
}

static void n329_udc_start_read(struct n329_ep *ep, u8* buf, u32 length)
{
	struct n329_udc	*dev = ep->dev;
	
	if (dev->usb_dma_trigger) {
		printk("*** dma trigger ***\n");
		return;
	}

	g_usbd_access++;

	n329_udc_write(USB_DMA_REQ | USB_RST_STS | USB_SUS_REQ | USB_VBUS_STS, 
					REG_USBD_IRQ_ENB);

	/* Tell DMA the memory address and length */
	n329_udc_write((u32) buf, REG_USBD_AHB_DMA_ADDR);
	n329_udc_write(length, REG_USBD_DMA_CNT);

	dev->usb_dma_trigger = 1;
	dev->usb_dma_cnt = length;
	dev->usb_dma_loop = (length+31)/32;
	dev->usb_dma_owner = ep->index;

	n329_udc_write(n329_udc_read(REG_USBD_DMA_CTRL_STS) | 0x00000020, 
					REG_USBD_DMA_CTRL_STS);

	return ;
}

static int n329_udc_write_packet(struct n329_ep *ep, struct n329_request *req)
{
	struct n329_udc *udc = &controller;
	unsigned len;
	u8 *buf;
	u8 tmp_data;
	u16 i;
	u32 max;

	buf = req->req.buf + req->req.actual;

	if (ep->EP_Num == 0) {
		/* Control endpoint doesn't use DMA */
		max = ep->ep.maxpacket;
		len = min(req->req.length - req->req.actual, max);
		if (len == 0) {
			if (req->req.zero&&!req->req.length) {
				n329_udc_write(CEP_ZEROLEN, REG_USBD_CEP_CTRL_STAT);
			}
		} else {
			for (i=0; i<len; i++) {
				tmp_data = *buf++;
				__raw_writeb(tmp_data, REG_USBD_CEP_DATA_BUF);
			}
			n329_udc_write(len, REG_USBD_IN_TRNSFR_CNT);
		}
		req->req.actual += len;
	} else {
		len = req->req.length - req->req.actual;

		if (req->req.dma == DMA_ADDR_INVALID) {
			req->req.dma = dma_map_single(&udc->pdev->dev, req->req.buf, req->req.length, ep->EP_Dir ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
			req->dma_mapped = 1;
		} else {
			dma_sync_single_for_device(&udc->pdev->dev, req->req.dma, req->req.length, ep->EP_Dir ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
			req->dma_mapped = 0;
		}
		buf = (u8*) (req->req.dma + req->req.actual);
		if (len == 0) {
			printk("n329_udc_write_packet send zero packet\n");
			n329_udc_write((n329_udc_read(REG_USBD_EPA_RSP_SC + 0x28 * (ep->index - 1)) & 0xF7) | 
					EP_ZERO_IN, REG_USBD_EPA_RSP_SC + 0x28 * (ep->index - 1));
		} else {
			len = udc_transfer(ep, buf, len, DMA_WRITE);
		}
		req->req.actual += len;
	}

	return len;
}

static int n329_udc_write_fifo(struct n329_ep *ep, struct n329_request *req)
{
	u32 len;
	
	len = n329_udc_write_packet(ep, req);

	/* return:  0 = still running, 1 = completed, negative = errno */
	/* last packet is often short (sometimes a zlp) */
	if (req->req.length == req->req.actual /* && !req->req.zero*/) {
		n329_udc_done(ep, req, 0);
		return 1;
	}

	return 0;
}

static int n329_udc_read_packet(struct n329_ep *ep,u8 *buf, 
				struct n329_request *req, u16 cnt)
{
	struct n329_udc *udc = &controller;
	unsigned len, fifo_count;
	u16 i;
	u8 data;	

	if (ep->EP_Num == 0) { //ctrl pipe don't use DMA
		fifo_count = n329_udc_read(REG_USBD_CEP_CNT);
		len = min(req->req.length - req->req.actual, fifo_count);
  
		for (i=0; i<len; i++) {
			data = __raw_readb(REG_USBD_CEP_DATA_BUF);
			*buf++ = data&0xFF;
		}
		req->req.actual += len;

	} else {
		if (req->req.dma == DMA_ADDR_INVALID) {
			req->req.dma = dma_map_single(&udc->pdev->dev, req->req.buf, req->req.length, ep->EP_Dir ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
			req->dma_mapped = 1;
		} else {
			dma_sync_single_for_device(&udc->pdev->dev, req->req.dma, req->req.length, ep->EP_Dir ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
			req->dma_mapped = 0;
		}
		buf = (u8*)req->req.dma;
		len = req->req.length - req->req.actual;

		if (cnt && cnt < ep->ep.maxpacket)
			len = udc_transfer(ep, buf, cnt, DMA_READ);
		else if (len) {
			len = udc_transfer(ep, buf, len, DMA_READ);
		}
		req->req.actual += len;
	}

	return len;
}

static int n329_udc_read_fifo(struct n329_ep *ep, struct n329_request *req, u16 cnt)
{
	u8 *buf;
	unsigned bufferspace;
	int is_last=1;
	int fifo_count = 0;

	/* return:  0 = still running, 1 = queue empty, negative = errno */
	buf = req->req.buf + req->req.actual;
	bufferspace = req->req.length - req->req.actual;
	if (!bufferspace) {
		printk("n329_udc_read_fifo: Buffer full !!\n");
		return -1;
	}

	fifo_count = n329_udc_read_packet(ep, buf, req, cnt);

	if (req->req.length == req->req.actual)
		n329_udc_done(ep, req, 0);
	else if (fifo_count && (fifo_count < ep->ep.maxpacket)) {
		n329_udc_done(ep, req, 0);
		/* Did we overflow this request? */
		if (req->req.length != req->req.actual) {
			/* Did the device read less than host wrote */
			if (req->req.short_not_ok) {
				printk("%s(): EOVERFLOW set\n", __FUNCTION__);
				req->req.status = -EOVERFLOW;
			}
		}
	} else
		is_last = 0;

	return is_last;
}

void n329_udc_paser_irq_stat(int irq, struct n329_udc *dev)
{
	u32 volatile reg;

	/* Clear the IRQ bit */
	n329_udc_write(irq, REG_USBD_IRQ_STAT);

	switch (irq) {
	case USB_VBUS_STS:		
		reg = n329_udc_read(REG_USBD_PHY_CTL);	
		if (reg & BIT31)
		{
			usb_pc_status_ckeck = 1;
			usb_pc_status = 0;
			usb_eject_flag = 0;
			g_USB_Mode_Check = 1;
			printk("<USBD - USBD plug>\n");			
		}
		else
		{
			usb_pc_status_ckeck = 0;
			usb_pc_status = 0;
			g_usbd_access = 0;
			usb_eject_flag = 1;
			g_USB_Mode_Check  = 0;
			del_timer(&usbd_timer);
			printk("<USBD - USBD Un-plug>\n");	
		}

	case USB_SOF:
		break;

	case USB_RST_STS://reset
		if (usb_pc_status_ckeck == 1 && usb_pc_status == 0)
		{
			usb_pc_status = 1;
			printk("<USBD - CONNECT TO PC>\n");		
		}
		if (g_USB_Mode_Check)
		{
			g_USB_Mode_Check = 0;
			mod_timer(&usbd_timer, jiffies + USBD_INTERVAL_TIME); 
		}
		udc_isr_rst(dev);
		break;

	case USB_RESUME:
		usb_eject_flag = 0;
		n329_udc_write(USB_RST_STS | USB_SUS_REQ | USB_VBUS_STS, 
					   REG_USBD_IRQ_ENB);
		break;

	case USB_SUS_REQ:
		if (dev != NULL) {
			usb_eject_flag = 1;
			n329_udc_write(USB_RST_STS | USB_RESUME| USB_VBUS_STS, 
						   REG_USBD_IRQ_ENB);
		}
		break;

	case USB_HS_SETTLE:
		/* Default state */
		dev->usb_devstate = USB_FULLSPEED;
		dev->usb_address = 0;
		n329_udc_write(0x002, REG_USBD_CEP_IRQ_ENB);
		break;

	case USB_DMA_REQ:
		udc_isr_dma(dev);
		break;

	case USABLE_CLK:
		break;

	default:
		break;
	}
}

void n329_udc_paser_irq_cep(int irq, struct n329_udc *dev, u32 IrqSt)
{
	struct n329_ep *ep = &dev->ep[0];
	struct n329_request	*req;
	int is_last=1;

	if (list_empty(&ep->queue)) {
		req = 0;
	} else {
		req = list_entry(ep->queue.next, struct n329_request, queue);
	}

	switch (irq) {
	case CEP_SUPPKT:
		/* Receive setup packet */
		dev->ep0state = EP0_IDLE;
		dev->setup_ret = 0;
		udc_isr_ctrl_pkt(dev);
		break;

	case CEP_DATA_RXD:

		if (dev->ep0state == EP0_OUT_DATA_PHASE) {
			if (req)
				is_last = n329_udc_read_fifo(ep,req, 0);

			n329_udc_write(0x400, REG_USBD_CEP_IRQ_STAT);

			if (!is_last)
				/* Enable out token and status complete int */
				n329_udc_write(0x440, REG_USBD_CEP_IRQ_ENB);
			else {
				/* Transfer is finished */
				n329_udc_write(0x04C, REG_USBD_CEP_IRQ_STAT);
				/* clear nak so that sts stage is complete */
				n329_udc_write(CEP_NAK_CLEAR, REG_USBD_CEP_CTRL_STAT);
				/* suppkt int//enb sts completion int */
				n329_udc_write(0x400, REG_USBD_CEP_IRQ_ENB);
				dev->ep0state = EP0_END_XFER;
			}
		}

		return;

	case CEP_IN_TOK:

		if ((IrqSt & CEP_STS_END))
			dev->ep0state=EP0_IDLE;

		if (dev->setup_ret < 0) {
			printk("CEP send zero pkt\n");
			n329_udc_write(CEP_ZEROLEN, REG_USBD_CEP_CTRL_STAT);
			/* enb sts completion int */
			n329_udc_write(0x400, REG_USBD_CEP_IRQ_ENB);
		} else if (dev->ep0state == EP0_IN_DATA_PHASE) {

			if (req)
				is_last = n329_udc_write_fifo(ep,req);

			if (!is_last)
				n329_udc_write(0x408, REG_USBD_CEP_IRQ_ENB);
			else {
				if (dev->setup_ret >= 0)
					/* Clear nak so that sts stage is complete */
					n329_udc_write(CEP_NAK_CLEAR, REG_USBD_CEP_CTRL_STAT);
				/* suppkt int//enb sts completion int */
				n329_udc_write(0x402, REG_USBD_CEP_IRQ_ENB);

				if (dev->setup_ret < 0)
					dev->ep0state = EP0_IDLE;
				else if (dev->ep0state != EP0_IDLE)
					dev->ep0state = EP0_END_XFER;
			}
		}

		return;

	case CEP_PING_TOK:
		/* suppkt int//enb sts completion int */
		n329_udc_write(0x402, REG_USBD_CEP_IRQ_ENB);
		return;

	case CEP_DATA_TXD:
		 return;

	case CEP_STS_END:
		n329_udc_write(0x4A, REG_USBD_CEP_IRQ_ENB);
		udc_isr_update_dev(dev);
		dev->ep0state=EP0_IDLE;
		dev->setup_ret = 0;

		break;

	default:
		break;
	}
}

void n329_udc_paser_irq_nep(int irq, struct n329_ep *ep, u32 IrqSt)
{
	struct n329_udc *dev = ep->dev;
	struct n329_request	*req;
	int i;
	u16 fifo_count, loop;
	u8 *buf;
	u8 data;
	u32 datacnt_reg;

	if (list_empty(&ep->queue)) {
		printk("nep->queue is empty\n");
		req = 0;
	} else {
		n329_udc_write(n329_udc_read(REG_USBD_EPA_IRQ_STAT + 0x28 * (ep->index - 1)),
			     REG_USBD_EPA_IRQ_STAT + 0x28 * (ep->index - 1));
		req = list_entry(ep->queue.next, struct n329_request, queue);
	}

	switch (irq) {
	case EP_IN_TOK:
		n329_udc_write(irq, REG_USBD_EPA_IRQ_STAT + 0x28 * (ep->index - 1));

		if (ep->EP_Type == EP_TYPE_BLK) {
			/* Send last packet */
			if (n329_udc_read(REG_USBD_EPA_RSP_SC + 0x28 * (ep->index - 1)) & 0x40) {
				printk("send last packet\n");
				break;
			}
		}
		if (req == NULL) {
			n329_udc_write(0, REG_USBD_EPA_IRQ_ENB + 0x28 * (ep->index-1));
			break;
		}

		/* Wait DMA complete */
		while (n329_udc_read(REG_USBD_DMA_CTRL_STS) & 0x20);
		if (dev->usb_dma_trigger) {
			printk("IN dma triggered\n");
			while ((n329_udc_read(REG_USBD_IRQ_STAT) & 0x20) == 0);
			n329_udc_write(0x20, REG_USBD_IRQ_STAT);
			udc_isr_dma(dev);
		}

		n329_udc_write_fifo(ep,req);
		break;

	case EP_BO_SHORT_PKT:
		if (req) {
			if (dev->usb_dma_trigger) {
				loop = n329_udc_read(REG_USBD_EPA_DATA_CNT + 0x28*(ep->index-1))>>16;
				printk("loop=%d, %d\n", loop, dev->usb_dma_loop);
				loop = dev->usb_dma_loop - loop;

				if (loop)
					req->req.actual += loop*32;//each loop 32 bytes
				//printk("reset dma\n");
				dev->usb_dma_trigger = 0;
				//reset DMA
				n329_udc_write(0x80, REG_USBD_DMA_CTRL_STS);
				n329_udc_write(0x00, REG_USBD_DMA_CTRL_STS);

				//printk("after DMA reset DATA_CNT=%x, %x\n", n329_udc_read(REG_USBD_EPA_DATA_CNT + 0x28*(ep->index-1)), dev->irq_enbl);

				n329_udc_write(dev->irq_enbl, REG_USBD_IRQ_ENB_L);
			}

			fifo_count = n329_udc_read(REG_USBD_EPA_DATA_CNT + 0x28*(ep->index-1));

			buf = req->req.buf + req->req.actual;

			for (i=0; i<fifo_count; i++) {
				data = __raw_readb(REG_USBD_EPA_DATA_BUF + 0x28*(ep->index-1));                             
				*buf++ = data&0xFF;
			}
			if (ep->buffer_disabled) {
				/* Enable buffer */
				n329_udc_write((n329_udc_read(REG_USBD_EPA_RSP_SC + 0x28 * (ep->index-1))) & 0x77,
					     REG_USBD_EPA_RSP_SC + 0x28 * (ep->index - 1));
				/* Disable buffer when short packet */
				n329_udc_write((n329_udc_read(REG_USBD_EPA_RSP_SC+0x28*(ep->index-1)) & 0xF7) | 0x80,
					      REG_USBD_EPA_RSP_SC + 0x28 * (ep->index - 1));
			}

			req->req.actual += fifo_count;

			n329_udc_done(ep, req, 0);
		} else {
			n329_udc_write(0, REG_USBD_EPA_IRQ_ENB + 0x28 * (ep->index - 1));
		}

		break;

	case EP_DATA_RXD:

		if (req == NULL) {
			n329_udc_write(0, REG_USBD_EPA_IRQ_ENB + 0x28 * (ep->index - 1));
			break;
		}
		datacnt_reg = (u32)(REG_USBD_EPA_DATA_CNT + 0x28 * (ep->index - 1));
		if (n329_udc_read(datacnt_reg) == 0)
			break;

		/* wait DMA complete */
		while (n329_udc_read(REG_USBD_DMA_CTRL_STS) & 0x20);

		fifo_count = n329_udc_read(datacnt_reg);

		if (dev->usb_dma_trigger) {
			printk("RxED dma triggered\n");
			while ((n329_udc_read(REG_USBD_IRQ_STAT) & 0x20) == 0);
			n329_udc_write(0x02, REG_USBD_IRQ_STAT);
			udc_isr_dma(dev);
		}

		n329_udc_read_fifo(ep,req, n329_udc_read(datacnt_reg));

		break;
	default:
		printk("irq: %d not handled !\n",irq);
		n329_udc_write(irq, REG_USBD_EPA_IRQ_STAT + 0x28*(ep->index-1));
		break;
	}
}

void n329_udc_paser_irq_nepint(int irq, struct n329_ep *ep, u32 IrqSt)
{
	struct n329_udc *dev = ep->dev;
	struct n329_request	*req;

	n329_udc_write(irq, REG_USBD_EPA_IRQ_STAT + 0x28*(ep->index-1));

	if (list_empty(&ep->queue)) {
		printk("nepirq->queue is empty\n");
		req = 0;
		return;
	}

	req = list_entry(ep->queue.next, struct n329_request, queue);

	switch (irq) {
	case EP_IN_TOK:

		while (n329_udc_read(REG_USBD_DMA_CTRL_STS)&0x20);//wait DMA complete
		if (dev->usb_dma_trigger) {
			printk("int IN dma triggered\n");
			while ((n329_udc_read(REG_USBD_IRQ_STAT) & 0x20) == 0);
			n329_udc_write(0x20, REG_USBD_IRQ_STAT);
			udc_isr_dma(dev);
		}
		n329_udc_write_fifo(ep,req);

		break;
	default:
		printk("irq: %d not handled !\n",irq);
		n329_udc_write(irq, REG_USBD_EPA_IRQ_STAT + 0x28*(ep->index-1));
		break;

	}
}


static irqreturn_t n329_udc_irq(int irq, void *_dev)
{
	struct n329_udc *dev;
	struct n329_ep *ep;
	u32 volatile IrqStL, IrqEnL;
	u32 volatile  IrqSt, IrqEn;
	int i = 0, j;
	
	dev = (struct n329_udc *)(_dev);
	g_usbd_access++;
	IrqStL = n329_udc_read(REG_USBD_IRQ_STAT_L);
	IrqEnL = n329_udc_read(REG_USBD_IRQ_ENB_L);

	IrqStL = IrqStL & IrqEnL ;
	if (!IrqStL) {
		printk("Not our interrupt !\n");
		return IRQ_HANDLED;
	}

	if (IrqStL & IRQ_USB_STAT) {
		IrqSt = n329_udc_read(REG_USBD_IRQ_STAT);
		IrqEn = n329_udc_read(REG_USBD_IRQ_ENB);
		n329_udc_write(IrqSt, REG_USBD_IRQ_STAT);

		IrqSt = IrqSt & IrqEn ;

		if (IrqSt && (dev->driver || (IrqSt & USB_VBUS_STS))) {
			for (i=0; i<9; i++) {
				if (IrqSt&(1<<i)) {
					n329_udc_paser_irq_stat(1<<i,dev);
					break;
				}
			}
		}

	}

	if (IrqStL & IRQ_CEP) {

		IrqSt = n329_udc_read(REG_USBD_CEP_IRQ_STAT);
		IrqEn = n329_udc_read(REG_USBD_CEP_IRQ_ENB);
		IrqSt = IrqSt & IrqEn;

		n329_udc_write(IrqSt, REG_USBD_CEP_IRQ_STAT);

		if (IrqSt && dev->driver) {
			if (IrqSt & CEP_STS_END) { //deal with STS END
				if (dev->ep0state == EP0_OUT_DATA_PHASE)
					IrqSt &= 0x1BF7;
				n329_udc_paser_irq_cep(CEP_STS_END,dev,IrqSt);
			}
			for (i=0; i<13; i++) {
				if (i == 10)
					continue;
				if (IrqSt & (1 << i))
					n329_udc_paser_irq_cep(1 << i, dev, IrqSt);
			}
		}
	}

	if (IrqStL & IRQ_NCEP) {

		IrqStL >>= 2;

		for (j = 0; j < 6; j++) { //6 endpoints
			if (IrqStL & (1 << j)) {
				/* in-token and out token interrupt can deal with one only */
				IrqSt = n329_udc_read(REG_USBD_EPA_IRQ_STAT + 0x28 * j);
				IrqEn = n329_udc_read(REG_USBD_EPA_IRQ_ENB + 0x28 * j);
				IrqSt = IrqSt & IrqEn;

				if (IrqSt && dev->driver) {
					ep = &dev->ep[j+1];
					for (i = 12; i >= 0; i--) {
						if (IrqSt & (1<<i)) {
							/* Should we clear out token/RxED intr */
							if ((1 << i) == EP_BO_SHORT_PKT)
								IrqSt &= 0x1FCF;
							if ((ep->EP_Type == EP_TYPE_BLK) || 
								(ep->EP_Type == EP_TYPE_ISO))
								n329_udc_paser_irq_nep(1<<i, ep, IrqSt);
							else if (ep->EP_Type == EP_TYPE_INT)
								n329_udc_paser_irq_nepint(1<<i, ep, IrqSt);
							break;
						}
					}
				}
			}
		}
	}

	return IRQ_HANDLED;
}

static s32 n329_udc_get_sram_base(struct n329_udc *dev, u32 max)
{
	int i, cnt = 1, j;
	s32 start, end;
	static s32 sram_data[N329_ENDPOINTS][2] = {{0, 0x40}};

	for (i = 1; i < N329_ENDPOINTS; i++) {
		struct n329_ep *ep = &dev->ep[i];
		start = n329_udc_read(REG_USBD_EPA_START_ADDR + 0x28 * (ep->index - 1));
		end = n329_udc_read(REG_USBD_EPA_END_ADDR + 0x28 * (ep->index - 1));
		if (end - start > 0) {
			sram_data[cnt][0] = start;
			sram_data[cnt][1] = end + 1;
			cnt++;
		}
	}

	if (cnt == 1)
		return 0x40;

	/* Sort from small to large */
	for (j= 1; j < cnt; j++) {
		for (i = 0; i < cnt - j; i++) {
			if (sram_data[i][0] > sram_data[i+1][0]) {
				start = sram_data[i][0];
				end = sram_data[i][1];
				sram_data[i][0] = sram_data[i+1][0];
				sram_data[i][1] = sram_data[i+1][1];
				sram_data[i+1][0] = start;
				sram_data[i+1][1] = end;
			}
		}
	}

	for (i = 0; i < cnt-1; i++) {
		if (sram_data[i+1][0] - sram_data[i][1] >= max)
			return sram_data[i][1];
	}

	if (0x800 - sram_data[cnt-1][1] >= max)
		return sram_data[cnt-1][1];

	return -ENOBUFS;
}

static int n329_ep_enable(struct usb_ep *_ep, 
				const struct usb_endpoint_descriptor *desc)
{
	struct n329_ep *ep;
	struct n329_udc *dev;
	unsigned long flags;
	u32 max, tmp, int_en_reg;
	s32 sram_addr;

	ep = container_of(_ep, struct n329_ep, ep);

	if (!_ep || !desc || ep->desc || _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;
	dev = ep->dev;

	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	max = le16_to_cpu (desc->wMaxPacketSize) & 0x1fff;

	spin_lock_irqsave (&dev->lock, flags);
	_ep->maxpacket = max & 0x7ff;

	ep->desc = desc;
	ep->bEndpointAddress = desc->bEndpointAddress;

	/* set max packet */
	if (ep->index != 0) {
		n329_udc_write(max, REG_USBD_EPA_MPS + 0x28*(ep->index-1));
		ep->ep.maxpacket = max;

		sram_addr = n329_udc_get_sram_base(dev, max);

		if (sram_addr < 0)
			return sram_addr;

		n329_udc_write(sram_addr, REG_USBD_EPA_START_ADDR + 
							0x28 * (ep->index - 1));
		sram_addr = sram_addr + max;
		n329_udc_write(sram_addr-1, REG_USBD_EPA_END_ADDR + 
							0x28 * (ep->index - 1));
	}

	/* set type, direction, address; reset fifo counters */
	if (ep->index != 0) {
		ep->EP_Num = desc->bEndpointAddress & ~USB_DIR_IN;
		ep->EP_Dir = desc->bEndpointAddress &0x80 ? 1 : 0;
		ep->EP_Type = ep->desc->bmAttributes&USB_ENDPOINT_XFERTYPE_MASK;
		if (ep->EP_Type == USB_ENDPOINT_XFER_ISOC) {
			ep->EP_Type = EP_TYPE_ISO;
			ep->EP_Mode = EP_MODE_FLY;
		} else if (ep->EP_Type == USB_ENDPOINT_XFER_BULK) {
			ep->EP_Type = EP_TYPE_BLK;
			ep->EP_Mode = EP_MODE_AUTO;
		}
		if (ep->EP_Type == USB_ENDPOINT_XFER_INT) {
			ep->EP_Type = EP_TYPE_INT;
			ep->EP_Mode = EP_MODE_MAN;
		}
		/* DATA0 and flush SRAM */
		n329_udc_write(0x9, REG_USBD_EPA_RSP_SC+0x28*(ep->index-1));

		n329_udc_write(ep->EP_Num<<4|ep->EP_Dir<<3|ep->EP_Type<<1|1,
			     REG_USBD_EPA_CFG+0x28*(ep->index-1));
		n329_udc_write(ep->EP_Mode, REG_USBD_EPA_RSP_SC+0x28*(ep->index-1));

		/* Enable irqs */
		int_en_reg = n329_udc_read(REG_USBD_IRQ_ENB_L);
		n329_udc_write(int_en_reg | (1 << (ep->index + 1)),
			     REG_USBD_IRQ_ENB_L);
		dev->irq_enbl = n329_udc_read(REG_USBD_IRQ_ENB_L);

		if (ep->EP_Type == EP_TYPE_BLK) {
			if (ep->EP_Dir) //IN
				ep->irq_enb = 0x40;
			else {
				ep->irq_enb = 0x10; //0x1020;
				/* Disable buffer when short packet */
				n329_udc_write((n329_udc_read(REG_USBD_EPA_RSP_SC+0x28*(ep->index-1))&0xF7)|0x80,
					     REG_USBD_EPA_RSP_SC + 0x28*(ep->index-1));
				ep->buffer_disabled = 1;
			}
		} else if (ep->EP_Type == EP_TYPE_INT)
			ep->irq_enb = 0x40;
		else if (ep->EP_Type == EP_TYPE_ISO) {
			if (ep->EP_Dir) //IN
				ep->irq_enb = 0x40;
			else
				ep->irq_enb = 0x20;
		}
	}

	/* print some debug message */
	tmp = desc->bEndpointAddress;
	printk ("enable %s(%d) ep%02x%s-blk max %02x\n",
		_ep->name,ep->EP_Num, tmp, desc->bEndpointAddress & USB_DIR_IN ? "in" : "out", max);

	spin_unlock_irqrestore (&dev->lock, flags);

	return 0;
}

static int n329_ep_disable(struct usb_ep *_ep)
{
	struct n329_ep *ep = container_of(_ep, struct n329_ep, ep);
	unsigned long	flags;

	/* Sanity check */
	if (!_ep || !ep->desc)
		return -EINVAL;

	spin_lock_irqsave(&ep->dev->lock, flags);

	ep->desc = 0;

	n329_udc_write(0, REG_USBD_EPA_CFG + 0x28 * (ep->index - 1));
	n329_udc_write(0, REG_USBD_EPA_IRQ_ENB + 0x28 * (ep->index - 1));

	n329_udc_nuke(ep->dev, ep);

	n329_udc_write(0, REG_USBD_EPA_START_ADDR + 0x28 * (ep->index - 1));
	n329_udc_write(0, REG_USBD_EPA_END_ADDR + 0x28 * (ep->index - 1));

	spin_unlock_irqrestore(&ep->dev->lock, flags);

	printk("%s disabled\n", _ep->name);

	return 0;
}

static struct usb_request *n329_alloc_request(struct usb_ep *_ep,
				gfp_t mem_flags)
{
	struct n329_ep *ep;
	struct n329_request	*req;

	ep = container_of(_ep, struct n329_ep, ep);
	if (!_ep)
		return 0;

	req = kmalloc (sizeof *req, mem_flags);
	if (!req)
		return 0;
	memset (req, 0, sizeof *req);
	INIT_LIST_HEAD (&req->queue);
	req->req.dma = DMA_ADDR_INVALID;

	//printk("req=0x%x, buf0x%x\n", (int)req, (int)req->req.buf);
	return &req->req;
}

static void n329_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct n329_ep	*ep;
	struct n329_request	*req;

	//printk("n329_free_request(ep=%p,req=%p)\n", _ep, _req);

	ep = container_of (_ep, struct n329_ep, ep);
	if (!ep || !_req || (!ep->desc && _ep->name != ep0name))
		return;

	req = container_of (_req, struct n329_request, req);

	list_del_init(&req->queue);

	WARN_ON (!list_empty (&req->queue));
	kfree (req);
}


static int n329_queue(struct usb_ep *_ep, struct usb_request *_req, 
				gfp_t gfp_flags)
{
	struct n329_request	*req;
	struct n329_ep	*ep;
	struct n329_udc	*dev;
	unsigned long flags;

	local_irq_save(flags);

	req = container_of(_req, struct n329_request, req);

	if (unlikely (!_req || !_req->complete || !_req->buf
			|| !list_empty(&req->queue))) {
		if (!_req) {
			printk("n329_queue: 1 X X X\n");
		} else {
			printk("n329_queue: 0 %01d %01d %01d\n",!_req->complete,!_req->buf, !list_empty(&req->queue));
		}
		local_irq_restore(flags);
		return -EINVAL;
	}

	ep = container_of(_ep, struct n329_ep, ep);
	if (unlikely (!_ep || (!ep->desc && ep->ep.name != ep0name))) {
		printk("n329_queue: inval 2\n");
		local_irq_restore(flags);
		return -EINVAL;
	}

	dev = ep->dev;
	if (unlikely (!dev->driver
			|| dev->gadget.speed == USB_SPEED_UNKNOWN)) {
		local_irq_restore(flags);
		printk("n329_queue: speed =%d\n",dev->gadget.speed);
		return -ESHUTDOWN;
	}

	/* iso is always one packet per request, that's the only way
	 * we can report per-packet status.  that also helps with dma.
	 */
	if (ep->desc) { //clyu
		if (unlikely (ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC
				&& req->req.length > le16_to_cpu
				(ep->desc->wMaxPacketSize))) {
			local_irq_restore(flags);
			return -EMSGSIZE;
		}
	}

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* pio or dma irq handler advances the queue. */
	if (likely (req != 0)) {
		list_add_tail(&req->queue, &ep->queue);
	}

	if (ep->index==0) { //delayed status
		if (dev->setup_ret > 1000||
				((req->req.length==0)&&(dev->ep0state == EP0_OUT_DATA_PHASE))) {
			printk("delayed status done\n");
			n329_udc_write(CEP_NAK_CLEAR, REG_USBD_CEP_CTRL_STAT);	// clear nak so that sts stage is complete
			n329_udc_write(0x402, REG_USBD_CEP_IRQ_ENB);		// suppkt int//enb sts completion int
			n329_udc_done(ep, req, 0);
		}
	} else if (ep->index > 0) {
		if (ep->EP_Dir) { //IN
			if (!dev->usb_dma_trigger || (ep->index!=dev->usb_dma_owner)) {
				n329_udc_write(ep->irq_enb, REG_USBD_EPA_IRQ_ENB + 0x28*(ep->index-1));
			}
		} else { //OUT
			if (!dev->usb_dma_trigger || (ep->index!=dev->usb_dma_owner))
				n329_udc_write(ep->irq_enb, REG_USBD_EPA_IRQ_ENB + 0x28*(ep->index-1));
		}
	}

	local_irq_restore(flags);

	return 0;
}

static int n329_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct n329_ep	*ep;
	struct n329_udc	*udc = &controller;
	int			retval = -EINVAL;
	unsigned long		flags;
	struct n329_request	*req;

	printk("n329_dequeue(ep=%p,req=%p)\n", _ep, _req);

	if (!udc->driver)
		return -ESHUTDOWN;

	if (!_ep || !_req)
		return retval;
	ep = container_of (_ep, struct n329_ep, ep);
	udc = container_of (ep->gadget, struct n329_udc, gadget);

	spin_lock_irqsave (&udc->lock, flags);
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init (&req->queue);
			_req->status = -ECONNRESET;
			retval = 0;
			break;
		}
	}
	spin_unlock_irqrestore (&udc->lock, flags);
	printk("dequeue: %d, req %p\n", retval,  &req->req);
	if (retval == 0) {
		printk( "dequeued req %p from %s, len %d buf %p\n",
			req, _ep->name, _req->length, _req->buf);

		_req->complete (_ep, _req);
		n329_udc_done(ep, req, -ECONNRESET);
	}

	return retval;
}

static int n329_set_halt (struct usb_ep *_ep, int value)
{
	/* Do nothing */
	printk("set halt\n");
	return 0;
}

static const struct usb_ep_ops n329_ep_ops = {
	.enable = n329_ep_enable,
	.disable = n329_ep_disable,

	.alloc_request = n329_alloc_request,
	.free_request = n329_free_request,

	.queue = n329_queue,
	.dequeue = n329_dequeue,

	.set_halt = n329_set_halt,
};

static int n329_get_frame (struct usb_gadget *_gadget)
{
	int tmp;

	tmp = n329_udc_read(REG_USBD_FRAME_CNT);

	return tmp & 0xffff;
}

static int n329_wakeup (struct usb_gadget *_gadget)
{
	/* Do nothing */
	return 0;
}

static int n329_set_selfpowered (struct usb_gadget *_gadget, int value)
{
	/* Do nothing */
	return 0;
}

static const struct usb_gadget_ops n329_ops = {
	.wakeup = n329_wakeup,
	.get_frame = n329_get_frame,
	.set_selfpowered = n329_set_selfpowered,
};


static void n329_nop_release(struct device *dev)
{
	/* Do nothing */
}

int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	struct n329_udc *udc = &controller;
	int retval;

//      printk("usb_gadget_register_driver() '%s'\n", driver->driver.name);

	if (!udc)
		return -ENODEV;

	if (udc->driver)
		return -EBUSY;
	if (!driver->bind || !driver->unbind || !driver->setup
			|| driver->speed == USB_SPEED_UNKNOWN)
		return -EINVAL;
	printk("driver->speed=%d\n", driver->speed);
	udc->gadget.name = gadget_name;
	udc->gadget.ops = &n329_ops;
	udc->gadget.is_dualspeed = 1;
	udc->gadget.speed = USB_SPEED_HIGH;//USB_SPEED_FULL;
	udc->ep0state = EP0_IDLE;

	udc->gadget.dev.release = n329_nop_release;

	udc->driver = driver;

	udc->gadget.dev.driver = &driver->driver;

	printk( "binding gadget driver '%s'\n", driver->driver.name);
	if ((retval = driver->bind (&udc->gadget)) != 0) {
		printk("bind fail\n");
		udc->driver = 0;
		udc->gadget.dev.driver = 0;
		return retval;
	}

#if 0
	driver->driver.bus = udc->gadget.dev.parent->bus;
	driver_register (&driver->driver);
	device_bind_driver (&udc->gadget.dev);
#endif

	mdelay(300);
	
      /* ????? */    n329_udc_write(0x320, REG_USBD_PHY_CTL);//power on usb D+ high
	return 0;
}

int usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
	struct n329_udc *udc = &controller;

	if (!udc)
		return -ENODEV;

	if (!driver || driver != udc->driver)
		return -EINVAL;

	printk("usb_gadget_unregister_driver() '%s'\n",
	       driver->driver.name);

	printk("call driver->unbind\n");
	driver->unbind (&udc->gadget);
	udc->driver = 0;

	printk("device_release_driver\n");
#if 0
	device_release_driver (&udc->gadget.dev);
	driver_unregister (&driver->driver);
#endif
    /* ????? */
    /* Power off usb D+ high */
    n329_udc_write(n329_udc_read(REG_USBD_PHY_CTL) & ~0x100, 
    				REG_USBD_PHY_CTL);


	return 0;
}

static void udc_isr_rst(struct n329_udc	*dev)
{
	int i;

	/* Clear the endpoint states */
	for (i = 0; i < N329_ENDPOINTS; i++)
		n329_udc_nuke(dev, &dev->ep[i]);


	// Reset DMA
	n329_udc_write(0x80, REG_USBD_DMA_CTRL_STS);
	n329_udc_write(0x00, REG_USBD_DMA_CTRL_STS);

	/* Default state */
	dev->usb_devstate = 1;
	dev->usb_address = 0;
	dev->usb_less_mps = 0;

	printk("speed:%x\n", n329_udc_read(REG_USBD_OPER));
	
	if (n329_udc_read(REG_USBD_OPER) == 2)
		dev->gadget.speed = USB_SPEED_FULL;
	else
		dev->gadget.speed = USB_SPEED_HIGH;

	/* Flush fifo */
	n329_udc_write(n329_udc_read(REG_USBD_CEP_CTRL_STAT)|CEP_FLUSH,
		     REG_USBD_CEP_CTRL_STAT);
	for (i = 1; i < N329_ENDPOINTS; i++) {
		n329_udc_write(0x09, REG_USBD_EPA_RSP_SC + 0x28 * (i - 1));
	}

	n329_udc_write(0, REG_USBD_ADDR);
	n329_udc_write(0x002, REG_USBD_CEP_IRQ_ENB);
}

static void udc_isr_dma(struct n329_udc *dev)
{
	struct n329_request	*req;
	struct n329_ep	*ep;
	u32 datacnt_reg;

	if (!dev->usb_dma_trigger) {
		printk("DMA not trigger, intr?\n");
		return;
	}

	ep = &dev->ep[dev->usb_dma_owner];

	datacnt_reg = (u32)(REG_USBD_EPA_DATA_CNT+0x28*(ep->index-1));

	if (dev->usb_dma_dir == Ep_In) {
		n329_udc_write(0x40, REG_USBD_EPA_IRQ_STAT + 0x28*(ep->index-1));
	}

	dev->usb_dma_trigger = 0;


	if (list_empty(&ep->queue)) {
		printk("DMA ep->queue is empty\n");
		req = 0;
		n329_udc_write(dev->irq_enbl, REG_USBD_IRQ_ENB_L);
		return;
	} else {
		req = list_entry(ep->queue.next, struct n329_request, queue);
		//printk("req = %x\n", req);
	}

	if (req) {
		if (ep->EP_Type == EP_TYPE_BLK) {
			if (dev->usb_less_mps == 1) {
				n329_udc_write((n329_udc_read(REG_USBD_EPA_RSP_SC+0x28*(ep->index-1))&0xF7)|0x40,
					     REG_USBD_EPA_RSP_SC+0x28*(ep->index-1)); // packet end
				dev->usb_less_mps = 0;
			}
		} else if (ep->EP_Type == EP_TYPE_INT) {
			n329_udc_write(dev->usb_dma_cnt, REG_USBD_EPA_TRF_CNT+0x28*(ep->index-1));
		}
		req->req.actual += dev->usb_dma_cnt;
		if ((req->req.length == req->req.actual) || dev->usb_dma_cnt < ep->ep.maxpacket) {
			n329_udc_write(dev->irq_enbl, REG_USBD_IRQ_ENB_L);
			if ((ep->EP_Type == EP_TYPE_BLK) &&
					(ep->EP_Dir == 0) && //OUT
					dev->usb_dma_cnt < ep->ep.maxpacket) {
				if (ep->buffer_disabled) {
					n329_udc_write((n329_udc_read(REG_USBD_EPA_RSP_SC + 0x28*(ep->index-1)))&0x77,
						     REG_USBD_EPA_RSP_SC + 0x28*(ep->index-1));//enable buffer
					n329_udc_write((n329_udc_read(REG_USBD_EPA_RSP_SC+0x28*(ep->index-1))&0xF7)|0x80,
						     REG_USBD_EPA_RSP_SC+0x28*(ep->index-1));//disable buffer when short packet
				}
			}

			n329_udc_done(ep, req, 0);

			return;
		}
	}

	if (dev->usb_dma_dir == Ep_Out) {
		if (dev->usb_dma_trigger_next) {
			dev->usb_dma_trigger_next = 0;
			printk("dma out\n");
			n329_udc_read_fifo(ep, req, 0);
		}
	}

	else if (dev->usb_dma_dir == Ep_In) {
		if (dev->usb_less_mps == 1) {
			dev->usb_less_mps = 0;

		}

		if (dev->usb_dma_trigger_next) {
			dev->usb_dma_trigger_next = 0;
			printk("dma in\n");
			n329_udc_write_fifo(ep, req);
		}
	}
}


static void udc_isr_ctrl_pkt(struct n329_udc *dev)
{
	u32	temp;
	u32	ReqErr=0;
	struct n329_ep *ep = &dev->ep[0];
	struct usb_ctrlrequest	crq;
	struct n329_request	*req;
	int ret;
	if (list_empty(&ep->queue)) {
		//printk("ctrl ep->queue is empty\n");
		req = 0;
	} else {
		req = list_entry(ep->queue.next, struct n329_request, queue);
		//printk("req = %x\n", req);
	}

	temp = n329_udc_read(REG_USBD_SETUP1_0);

	pcrq.bRequest = (u8) ((temp >> 8) & 0xff);
	pcrq.bRequestType = (u8) (temp & 0xff);
	pcrq.wValue = (u16) n329_udc_read(REG_USBD_SETUP3_2);
	pcrq.wIndex = (u16) n329_udc_read(REG_USBD_SETUP5_4);
	pcrq.wLength = (u16) n329_udc_read(REG_USBD_SETUP7_6);

	dev->crq = crq;

	switch (dev->ep0state) {
	case EP0_IDLE:
		switch (crq.bRequest) {

		case USBR_SET_ADDRESS:
			ReqErr = ((crq.bRequestType == 0) && ((crq.wValue & 0xff00) == 0)
				  && (crq.wIndex == 0) && (crq.wLength == 0)) ? 0 : 1;

			if ((crq.wValue & 0xffff) > 0x7f) {	//within 7f
				ReqErr=1;	//Devaddr > 127
			}

			if (dev->usb_devstate == 3) {
				ReqErr=1;	//Dev is configured
			}

			if (ReqErr==1) {
				break;		//break this switch loop
			}

			if (dev->usb_devstate == 2) {
				if (crq.wValue == 0)
					dev->usb_devstate = 1;		//enter default state
				dev->usb_address = crq.wValue;	//if wval !=0,use new address
			}

			if (dev->usb_devstate == 1) {
				if (crq.wValue != 0) {
					dev->usb_address = crq.wValue;
					dev->usb_devstate = 2;
				}
			}

			break;

		case USBR_SET_CONFIGURATION:
			ReqErr = ((crq.bRequestType == 0) && ((crq.wValue & 0xff00) == 0) &&
				  ((crq.wValue & 0x80) == 0) && (crq.wIndex == 0) &&
				  (crq.wLength == 0)) ? 0 : 1;

			if (dev->usb_devstate == 1) {
				ReqErr=1;
			}

			if (ReqErr==1) {
				break;	//break this switch loop
			}

			if (crq.wValue == 0)
				dev->usb_devstate = 2;
			else
				dev->usb_devstate = 3;
			break;

		case USBR_SET_INTERFACE:
			ReqErr = ((crq.bRequestType == 0x1) && ((crq.wValue & 0xff80) == 0)
				  && ((crq.wIndex & 0xfff0) == 0) && (crq.wLength == 0)) ? 0 : 1;

			if (!((dev->usb_devstate == 0x3) && (crq.wIndex == 0x0) && (crq.wValue == 0x0)))
				ReqErr=1;

			if (ReqErr == 1) {
				break;	//break this switch loop
			}

			break;

		default:
			break;
		}//switch end

		if (crq.bRequestType & USB_DIR_IN) {
			dev->ep0state = EP0_IN_DATA_PHASE;
			n329_udc_write(0x08, REG_USBD_CEP_IRQ_ENB);
		} else {
			dev->ep0state = EP0_OUT_DATA_PHASE;
			n329_udc_write(0x40, REG_USBD_CEP_IRQ_ENB);
		}

		ret = dev->driver->setup(&dev->gadget, &crq);
		dev->setup_ret = ret;
		if (ret < 0) {

			n329_udc_write(0x400, REG_USBD_CEP_IRQ_STAT);
			n329_udc_write(0x448, REG_USBD_CEP_IRQ_ENB);		// enable in/RxED/status complete interrupt
			n329_udc_write(CEP_NAK_CLEAR, REG_USBD_CEP_CTRL_STAT);	//clear nak so that sts stage is complete


			if (ret == -EOPNOTSUPP)
				printk("Operation %x not supported\n", crq.bRequest);
			else {
				printk("dev->driver->setup failed. (%d)\n",ret);
			}
		} else if (ret > 1000) { //DELAYED_STATUS
			printk("DELAYED_STATUS:%p\n", req);
			dev->ep0state = EP0_END_XFER;
			n329_udc_write(0, REG_USBD_CEP_IRQ_ENB);
		}

		break;

	case EP0_STALL:
		break;
	default:
		break;
	}

	if (ReqErr == 1) {
		n329_udc_write(CEP_SEND_STALL, REG_USBD_CEP_CTRL_STAT);
		dev->ep0state = EP0_STALL;
	}

}

void udc_isr_update_dev(struct n329_udc *dev)
{
	struct usb_ctrlrequest	*pcrq = &dev->crq;

	//update this device for set requests
	// TODO: write me!!
	switch (pcrq->bRequest) {
	case USBR_SET_ADDRESS:
		n329_udc_write(dev->usb_address, REG_USBD_ADDR);
		break;

	case USBR_SET_CONFIGURATION:
		break;

	case USBR_SET_INTERFACE:
		break;

	case USBR_SET_FEATURE:
#if 0
		for (i = 1; i < N329_ENDPOINTS; i++) {
			if (dev->ep[i].EP_Num == dev->usb_haltep) {
				index = i;
				break;
			}
		}
		if (dev->usb_haltep == 0)
			USB_WRITE(REG_USBD_CEP_CTRL_STAT, CEP_SEND_STALL);
		else if (index)
			USB_WRITE(REG_USBD_EPA_RSP_SC + 0x28*(dev->ep[index].index-1), EP_HALT);
		else if (dev->usb_enableremotewakeup == 1) {
			dev->usb_enableremotewakeup = 0;
			dev->usb_remotewakeup = 1;
		}
#endif
		break;

	case USBR_CLEAR_FEATURE:
#if 0
		if (dev->usb_unhaltep == 1 && dev->usb_haltep == 1) {
			USB_WRITE(REG_USBD_EPA_RSP_SC, 0x0);
			USB_WRITE(REG_USBD_EPA_RSP_SC, EP_TOGGLE);
			dev->usb_haltep = 4; // just for changing the haltep value
		}
		if (dev->usb_unhaltep == 2 && dev->usb_haltep == 2) {
			USB_WRITE(REG_USBD_EPB_RSP_SC, 0x0);
			USB_WRITE(REG_USBD_EPB_RSP_SC, EP_TOGGLE);
			dev->usb_haltep = 4; // just for changing the haltep value
		} else if (dev->usb_disableremotewakeup == 1) {
			dev->usb_disableremotewakeup=0;
			dev->usb_remotewakeup=0;
		}
#endif
		break;

	default:
		break;
	}//switch end

	return;
}


static void USB_Init(struct n329_udc *dev)
{
	int	j;

	dev->usb_devstate=0;
	dev->usb_address = 0;

	/*
	 * configure USB controller
	 */
	n329_udc_write(0x03, REG_USBD_IRQ_ENB_L);	/* enable usb, cep interrupt */
	n329_udc_write((USB_RESUME | USB_RST_STS| USB_VBUS_STS), REG_USBD_IRQ_ENB);

	n329_udc_write(USB_HS, REG_USBD_OPER);//USB 2.0

	n329_udc_write(0, REG_USBD_ADDR);
	n329_udc_write((CEP_SUPPKT | CEP_STS_END), REG_USBD_CEP_IRQ_ENB);

	for (j = 0; j < N329_ENDPOINTS; j++) {
		dev->ep[j].EP_Num = 0xff;
		dev->ep[j].EP_Dir = 0xff;
		dev->ep[j].EP_Type = 0xff;

	}

}

static u32 udc_transfer(struct n329_ep *ep, u8* buf, size_t size, u32 mode)
{
	struct n329_udc	*dev = ep->dev;
	unsigned int volatile count=0;
	int volatile loop,len=0;

	loop = size / USBD_DMA_LEN;

	if (mode == DMA_WRITE) {
		while (!(n329_udc_read(REG_USBD_EPA_IRQ_STAT + (0x28* (ep->index-1))) & 0x02));
		{
			dev->usb_dma_dir = Ep_In;
			dev->usb_less_mps = 0;
			n329_udc_write(0x03, REG_USBD_IRQ_ENB_L);


			n329_udc_write((n329_udc_read(REG_USBD_DMA_CTRL_STS)&0xe0) | 0x10 | ep->EP_Num,
				     REG_USBD_DMA_CTRL_STS);// bulk in, write

			n329_udc_write(0, REG_USBD_EPA_IRQ_ENB + (0x28* (ep->index-1)));

			if (loop > 0) {
				loop--;
				if (loop > 0)
					dev->usb_dma_trigger_next = 1;
				n329_udc_start_write(ep, buf, USBD_DMA_LEN);
				//len = USBD_DMA_LEN;
			} else {
				if (size >= ep->ep.maxpacket) {
					count = size/ep->ep.maxpacket;
					count *= ep->ep.maxpacket;

					if (count < size)
						dev->usb_dma_trigger_next = 1;
					n329_udc_start_write(ep, buf, count);
					//len = count;
				} else {
					if (ep->EP_Type == EP_TYPE_BLK)
						dev->usb_less_mps = 1;
					n329_udc_start_write(ep, buf, size);
					//len = size;
				}
			}
		}
	} else if (mode == DMA_READ) {
		dev->usb_dma_dir = Ep_Out;
		dev->usb_less_mps = 0;
		//dev->irq_enbl = n329_udc_read(REG_USBD_IRQ_ENB_L);
		n329_udc_write(0x03, REG_USBD_IRQ_ENB_L);

		n329_udc_write((n329_udc_read(REG_USBD_DMA_CTRL_STS) & 0xe0)|ep->EP_Num,
			     REG_USBD_DMA_CTRL_STS);	//read
		n329_udc_write(0x1000,  REG_USBD_EPA_IRQ_ENB + (0x28* (ep->index-1)));
		n329_udc_write(n329_udc_read( REG_USBD_IRQ_ENB_L)|(ep->index<<2),
			     REG_USBD_IRQ_ENB_L);

		if (loop > 0) {
			loop--;
			if (loop > 0)
				dev->usb_dma_trigger_next = 1;
			n329_udc_start_read(ep, buf, USBD_DMA_LEN);
			//len = USBD_DMA_LEN;
		} else {
			if (size >= ep->ep.maxpacket) {
				count = size/ep->ep.maxpacket;
				count *= ep->ep.maxpacket;
				if (count < size)
					dev->usb_dma_trigger_next = 1;
				n329_udc_start_read(ep, buf, count);
				//len = count;
			} else {
				//using short packet intr to deal with
				n329_udc_start_read(ep, buf, size);
				//len = size;
			}
		}
	}

	return len;
}



/*
 *	probe - binds to the platform device
 */
static int /*__init*/ n329_udc_probe(struct platform_device *pdev)
{
	struct n329_udc *udc = &controller;
	struct device *dev = &pdev->dev;
	int error, i;
	dev_dbg(dev, "%s()\n", __func__);

	printk("<--n329_udc_probe-->\n");
	udc->pdev = pdev;
	udc->gadget.dev.parent = &pdev->dev;
	udc->gadget.dev.dma_mask = pdev->dev.dma_mask;

       udc->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(udc->clk)) {
		dev_err(dev, "failed to get udc clock\n");
		error = PTR_ERR(udc->clk);
		goto fail1;
	}
	clk_enable(udc->clk);
	//n329_udc_write(n329_udc_read(REG_AHBCLK) | USBD_CKE, REG_AHBCLK);
	//n329_udc_write(n329_udc_read(REG_AHBIPRST) | UDCRST, REG_AHBIPRST);
	//n329_udc_write(n329_udc_read(REG_AHBIPRST) & ~UDCRST, REG_AHBIPRST);
	//n329_udc_write(n329_udc_read((REG_USBD_PHY_CTL) | (0x20 | Phy_suspend), REG_USBD_PHY_CTL);    // offset 0x704


	udc->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (udc->res == NULL) {
		dev_err(dev, "failed to get I/O memory\n");
		error = -ENXIO;
		goto fail1;
	}

	if (!request_mem_region(udc->res->start,
				resource_size(udc->res), pdev->name)) {
		dev_err(dev, "failed to request I/O memory\n");
		error = -EBUSY;
		goto fail1;
	}

	udc->reg = ioremap(udc->res->start, resource_size(udc->res));
	if (udc->reg == NULL) {
		dev_err(dev, "failed to remap I/O memory\n");
		error = -ENXIO;
		goto fail1;
	}

	device_initialize(&udc->gadget.dev);
	dev_set_name(&udc->gadget.dev, "gadget");
	udc->gadget.dev.parent = dev;
#if 0
	error = device_register (&udc->gadget.dev);
	if (error < 0) {
		dev_err(dev, "device_register() fail\n");
		goto fail2;
	}
#endif

	platform_set_drvdata (pdev, udc);

	spin_lock_init (&udc->lock);

	n329_udc_write(0x220, REG_USBD_PHY_CTL);

	// FIXME: is it possible to loop forever?
	while (1) {
		n329_udc_write(0x20, REG_USBD_EPA_MPS);
		if (n329_udc_read(REG_USBD_EPA_MPS) == 0x20)
			break;
	}
	USB_Init(udc);

	/* setup endpoint information */
	INIT_LIST_HEAD (&udc->gadget.ep_list);
	for (i = 0; i < N329_ENDPOINTS; i++) {
		struct n329_ep *ep = &udc->ep[i];

	      //  printk("ep %d\n", i);
		if (!ep_name[i])
			break;
		ep->index = i;
		ep->ep.name = ep_name[i];
		ep->ep.ops = &n329_ep_ops;
		list_add_tail (&ep->ep.ep_list, &udc->gadget.ep_list);

		/* maxpacket differs between ep0 and others ep */
		if (!i) {
			ep->EP_Num = 0;
			ep->ep.maxpacket = EP0_FIFO_SIZE;
			n329_udc_write(0x00000000, REG_USBD_CEP_START_ADDR);
			n329_udc_write(0x0000003f, REG_USBD_CEP_END_ADDR);
		} else {
			ep->ep.maxpacket = EP_FIFO_SIZE;
			//sram_addr = udc->ep[0].ep.maxpacket + EP_FIFO_SIZE * (i - 1);
			//printk("sram_addr=%x\n", sram_addr);
			n329_udc_write(0, REG_USBD_EPA_START_ADDR+0x28*(ep->index-1));
			n329_udc_write(0, REG_USBD_EPA_END_ADDR+0x28*(ep->index-1));
		}
		ep->gadget = &udc->gadget;
		ep->dev = udc;
		ep->desc = 0;
		INIT_LIST_HEAD (&ep->queue);
	}

	udc->gadget.ep0 = &udc->ep[0].ep;
	list_del_init (&udc->ep[0].ep.ep_list);

	udc->irq = platform_get_irq(pdev, 0);
	if (udc->irq < 0) {
		dev_err(dev, "Failed to get irq\n");
		error = -ENXIO;
		goto fail2;
	}
	error = request_irq(udc->irq, n329_udc_irq,
			    IRQF_DISABLED, gadget_name, udc);
	if (error != 0) {
		dev_err(dev, "request_irq() failed\n");
		goto fail2;
	}
	init_timer(&usbd_timer);

	usbd_timer.function = timer_check_usbd_access;	/* timer handler */

	error = device_add(&udc->gadget.dev);
	if (error != 0) {
		dev_err(dev, "device_add() failed\n");
		goto fail3;
	}
	return 0;
fail3:
	free_irq(udc->irq, udc);
fail2:
	iounmap(udc->reg);
fail1:
	return error;
}

/*
 * 	n329_udc_remove
 */
static int __exit n329_udc_remove(struct platform_device *pdev)
{
	struct n329_udc *udc = platform_get_drvdata (pdev);
	dev_dbg(&pdev->dev, "%s()\n", __func__);

	free_irq(udc->irq, udc);
	iounmap(udc->reg);

	platform_set_drvdata (pdev, NULL);
	device_unregister (&udc->gadget.dev);

      //n329_udc_write(n329_udc_read(REG_USBD_PHY_CTL) & ~0x200,
      //             REG_USBD_PHY_CTL);    // phy suspend
      n329_udc_write(0x20, REG_USBD_PHY_CTL);    // phy suspend
	clk_disable(udc->clk);

	return 0;
}

#ifdef CONFIG_PM
static int n329_udc_suspend (struct platform_device *pdev, pm_message_t state)
{
	// TODO:
	return 0;
}

static int n329_udc_resume (struct platform_device *pdev)
{
	// TODO:
	return 0;
}
#else
#define n329_udc_suspend     NULL
#define n329_udc_resume      NULL
#endif

static struct platform_driver udc_driver = {
	.probe = n329_udc_probe,
	.remove	= __exit_p(n329_udc_remove),
	.suspend = n329_udc_suspend,
	.resume = n329_udc_resume,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= (char *) "w55fa93-usbgadget",

	},
};

//insmod g_mass_storage.ko file=/dev/mmcblk0p1 stall=0 removable=1
static int __init udc_init(void)
{
	return platform_driver_register(&udc_driver);
}

static void __exit udc_exit(void)
{
	platform_driver_unregister (&udc_driver);
}

static void timer_check_usbd_access(unsigned long dummy)
{	
	if (g_usbd_access == 0)
	{
		printk("<USBD - Ejected by Host/No Transfer from Host>\n");	
		usb_eject_flag = 1;
		g_usbd_access = 0;
	}
	else
	{
		g_usbd_access = 0;
		mod_timer(&usbd_timer, jiffies + USBD_INTERVAL_TIME); 
	}
	return;
}







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
