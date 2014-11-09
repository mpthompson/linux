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

#ifndef __N329_GCR_H
#define __N329_GCR_H

#include <linux/device.h>
#include <linux/types.h>
#include <linux/mfd/core.h>

#ifndef BITS
#define BITS(start,end)		((0xffffffff >> (31 - start)) & (0xffffffff << end))
#endif

/* System and Global Control Registers */
#define REG_GCR_CHIPID		(0x00)		/* R - Chip Identification Register */
	#define CHIP_VER	BITS(27,24)	/* Chip Version */
	#define CHIP_ID		BITS(23,0)	/* Chip Identification */

#define REG_GCR_CHIPCFG		(0x04)		/* R/W - Chip Power-On Configuration Register */
	#define UDFMODE		BITS(27,24)	/* User-Defined Power-On setting mode */
	#define MAPSDR		BIT(16)		/* Map SDRAM */
	#define USBDEV		BIT(7) 		/* USB Host Selection */
	#define CLK_SRC		BIT(6)		/* System Clock Source Selection */
	#define SDRAMSEL	BITS(5,4)	/* SDRAM Type Selection */
	#define COPMODE		BITS(3,0)	/* Chip Operation Mode Selection */

#define REG_GCR_AHBCTL		(0x10)		/* R/W - AHB Bus Arbitration Control Register */
	#define IPACT		BIT(5) 		/* Interrupt Active Status */
	#define IPEN		BIT(4) 		/* CPU Priority Raising Enable during Interrupt Period */
	#define PRTMOD1		BIT(1) 		/* Priority Mode Control 1 */
	#define PRTMOD0		BIT(0) 		/* Priority Mode Control 0 */

#define REG_GCR_AHBIPRST		(0x14)		/* R/W - AHB IP Reset Control Resister */
	#define JPGRST		BIT(17) 	/* JPG Reset */
	#define BLTRST		BIT(16)		/* 2D Accelerator Reset */
	#define AESRST		BIT(15) 	/* AES Reset */
	#define FSCRST		BIT(14) 	/* FSC Reset */
	#define GE4PRST		BIT(13) 	/* GE4P Reset */
	#define GPURST		BIT(12) 	/* GPU Reset */
	#define CAPRST		BIT(11) 	/* CAP Reset */
	#define VPOSTRST	BIT(10) 	/* VPOST Reset */
	#define I2SRST		BIT(9) 		/* I2S Reset */
	#define SPURST		BIT(8) 		/* SPU Reset */
	#define UHCRST		BIT(7) 		/* UHC Reset */
	#define UDCRST		BIT(6) 		/* UDC Reset */
	#define SICRST		BIT(5) 		/* SIC Reset */
	#define TICRST		BIT(4) 		/* TIC Reset */
	#define EDMARST		BIT(3) 		/* EDMA Reset */
	#define SRAMRST		BIT(2) 		/* SRAM Reset */
	#define SDICRST		BIT(0) 		/* SDIC Reset */

#define REG_GCR_APBIPRST		(0x18)		/* R/W - APB IP Reset Control Resister */
	#define ADCRST		BIT(14)		/* ADC Reset */
	#define SPI1RST		BIT(13) 	/* SPI 1 Reset */
	#define SPI0RST		BIT(12) 	/* SPI 0 Reset */
	#define PWMRST		BIT(10) 	/* PWM Reset */
	#define I2CRST		BIT(8) 		/* I2C Reset */
	#define UART1RST	BIT(7) 		/* UART 1 Reset */
	#define UART0RST	BIT(6) 		/* UART 0 Reset */
	#define TMR1RST		BIT(5) 		/* TMR1 Reset */
	#define TMR0RST		BIT(4) 		/* TMR0 Reset */
	#define WDTRST		BIT(3) 		/* WDT Reset */
	#define RTCRST		BIT(2) 		/* RTC Reset */
	#define GPIORST		BIT(1) 		/* RTC Reset */
	#define AICRST		BIT(0) 		/* AIC Reset */

#define REG_GCR_MISCR		(0x20)		/* R/W - Miscellaneous Control Register */
	#define LVR_RDY		BIT(9) 		/* Low Voltage Reset Function Ready */
	#define LVR_EN		BIT(8) 		/* Low Voltage Reset Function Enable */
	#define CPURSTON	BIT(1) 		/* CPU always keep in reset state for TIC */
	#define CPURST		BIT(0) 		/* CPU one shutte reset. */

 #define REG_GCR_SDRBIST	(0x24)		/* R/W	Power Management Control Register */
	#define TEST_BUSY	BIT(31) 	/* Test BUSY */
	#define CON_BUSY	BIT(30)		/* Connection Test Busy */
	#define SDRBIST_BUSY	BIT(29)		/* BIST Test Busy */
	#define TEST_FAIL	BIT(28) 	/* Test Failed */
	#define CON_FAIL	BIT(27)		/* Connection Test Failed */
	#define SDRBIST_FAIL	BIT(26) 	/* BIST Test Failed */

