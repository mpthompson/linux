/* arch/arm/include/debug/n329.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 2014 Michael P. Thompson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/n329/serial.h>

#ifdef CONFIG_DEBUG_UART_PHYS
	.macro	addruart, rp, rv, tmp
	ldr	\rp, =CONFIG_DEBUG_UART_PHYS
	ldr	\rv, =CONFIG_DEBUG_UART_VIRT
	.endm
#endif

	.macro	senduart,rd,rx
	strb	\rd, [\rx, #HW_UA_THR]
	.endm

	.macro	waituart,rd,rx
1001:	ldr	\rd, [\rx, #HW_UA_FSR]
	tst	\rd, #HW_UA_FSR_TX_FULL
	bne	1001b
	.endm

	.macro	busyuart,rd,rx
1001:	ldr	\rd, [\rx, #HW_UA_FSR]
	tst	\rd, #HW_UA_FSR_TE_FLAG
	beq	1001b
	.endm
