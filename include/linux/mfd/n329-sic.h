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
 
#ifndef __N329_SIC_H
#define __N329_SIC_H

#include <linux/device.h>
#include <linux/types.h>
#include <linux/mfd/core.h>

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

/* Flash Memory Interface Registers */
#define REG_FMICR		(0x800)		/* FMI Control Register */
	#define	FMI_SM_EN	BIT(3)		/* Enable FMI SM function */
	#define FMI_SD_EN	BIT(1)		/* Enable FMI SD function */
	#define FMI_SWRST	BIT(0)		/* Enable FMI software reset */

#define REG_FMIIER		(0x804)   	/* FMI DMA transfer starting address register */
	#define	FMI_DAT_IE	BIT(0)		/* Enable DMAC READ/WRITE targe abort interrupt generation */

#define REG_FMIISR		(0x808)   	/* FMI DMA byte count register */
	#define	FMI_DAT_IF	BIT(0)		/* DMAC READ/WRITE targe abort interrupt flag register */

/* Secure Digital Registers */
#define REG_SDCR		(0x820)   	/* SD Control Register */
	#define	SDCR_CLK_KEEP1	BIT(31)		/* SD-1 clock keep control */
	#define	SDCR_SDPORT	BITS(30,29)	/* SD port select */
	#define	SDCR_SDPORT_0	0		/* SD-0 port selected */
	#define	SDCR_SDPORT_1	BIT(29)		/* SD-1 port selected */
	#define	SDCR_SDPORT_2	BIT(30)		/* SD-2 port selected */
	#define	SDCR_CLK_KEEP2	BIT(28)		/* SD-1 clock keep control */
	#define	SDCR_SDNWR	BITS(27,24)	/* Nwr paramter for block write operation */
	#define SDCR_BLKCNT	BITS(23,16)	/* Block count to be transferred or received */
	#define	SDCR_DBW	BIT(15)		/* SD data bus width selection */
	#define	SDCR_SWRST	BIT(14)		/* Enable SD software reset */
	#define	SDCR_CMD_CODE	BITS(13,8)	/* SD command code */
	#define	SDCR_CLK_KEEP	BIT(7)		/* SD clock enable */
	#define SDCR_8CLK_OE	BIT(6)		/* 8 clock cycles output enable */
	#define SDCR_74CLK_OE	BIT(5)		/* 74 clock cycle output enable */
	#define SDCR_R2_EN	BIT(4)		/* Response R2 input enable */
	#define SDCR_DO_EN	BIT(3)		/* Data output enable */
	#define SDCR_DI_EN	BIT(2)		/* Data input enable */
	#define SDCR_RI_EN	BIT(1)		/* Response input enable */
	#define SDCR_CO_EN	BIT(0)		/* Command output enable */

#define REG_SDARG 		(0x824)   	/* SD command argument register */

#define REG_SDIER		(0x828)   	/* SD interrupt enable register */
	#define	SDIER_CDSRC	BIT(30)		/* SD card detection source selection: SD-DAT3 or GPIO */
	#define	SDIER_R1B_IEN	BIT(24)		/* R1b interrupt enable */
	#define	SDIER_WKUP_EN	BIT(14)		/* SDIO wake-up signal generating enable */
	#define	SDIER_DITO_IEN	BIT(13)		/* SD data input timeout interrupt enable */
	#define	SDIER_RITO_IEN	BIT(12)		/* SD response input timeout interrupt enable */
	#define SDIER_SDIO_IEN	BIT(10)		/* SDIO interrupt status enable (SDIO issue interrupt via DAT[1] */
	#define SDIER_CD_IEN	BIT(8)		/* CD# interrupt status enable */
	#define SDIER_CRC_IEN	BIT(1)		/* CRC-7, CRC-16 and CRC status error interrupt enable */
	#define SDIER_BLKD_IEN	BIT(0)		/* Block transfer done interrupt interrupt enable */

#define REG_SDISR		(0x82c)   	/* SD interrupt status register */
	#define	SDISR_R1B_IF	BIT(24)		/* R1b interrupt flag */
	#define SDISR_SD_DATA1	BIT(18)		/* SD DAT1 pin status */
	#define SDISR_CD_Card	BIT(16)		/* CD detection pin status */
	#define	SDISR_DITO_IF	BIT(13)		/* SD data input timeout interrupt flag */
	#define	SDISR_RITO_IF	BIT(12)		/* SD response input timeout interrupt flag */
	#define	SDISR_SDIO_IF	BIT(10)		/* SDIO interrupt flag (SDIO issue interrupt via DAT[1] */
	#define	SDISR_CD_IF	BIT(8)		/* CD# interrupt flag */
	#define SDISR_SD_DATA0	BIT(7)		/* SD DATA0 pin status */
	#define SDISR_CRC	BITS(6,4)	/* CRC status */
	#define SDISR_CRC_16	BIT(3)		/* CRC-16 Check Result Status */
	#define SDISR_CRC_7	BIT(2)		/* CRC-7 Check Result Status */
	#define	SDISR_CRC_IF	BIT(1)		/* CRC-7, CRC-16 and CRC status error interrupt status */
	#define	SDISR_BLKD_IF	BIT(0)		/* Block transfer done interrupt interrupt status */

#define REG_SDRSP0		(0x830)   	/* SD receive response token register 0 */
#define REG_SDRSP1		(0x834)   	/* SD receive response token register 1 */
#define REG_SDBLEN		(0x838)   	/* SD block length register */
#define REG_SDTMOUT 		(0x83c)   	/* SD block length register */

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

extern int n329_sic_read(struct device *dev, u32 addr);
extern void n329_sic_write(struct device *dev, u32 value, u32 addr);

#endif