#define REG_GCR_CRBIST		(0x28)		/* R/W - Cache RAM BIST Control & Status Register */
	#define ICV_F		BIT(29)		/* I-Cache Valid RAM BIST Failed Flag */
	#define ICT_F		BIT(28)		/* I-Cache Tag RAM BIST Failed Flag */
	#define ICD3_F		BIT(27)		/* I-Cache Data RAM 3 BIST Failed Flag */
	#define ICD2_F		BIT(26)		/* I-Cache Data RAM 2 BIST Failed Flag */
	#define ICD1_F		BIT(25)		/* I-Cache Data RAM 1 BIST Failed Flag */
	#define ICD0_F		BIT(24)		/* I-Cache Data RAM 0 BIST Failed Flag */
	#define MMU_F		BIT(23)		/* MMU RAM BIST Failed Flag */
	#define DCDIR_F		BIT(22)		/* D-Cache Dirty RAM BIST Failed Flag */
	#define DCV_F		BIT(21)		/* D-Cache Valid RAM BIST Failed Flag */
	#define DCT_F		BIT(20)		/* D-Cache Tag RAM BIST Failed Flag */
	#define DCD3_F		BIT(19)		/* D-Cache Data RAM 3 BIST Failed Flag */
	#define DCD2_F		BIT(18)		/* D-Cache Data RAM 2 BIST Failed Flag */
	#define DCD1_F		BIT(17)		/* D-Cache Data RAM 1 BIST Failed Flag */
	#define DCD0_F		BIT(16)		/* D-Cache Data RAM 0 BIST Failed Flag */
	#define BISTEN		BIT(15)		/* Cache RAM BIST Test Enable */

	#define ICV_R		BIT(13)		/* I-Cache Valid RAM BIST Running Flag */
	#define ICT_R		BIT(12)		/* I-Cache Tag RAM BIST Running Flag */
	#define ICD3_R		BIT(11)		/* I-Cache Data RAM 3 BIST Running Flag */
	#define ICD2_R		BIT(10)		/* I-Cache Data RAM 2 BIST Running Flag */
	#define ICD1_R		BIT(9)		/* I-Cache Data RAM 1 BIST Running Flag */
	#define ICD0_R		BIT(8)		/* I-Cache Data RAM 0 BIST Running Flag */
	#define MMU_R		BIT(7)		/* MMU RAM BIST Running Flag */
	#define DCDIR_R		BIT(6)		/* D-Cache Dirty RAM BIST Running Flag */
	#define DCV_R		BIT(5)		/* D-Cache Valid RAM BIST Running Flag */
	#define DCT_R		BIT(4)		/* D-Cache Tag RAM BIST Running Flag */
	#define DCD3_R		BIT(3)		/* D-Cache Data RAM 3 BIST Running Flag */
	#define DCD2_R		BIT(2)		/* D-Cache Data RAM 2 BIST Running Flag */
	#define DCD1_R		BIT(1)		/* D-Cache Data RAM 1 BIST Running Flag */
	#define DCD0_R		BIT(0)		/* D-Cache Data RAM 0 BIST Running Flag */

#define REG_GCR_EDSSR		(0x2C)		/* R/W - EDMA Service Selection Control Register */
	#define CH1_RXSEL	BITS(2,0)	/* EDMA Channel 1 Rx Selection */
	#define CH2_RXSEL	BITS(6,4)	/* EDMA Channel 2 Rx Selection */
	#define CH3_RXSEL	BITS(10,8)	/* EDMA Channel 3 Rx Selection */
	#define CH4_RXSEL	BITS(14,12)	/* EDMA Channel 4 Rx Selection */
	#define CH1_TXSEL	BITS(18,16)	/* EDMA Channel 1 Tx Selection */
	#define CH2_TXSEL	BITS(22,20)	/* EDMA Channel 2 Tx Selection */
	#define CH3_TXSEL	BITS(26,24)	/* EDMA Channel 3 Tx Selection */
	#define CH4_TXSEL	BITS(30,28)	/* EDMA Channel 4 Tx Selection */

