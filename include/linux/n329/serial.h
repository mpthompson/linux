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
#ifndef ASM_ARM_HARDWARE_SERIAL_N329_H
#define ASM_ARM_HARDWARE_SERIAL_N329_H

#include <linux/types.h>

#define HW_UA_RBR 		0x00	/* R - Receive Buffer Register. */
#define HW_UA_THR 		0x00	/* W - Transmit Holding Register. */
#define HW_UA_IER		0x04	/* R/W - Interrupt Enable Register. */
#define HW_UA_FCR		0x08	/* R/W - FIFO Control Register. */
#define HW_UA_LCR		0x0C	/* R/W - Line Control Register. */
#define HW_UA_MCR		0x10	/* R/W - Modem Control Register. */
#define HW_UA_MSR		0x14	/* R/W - Modem Status Register. */
#define HW_UA_FSR		0x18	/* R/W - FIFO Status Register. */
#define HW_UA_ISR		0x1C	/* R/W - Interrupt Status Register. */
#define HW_UA_TOR		0x20	/* R/W - Time Out Register. */
#define HW_UA_BAUD		0x24	/* R/W - Baud Rate Divider Register. */

#define HW_UA_FSR_TE_FLAG		0x10000000
#define HW_UA_FSR_TX_OVER_IF	0x01000000
#define HW_UA_FSR_TX_FULL		0x00800000
#define HW_UA_FSR_TX_EMPTY		0x00400000
#define HW_UA_FSR_RX_FULL		0x00008000
#define HW_UA_FSR_RX_EMPTY		0x00004000
#define HW_UA_FSR_BII			0x00000040
#define HW_UA_FSR_FEI			0x00000020
#define HW_UA_FSR_PEI			0x00000010
#define HW_UA_FSR_RX_OVER_IF	0x00000001

#endif
