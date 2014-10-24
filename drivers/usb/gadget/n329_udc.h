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

#ifndef __LINUX_USB_GADGET_N329_UDC_H__
#define __LINUX_USB_GADGET_N329_UDC_H__

#define DMA_ADDR_INVALID	(~(dma_addr_t)0)

#define N329_ENDPOINTS		7

enum ep0_state {
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_END_XFER,
	EP0_STALL,
};

struct n329_ep {
	struct usb_gadget *gadget;
	struct list_head queue;
	struct n329_udc *dev;
	const struct usb_endpoint_descriptor *desc;
	struct usb_ep ep;
	u8 index;
	u8 buffer_disabled;
	u8 bEndpointAddress;//w/ direction
	
	u8 EP_Mode;//auto/manual/fly
	u8 EP_Num;//no direction ep address
	u8 EP_Dir;//0 OUT, 1 IN
	u8 EP_Type;//bulk/in/iso
	u32 irq_enb;
};

struct n329_request {
	struct list_head queue;		/* ep's requests */
	struct usb_request req;
	u32 dma_mapped;
};

struct n329_udc {
	spinlock_t lock;
	
	struct n329_ep ep[N329_ENDPOINTS];
	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;
	struct platform_device *pdev;
	
	struct clk *clk;
	struct resource *res;
	void __iomem *reg;
	int irq;
	
	enum ep0_state ep0state;
	
	u8 usb_devstate;
	u8 usb_address;

	u8 usb_dma_dir;
	u8 usb_dma_trigger;
	u8 usb_dma_trigger_next;
	u8 usb_less_mps;
	u32 usb_dma_cnt;
	u32 usb_dma_loop; 
	u32 usb_dma_owner;
	
	struct usb_ctrlrequest crq;
	s32 setup_ret;
	
	u32 irq_enbl;
};

#endif /* __LINUX_USB_GADGET_N329_UDC_H__ */