#define REG_GCR_MISSR		(0x30)		/* R/W - Miscellaneous Status Register */
	#define KPI_WS		BIT(31)		/* KPI Wake-Up Status */
	#define ADC_WS		BIT(30)		/* ADC Wake-Up Status */
	#define UHC_WS		BIT(29)		/* UHC Wake-Up Status */
	#define UDC_WS		BIT(28)		/* UDC Wake-Up Status */
	#define UART_WS		BIT(27)		/* UART Wake-Up Status */
	#define SDH_WS		BIT(26)		/* SDH Wake-Up Status */
	#define RTC_WS		BIT(25)		/* RTC Wake-Up Status */
	#define GPIO_WS		BIT(24)		/* GPIO Wake-Up Status */
	#define KPI_WE		BIT(23)		/* KPI Wake-Up Enable */
	#define ADC_WE		BIT(22)		/* ADC Wake-Up Enable */
	#define UHC_WE		BIT(21)		/* UHC Wake-Up Enable */
	#define UDC_WE		BIT(20)		/* UDC Wake-Up Enable */
	#define UART_WE		BIT(19)		/* UART Wake-Up Enable */
	#define SDH_WE		BIT(18)		/* SDH Wake-Up Enable */
	#define RTC_WE		BIT(17)		/* RTC Wake-Up Enable */
	#define GPIO_WE		BIT(16)		/* GPIO Wake-Up Enable */
	#define CPU_RST		BIT(4)		/* CPU Reset Active Status */
	#define WDT_RST		BIT(3)		/* WDT Reset Active Status */
	#define KPI_RST		BIT(2)		/* KPI Reset Active Status */
	#define LVR_RST		BIT(1)		/* LVR Reset Active Status */
	#define EXT_RST		BIT(0)		/* External Reset Pin Active Status */

#define REG_GCR_OTP_CTRL	(0x40)		/* R/W - OTP Control Register */
	#define OTP_STAT	BITS(25,24)	/* OTP Burned Status */
	#define IBR4_STAT	BITS(23,22)	/* OTP_IBR4 Burned Status */
	#define IBR3_STAT	BITS(21,20)	/* OTP_IBR3 Burned Status */
	#define IBR2_STAT	BITS(19,18)	/* OTP_IBR2 Burned Status */
	#define IBR1_STAT	BITS(17,16)	/* OTP_IBR1 Burned Status */
	#define TEST_OK		BIT(4)		/* MARGIN Read Mode Test OK Flag */
	#define MARGIN		BIT(1)		/* OTP MARGIN Read Mode */
	#define OTPRD_EN	BIT(0)		/* OTP Read Enable */

#define REG_GCR_OTP_PROG		(0x44)		/* R/W - OTP Program Control Register */
	#define BURN_CYC	BITS(29,16)	/* OTP Program Cycle */
	#define OTP_EN		BITS(12,4)	/* OTP Enable */
	#define VPP_STA		BIT(1)		/* VPP State Indicator */
	#define BURN_EN		BIT(0)		/* OTP Program Enable */

#define REG_GCR_OTP_DIS		(0x48)		/* R/W - OTP Disable Register */
	#define CNTRL_DIS	BIT(16)		/* OTP Register Control Disable */

#define REG_GCR_OTP_KEY1	(0x50)		/* R/W - OTP Key 1 Register */

#define REG_GCR_OTP_KEY2	(0x54)		/* R/W - OTP Key 2 Register */

#define REG_GCR_OTP_KEY3	(0x58)		/* R/W - OTP Key 2 Register */

#define REG_GCR_OTP_KEY4	(0x5C)		/* R/W - OTP Key 2 Register */

#define REG_GCR_OTP_IBR1	(0x60)		/* R/W - OTP IBR Option 1 Register */

#define REG_GCR_OTP_IBR2	(0x64)		/* R/W - OTP IBR Option 2 Register */

#define REG_GCR_OTP_IBR3	(0x68)		/* R/W - OTP IBR Option 3 Register */

#define REG_GCR_OTP_IBR4	(0x6C)		/* R/W - OTP IBR Option 4 Register */

#define REG_GCR_OTP_CID		(0x70)		/* R/W - OTP IBR Option 4 Register */
	#define UDOption	BITS(31,8)	/* User Defined Option */
	#define OTP_CHIP_VER	BITS(7,4)	/* Chip version */
	#define CHIP_COD	BITS(29,28)	/* Chip mode */

#define REG_GCR_GPAFUN		(0x80)		/* R/W - Multi Function Pin Control Register 0 */
	#define MF_GPA15	BITS(31,30)	/* GPA[15] Multi Function */
	#define MF_GPA14	BITS(29,28)	/* GPA[14] Multi Function */
	#define MF_GPA13	BITS(27,26)	/* GPA[13] Multi Function */
	#define MF_GPA12	BITS(25,24)	/* GPA[12] Multi Function */
	#define MF_GPA11	BITS(23,22)	/* GPA[11] Multi Function */
	#define MF_GPA10	BITS(21,20)	/* GPA[10] Multi Function */
	#define MF_GPA9		BITS(19,18)	/* GPA[9] Multi Function */
	#define MF_GPA8		BITS(17,16)	/* GPA[8] Multi Function */
	#define MF_GPA7		BITS(15,14)	/* GPA[7] Multi Function */
	#define MF_GPA6		BITS(13,12)	/* GPA[6] Multi Function */
	#define MF_GPA5		BITS(11,10)	/* GPA[5] Multi Function */
	#define MF_GPA4		BITS(9,8)	/* GPA[4] Multi Function */
	#define MF_GPA3		BITS(7,6)	/* GPA[3] Multi Function */
	#define MF_GPA2		BITS(5,4)	/* GPA[2] Multi Function */
	#define MF_GPA1		BITS(3,2)	/* GPA[1] Multi Function */
	#define MF_GPA0		BITS(1,0)	/* GPA[0] Multi Function */

#define REG_GCR_GPBFUN		(0x84)		/* R/W - Multi Function Pin Control Register 0 */
	#define MF_GPB15	BITS(31,30)	/* GPB[15] Multi Function */
	#define MF_GPB14	BITS(29,28)	/* GPB[14] Multi Function */
	#define MF_GPB13	BITS(27,26)	/* GPB[13] Multi Function */
	#define MF_GPB12	BITS(25,24)	/* GPB[12] Multi Function */
	#define MF_GPB11	BITS(23,22)	/* GPB[11] Multi Function */
	#define MF_GPB10	BITS(21,20)	/* GPB[10] Multi Function */
	#define MF_GPB9		BITS(19,18)	/* GPB[9] Multi Function */
	#define MF_GPB8		BITS(17,16)	/* GPB[8] Multi Function */
	#define MF_GPB7		BITS(15,14)	/* GPB[7] Multi Function */
	#define MF_GPB6		BITS(13,12)	/* GPB[6] Multi Function */
	#define MF_GPB5		BITS(11,10)	/* GPB[5] Multi Function */
	#define MF_GPB4		BITS(9,8)	/* GPB[4] Multi Function */
	#define MF_GPB3		BITS(7,6)	/* GPB[3] Multi Function */
	#define MF_GPB2		BITS(5,4)	/* GPB[2] Multi Function */
	#define MF_GPB1		BITS(3,2)	/* GPB[1] Multi Function */
	#define MF_GPB0		BITS(1,0)	/* GPB[0] Multi Function */

#define REG_GCR_GPCFUN		(0x88)		/* R/W - Multi Function Pin Control Register 0 */
	#define MF_GPC15	BITS(31,30)	/* GPC[15] Multi Function */
	#define MF_GPC14	BITS(29,28)	/* GPC[14] Multi Function */
	#define MF_GPC13	BITS(27,26)	/* GPC[13] Multi Function */
	#define MF_GPC12	BITS(25,24)	/* GPC[12] Multi Function */
	#define MF_GPC11	BITS(23,22)	/* GPC[11] Multi Function */
	#define MF_GPC10	BITS(21,20)	/* GPC[10] Multi Function */
	#define MF_GPC9		BITS(19,18)	/* GPC[9] Multi Function */
	#define MF_GPC8		BITS(17,16)	/* GPC[8] Multi Function */
	#define MF_GPC7		BITS(15,14)	/* GPC[7] Multi Function */
	#define MF_GPC6		BITS(13,12)	/* GPC[6] Multi Function */
	#define MF_GPC5		BITS(11,10)	/* GPC[5] Multi Function */
	#define MF_GPC4		BITS(9,8)	/* GPC[4] Multi Function */
	#define MF_GPC3		BITS(7,6)	/* GPC[3] Multi Function */
	#define MF_GPC2		BITS(5,4)	/* GPC[2] Multi Function */
	#define MF_GPC1		BITS(3,2)	/* GPC[1] Multi Function */
	#define MF_GPC0		BITS(1,0)	/* GPC[0] Multi Function */

#define REG_GCR_GPDFUN		(0x8C)		/* R/W - Multi Function Pin Control Register 0 */
	#define MF_GPD15	BITS(31,30)	/* GPD[15] Multi Function */
	#define MF_GPD14	BITS(29,28)	/* GPD[14] Multi Function */
	#define MF_GPD13	BITS(27,26)	/* GPD[13] Multi Function */
	#define MF_GPD12	BITS(25,24)	/* GPD[12] Multi Function */
	#define MF_GPD11	BITS(23,22)	/* GPD[11] Multi Function */
	#define MF_GPD10	BITS(21,20)	/* GPD[10] Multi Function */
	#define MF_GPD9		BITS(19,18)	/* GPD[9] Multi Function */
	#define MF_GPD8		BITS(17,16)	/* GPD[8] Multi Function */
	#define MF_GPD7		BITS(15,14)	/* GPD[7] Multi Function */
	#define MF_GPD6		BITS(13,12)	/* GPD[6] Multi Function */
	#define MF_GPD5		BITS(11,10)	/* GPD[5] Multi Function */
	#define MF_GPD4		BITS(9,8)	/* GPD[4] Multi Function */
	#define MF_GPD3		BITS(7,6)	/* GPD[3] Multi Function */
	#define MF_GPD2		BITS(5,4)	/* GPD[2] Multi Function */
	#define MF_GPD1		BITS(3,2)	/* GPD[1] Multi Function */
	#define MF_GPD0		BITS(1,0)	/* GPD[0] Multi Function */

#define REG_GCR_GPEFUN		(0x90)		/* R/W - Multi Function Pin Control Register 0 */
	#define MF_GPE15	BITS(31,30)	/* GPE[15] Multi Function */
	#define MF_GPE14	BITS(29,28)	/* GPE[14] Multi Function */
	#define MF_GPE13	BITS(27,26)	/* GPE[13] Multi Function */
	#define MF_GPE12	BITS(25,24)	/* GPE[12] Multi Function */
	#define MF_GPE11	BITS(23,22)	/* GPE[11] Multi Function */
	#define MF_GPE10	BITS(21,20)	/* GPE[10] Multi Function */
	#define MF_GPE9		BITS(19,18)	/* GPE[9] Multi Function */
	#define MF_GPE8		BITS(17,16)	/* GPE[8] Multi Function */
	#define MF_GPE7		BITS(15,14)	/* GPE[7] Multi Function */
	#define MF_GPE6		BITS(13,12)	/* GPE[6] Multi Function */
	#define MF_GPE5		BITS(11,10)	/* GPE[5] Multi Function */
	#define MF_GPE4		BITS(9,8)	/* GPE[4] Multi Function */
	#define MF_GPE3		BITS(7,6)	/* GPE[3] Multi Function */
	#define MF_GPE2		BITS(5,4)	/* GPE[2] Multi Function */
	#define MF_GPE1		BITS(3,2)	/* GPE[1] Multi Function */
	#define MF_GPE0		BITS(1,0)	/* GPE[0] Multi Function */

#define REG_GCR_MISFUN		(0x94)		/* R/W - Miscellaneous Multi Function Control Register */
	#define MF_NCS0		BITS(5,4)	/* MF_NCS0_ Multi Function */
	#define MF_EWAIT	BITS(3,2)	/* MF_EWAIT_ Multi Function */
	#define MF_ECS1		BITS(1,0)	/* MF_ECS1_ Multi Function */

#define REG_GCR_MISCPCR		(0xA0)		/* R/W - Miscellaneous Pin Control Register */
	#define SL_MD		BIT(7)		/* MD Pin Slew Rate Control */
	#define SL_MA		BIT(6)		/* MA Pin Slew Rate Control */
	#define SL_MCTL		BIT(5)		/* Memory I/F Control Pin Slew Rate Control */
	#define SL_MCLK		BIT(4)		/* MCLK Pin Rate Control */
	#define DS_MD		BIT(3)		/* MD Pins Driving Strength Control */
	#define DS_MA		BIT(2)		/* MA Pins Driving Strength Control */
	#define DS_MCTL		BIT(1)		/* MCTL Pins Driving Strength Control */
	#define DS_MCLK		BIT(0)		/* MCLK Pins Driving Strength Control */

/* GCR register access functions */
extern int n329_gcr_read(struct device *dev, u32 addr);
extern void n329_gcr_write(struct device *dev, u32 value, u32 addr);

/* GCR protection semaphore */
extern int n329_gcr_down(struct device *dev);
extern void n329_gcr_up(struct device *dev);

#endif
