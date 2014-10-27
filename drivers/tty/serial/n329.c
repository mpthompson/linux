/*
 * Nuvoton N329XX UART driver
 *
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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>

static struct uart_driver n329_uart_driver;

#define REG_COM_TX       0x00
#define REG_COM_RX       0x00
#define REG_COM_IER      0x04
#define REG_COM_FCR      0x08
#define REG_COM_LCR      0x0C
#define REG_COM_MCR      0x10
#define REG_COM_MSR      0x14
#define REG_COM_FSR      0x18
#define REG_COM_ISR      0x1C
#define REG_COM_TOR      0x20
#define REG_COM_BAUD     0x24

#define UARTx_FCR_FIFO_LEVEL1   0x00
#define UARTx_FCR_FIFO_LEVEL4   0x10
#define UARTx_FCR_FIFO_LEVEL8   0x20
#define UARTx_FCR_FIFO_LEVEL14  0x30
#define UARTx_FCR_FIFO_LEVEL30  0x40
#define UARTx_FCR_FIFO_LEVEL46  0x50
#define UARTx_FCR_FIFO_LEVEL62  0x60

#define UART_FCR_RFR            0x02
#define UART_FCR_TFR            0x04

#define UART_TXRXFIFO_RESET (UART_FCR_RFR | UART_FCR_TFR)

#define UART_FSR_ROE            0x00000000      // Rx Overrun error
#define UART_FSR_PE             0x00000010      // Parity error
#define UART_FSR_FE             0x00000020      // Frame error
#define UART_FSR_BI             0x00000040      // Break interrupt
#define UART_FSR_RFE            0x00004000      // Rx FIFO empty
#define UART_FSR_RFF            0x00008000      // Rx FIFO full
#define UART_FSR_RPMASK         (0x00003F00)    // Rx FIFO pointer
#define UART_FSR_TFE            0x00400000      // Tx FIFO empty
#define UART_FSR_TFF            0x00800000      // Tx FIFO full
#define UART_FSR_TPMASK         (0x003F0000)    // Tx FIFO pointer
#define UART_FSR_TOE            0x01000000      // Tx Overrun error
#define UART_FSR_TEMT           0x10000000      // Transmitter empty

#define UART_FSRSTAT_ANY (UART_FSR_ROE | UART_FSR_TOE | UART_FSR_FE | UART_FSR_BI)

#define UART_LCR_WLEN5          0x00
#define UART_LCR_WLEN6          0x01
#define UART_LCR_WLEN7          0x02
#define UART_LCR_WLEN8          0x03
#define UART_LCR_CSMASK         (0x3)
#define UART_LCR_PARITY         0x08
#define UART_LCR_NPAR           0x00
#define UART_LCR_OPAR           0x00
#define UART_LCR_EPAR           0x10
#define UART_LCR_PMMASK         (0x30)
#define UART_LCR_SPAR           0x20
#define UART_LCR_SBC            0x40
#define UART_LCR_NSB            0x00
#define UART_LCR_NSB1_5         0x04

#define UART_IER_CTS_EN         BIT(13)     // CTS auto flow control enable
#define UART_IER_RTS_EN         BIT(12)     // RTS auto flow control enable
#define UART_IER_TOUT_EN        BIT(11)     // Time output counter enable
#define UART_IER_RTO            BIT(4)      // Receive time out interrupt enable
#define UART_IER_MS             BIT(3)      // Modem status interrupt enable
#define UART_IER_RLS            BIT(2)      // Receive line status interrupt enable
#define UART_IER_THRE           BIT(1)      // Transmit hold register empty interrupt enable
#define UART_IER_RDA            BIT(0)      // Receive data available interrupt enable

#define UART_ISR_EDMA_RX_Flag   BIT(31)     // EDMA RX Mode Flag
#define UART_ISR_HW_Wake_INT    BIT(30)     // Wake up Interrupt pin status
#define UART_ISR_HW_Buf_Err_INT BIT(29)     // Buffer Error Interrupt pin status
#define UART_ISR_HW_Tout_INT    BIT(28)     // Time out Interrupt pin status
#define UART_ISR_HW_Modem_INT   BIT(27)     // MODEM Status Interrupt pin status
#define UART_ISR_HW_RLS_INT     BIT(26)     // Receive Line Status Interrupt pin status
#define UART_ISR_Rx_ack_st      BIT(25)     // TX ack pin status
#define UART_ISR_Rx_req_St      BIT(24)     // TX req pin status
#define UART_ISR_EDMA_TX_Flag   BIT(23)     // EDMA TX Mode Flag
#define UART_ISR_HW_Wake_IF     BIT(22)     // Wake up Flag
#define UART_ISR_HW_Buf_Err_IF  BIT(21)     // Buffer Error Flag
#define UART_ISR_HW_Tout_IF     BIT(20)     // Time out Flag
#define UART_ISR_HW_Modem_IF    BIT(19)     // MODEM Status Flag
#define UART_ISR_HW_RLS_IF      BIT(18)     // Receive Line Status Flag
#define UART_ISR_Tx_ack_st      BIT(17)     // TX ack pin status
#define UART_ISR_Tx_req_St      BIT(16)     // TX req pin status
#define UART_ISR_Soft_RX_Flag   BIT(15)     // Software RX Mode Flag
#define UART_ISR_Wake_INT       BIT(14)     // Wake up Interrupt pin status
#define UART_ISR_Buf_Err_INT    BIT(13)     // Buffer Error Interrupt pin status
#define UART_ISR_Tout_INT       BIT(12)     // Time out interrupt Interrupt pin status
#define UART_ISR_Modem_INT      BIT(11)     // MODEM Status Interrupt pin status
#define UART_ISR_RLS_INT        BIT(10)     // Receive Line Status Interrupt pin status
#define UART_ISR_THRE_INT       BIT(9)      // Transmit Holding Register Empty Interrupt pin status
#define UART_ISR_RDA_INT        BIT(8)      // Receive Data Available Interrupt pin status
#define UART_ISR_Soft_TX_Flag   BIT(7)      // Software TX Mode Flag
#define UART_ISR_Wake_IF        BIT(6)      // Wake up Flag
#define UART_ISR_Buf_Err_IF     BIT(5)      // Buffer Error Flag
#define UART_ISR_Tout_IF        BIT(4)      // Time out interrupt Flag
#define UART_ISR_Modem_IF       BIT(3)      // MODEM Status Flag
#define UART_ISR_RLS_IF         BIT(2)      // Receive Line Status Flag
#define UART_ISR_THRE_IF        BIT(1)      // Transmit Holding Register Empty Flag
#define UART_ISR_RDA_IF         BIT(0)      // Receive Data Available Flag

#define N329_UART_PORTS 2
#define N329_UART_FIFO_SIZE 16         

/* Flag to ignore all characters comming in */
#define RXSTAT_DUMMY_READ (0x10000000)

/* Register access controls */
#define portaddr(s, reg) ((s->port.membase) + (reg))
#define rd_regb(s, reg) (__raw_readb(portaddr(s, reg)))
#define rd_regl(s, reg) (__raw_readl(portaddr(s, reg)))
#define wr_regb(s, reg, val) \
	do { __raw_writeb(val, portaddr(s, reg)); } while(0)
#define wr_regl(s, reg, val) \
	do { __raw_writel(val, portaddr(s, reg)); } while(0)

/* Macros to change one thing to another */
#define tx_enabled(s)   (s->port.unused[0])
#define rx_enabled(s)   (s->port.unused[1])
#define tx_disable(s)   wr_regl(s, REG_COM_IER, rd_regl(s, REG_COM_IER) & ~UART_IER_THRE)
#define tx_enable(s)    wr_regl(s, REG_COM_IER, rd_regl(s, REG_COM_IER) | UART_IER_THRE | UART_IER_RTO | UART_IER_TOUT_EN)
#define rx_disable(s)   wr_regl(s, REG_COM_IER, rd_regl(s, REG_COM_IER) & ~UART_IER_RDA); wr_regl(s, REG_COM_TOR, 0x00)
#define rx_enable(s)    wr_regl(s, REG_COM_IER, rd_regl(s, REG_COM_IER) | UART_IER_RDA | UART_IER_RTO | UART_IER_TOUT_EN); wr_regl(s, REG_COM_TOR, 0x20)

enum n329_uart_type {
	N32905_UART
};

#define N329_UART_FLAGS_RTSCTS  1  /* bit 1 */

struct n329_uart_port {
	struct uart_port    port;

	enum n329_uart_type devtype;

	unsigned long       flags;
	unsigned int        ctrl;
	unsigned char       rx_claimed;
	unsigned char       tx_claimed;

	unsigned int        irq;
	struct clk          *clk;
	struct device       *dev;
};

#define to_n329_uart_port(u) container_of(u, struct n329_uart_port, port)

static struct platform_device_id n329_uart_devtype[] = {
	{ .name = "n329-uart-n32905", .driver_data = N32905_UART },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, n329_uart_devtype);

static struct of_device_id n329_uart_dt_ids[] = {
	{
		.compatible = "nuvoton,n329-uart",
		.data = &n329_uart_devtype[N32905_UART]
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n329_uart_dt_ids);

static void n329_uart_stop_tx(struct uart_port *u);

static irqreturn_t
n329_uart_irq_handler(int irq, void *dev)
{
	struct n329_uart_port *s = dev;
	struct uart_port *u = &s->port;
	struct circ_buf *xmit = &u->state->xmit;
	struct tty_port *tty = &u->state->port;
	unsigned int ch, flag;
	unsigned int isr_reg, fsr_reg; 
	unsigned int max_count, process_character;

	/* Get the interrupt status register */
	isr_reg = rd_regl(s, REG_COM_ISR);

	/* First test for transmit holding register empty */
	if (isr_reg & UART_ISR_THRE_INT)
	{
		/* We can't send more than the size of the fifo */
		int max_count = N329_UART_FIFO_SIZE;

		/* Xon/xoff characters have priority */
		if (u->x_char) {
			wr_regb(s, REG_COM_TX, u->x_char);
			u->icount.tx++;
			u->x_char = 0;
		} else {
			/* Can we transmit? */          
			if (uart_tx_stopped(u))
				n329_uart_stop_tx(u);
			else {
				/* Empty the circular buffer without overflowing the uart */
				while (!uart_circ_empty(xmit) && (max_count-- > 0)) {
					wr_regb(s, REG_COM_TX, xmit->buf[xmit->tail]);
					xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
					u->icount.tx++;
				}

				if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
					uart_write_wakeup(u);

				if (uart_circ_empty(xmit))
					n329_uart_stop_tx(u);
			}
		}
	} else if (isr_reg & UART_ISR_RDA_INT) {
		/* We can't read more than the size of the fifo */
		max_count = N329_UART_FIFO_SIZE;

		while (max_count-- > 0) {
			/* Get fifo status register */
			fsr_reg = rd_regl(s, REG_COM_FSR);
	
			/* Stop if the receive register empty */
			if (fsr_reg & UART_FSR_RFE)
				break;

			/* Get the next character in the fifo */	
			ch = rd_regb(s, REG_COM_RX);

			/* Insert the character into the buffer */
			flag = TTY_NORMAL;
			u->icount.rx++;
			process_character = 1;

			/* Process a break */
			if (fsr_reg & UART_FSR_BI) {
				u->icount.brk++;
				if (uart_handle_break(u))
					process_character = 0;
			}

			/* Process receive errors */
			if (unlikely(fsr_reg & UART_FSRSTAT_ANY)) {
				if (fsr_reg & UART_FSR_FE)
					u->icount.frame++;
				if (fsr_reg & (UART_FSR_ROE))
					u->icount.overrun++;

				fsr_reg &= u->read_status_mask;
				if (fsr_reg & UART_FSR_BI)
					flag = TTY_BREAK;
				else if (fsr_reg & UART_FSR_PE)
					flag = TTY_PARITY;
				else if (fsr_reg & ( UART_FSR_FE | UART_FSR_ROE))
					flag = TTY_FRAME;
			}

			if (uart_handle_sysrq_char(u, ch))
				process_character = 0;

			if (process_character)
				uart_insert_char(u, fsr_reg, UART_FSR_ROE, ch, flag);
		}

		tty_flip_buffer_push(tty);
	} else if (isr_reg & UART_ISR_Tout_INT) {
		/* Get fifo status register */
		fsr_reg = rd_regl(s, REG_COM_FSR);

		/* Process a break */
		if (fsr_reg & UART_FSR_BI) {
			u->icount.brk++;
			uart_handle_break(u);
		}

		/* Rx software reset */
		wr_regl(s, REG_COM_FCR, rd_regl(s, REG_COM_FCR) | UART_FCR_RFR);
	}

	return IRQ_HANDLED;
}

static int n329_uart_request_port(struct uart_port *u)
{
	/* Nothing to do */
	return 0;
}

static int n329_uart_verify_port(struct uart_port *u,
					struct serial_struct *ser)
{
	if (u->type != PORT_UNKNOWN && u->type != PORT_N329)
		return -EINVAL;
	return 0;
}

static void n329_uart_config_port(struct uart_port *u, int flags)
{
	/* Nothing to do */
}

static const char *n329_uart_type(struct uart_port *u)
{
	struct n329_uart_port *s = to_n329_uart_port(u);

	return dev_name(s->dev);
}

static void n329_uart_release_port(struct uart_port *u)
{
	/* Nothing to do */
}

static void n329_uart_set_mctrl(struct uart_port *u, unsigned mctrl)
{
	/* Not supported by this driver */
}

static u32 n329_uart_get_mctrl(struct uart_port *u)
{
	/* Report CTS, DCD or DSR as active, RI as inactive */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

#define ABS_DELTA(a,b)  ((a) > (b) ? (a) - (b) : (b) - (a))

u32 n329_uart_calc_baud_register(u32 baud, u32 clock)
{
	u32 best_dxo, best_dxe, best_a, best_b, best_baud;
	u32 test_a, test_b, test_baud;

	/* Default calculation */
	best_dxo = 0;
	best_dxe = 0;
	best_b = 1;
	best_a = (clock / (baud * 16)) - 2;
	best_baud = clock / (16 * (best_a + 2));

	if (best_baud == baud)
		goto done;

	/* Try to get closer */
	test_a = (clock / baud) - 2;
	test_baud = clock / (test_a + 2);
	if ((test_a > 3) && (ABS_DELTA(baud, test_baud) < 
			ABS_DELTA(baud, best_baud)))
	{
		best_dxo = 1;
		best_dxe = 1;
		best_b = 1;
		best_a = test_a;
		best_baud = test_baud;
	}

	if (best_baud == baud)
		goto done;

	/* Try again to get closer */
	for (test_b = 10; test_b <= 16; ++test_b)
	{
		test_a = (clock / (baud * test_b)) - 2;
		test_baud = clock / (test_b * (test_a + 2));
		if (ABS_DELTA(baud, test_baud) <= 
				ABS_DELTA(baud, best_baud))
		{
			best_dxo = 0;
			best_dxe = 1;
			best_b = test_b;
			best_a = test_a;
			best_baud = test_baud;
		}
	}

done:
	pr_devel("dxe=%u dxo=%u b=%u a=%u best_baud=%u\n",
			best_dxe, best_dxo, best_b, best_a, best_baud);

	return (best_dxe << 29) | (best_dxo << 28) | 
		((best_b - 1)<< 24) | best_a;
}

static void n329_uart_settermios(struct uart_port *u,
				 struct ktermios *termios,
				 struct ktermios *old)
{
	struct n329_uart_port *s = to_n329_uart_port(u);
	unsigned long flags;
	u32 baud;
	u32 lcr_register;
	u32 baud_register;

	/* Update the port clock rate */
	s->port.uartclk = clk_get_rate(s->clk);

	/* We don't support modem control lines */
	termios->c_cflag &= ~(HUPCL | CMSPAR);
	termios->c_cflag |= CLOCAL;

	/* Determine the baud rate divider register contents */

	/* Turn the termios structure into a baud rate */
	baud = uart_get_baud_rate(u, termios, old, 300, 115200 * 8);

	/* Handle a custom divider */
	if (baud == 38400 && (u->flags & UPF_SPD_MASK) == UPF_SPD_CUST) {
		baud_register = u->custom_divisor;
		if (baud_register < 4)
			baud_register = 4;
		if (baud_register > 65535)
			baud_register = 65535;
		baud_register |= BIT(29) | BIT(28);
	} else {
		baud_register = n329_uart_calc_baud_register(baud, 
					s->port.uartclk);
	}
	pr_devel("baud=%d, divider=%08x\n", baud, baud_register);

	lcr_register = 0;
	switch (termios->c_cflag & CSIZE) {
		case CS5:
			lcr_register = UART_LCR_WLEN5;
			break;
		case CS6:
			lcr_register = UART_LCR_WLEN6;
			break;
		case CS7:
			lcr_register = UART_LCR_WLEN7;
			break;
		case CS8:
		default:
			lcr_register = UART_LCR_WLEN8;
			break;
	}

	if (termios->c_cflag & CSTOPB)
		lcr_register |= UART_LCR_NSB;

	if (termios->c_cflag & PARENB) {
		lcr_register |= UART_LCR_PARITY;
		if (termios->c_cflag & PARODD)
			lcr_register |= UART_LCR_OPAR;
		else
			lcr_register |= UART_LCR_EPAR;
	} else {
		lcr_register |= UART_LCR_NPAR;
	}

	spin_lock_irqsave(&u->lock, flags);

	wr_regl(s, REG_COM_BAUD, baud_register);
	wr_regl(s, REG_COM_LCR, lcr_register);
	wr_regl(s, REG_COM_MCR, 0x00);

	spin_unlock_irqrestore(&u->lock, flags);

	/* Update the per-port timeout */
	uart_update_timeout(u, termios->c_cflag, baud);

	/* Which character status flags are we interested in? */
	u->read_status_mask = UART_FSR_ROE | UART_FSR_TOE;
	if (termios->c_iflag & INPCK)
		u->read_status_mask |= UART_FSR_FE | UART_FSR_PE;

	/* Which character status flags should we ignore? */
	u->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		u->ignore_status_mask |= UART_FSR_ROE | UART_FSR_TOE;
	if ((termios->c_iflag & IGNBRK) && (termios->c_iflag & IGNPAR))
		u->ignore_status_mask |= UART_FSR_FE;

	/* Ignore all characters if CREAD is not set */
	if (~termios->c_cflag & CREAD)
		u->ignore_status_mask |= RXSTAT_DUMMY_READ;
}

static void n329_uart_reset(struct uart_port *u)
{
	struct n329_uart_port *s = to_n329_uart_port(u);

	/* Reset tx and rx fifos if the high-speed uart */
	if (u->line == 0)
		wr_regl(s, REG_COM_FCR, UART_FCR_RFR | UART_FCR_TFR | 
				UARTx_FCR_FIFO_LEVEL14);
}

static int n329_uart_startup(struct uart_port *u)
{
	int ret;
	struct n329_uart_port *s = to_n329_uart_port(u);

	/* TBD configure pin outputs */

	ret = clk_prepare_enable(s->clk);
	if (ret)
		return ret;

	/* Request the receive irq */
	ret = request_irq(s->irq, n329_uart_irq_handler, 0, dev_name(s->dev), s);
	if (ret)
		return ret;

	rx_enable(s);
	rx_enabled(s) = 1;

	s->rx_claimed = 1;
	s->tx_claimed = 1;

	return 0;
}

static void n329_uart_shutdown(struct uart_port *u)
{
	struct n329_uart_port *s = to_n329_uart_port(u);
	
	if (s->tx_claimed || s->rx_claimed) {
		// TBD free_irq(u->irq, s);
		clk_disable_unprepare(s->clk);
	}

	if (s->tx_claimed) {    
		tx_disable(s);
		tx_enabled(s) = 0;
		s->tx_claimed = 0;
	}

	if (s->rx_claimed) {
		rx_disable(s);
		rx_enabled(s) = 0;
		s->rx_claimed = 0;
	}
}

static unsigned int n329_uart_tx_empty(struct uart_port *u)
{
	struct n329_uart_port *s = to_n329_uart_port(u);

	if (rd_regl(s, REG_COM_FSR) & UART_FSR_TFE)
		return TIOCSER_TEMT;
	else
		return 0;
}

#if 0
static void n329_uart_enable_rx(struct uart_port *u)
{
	unsigned int fcr;
	unsigned long flags;
	struct n329_uart_port *s = to_n329_uart_port(u);

	spin_lock_irqsave(&u->lock, flags);

	fcr = rd_regl(s, REG_COM_FCR);
	fcr |= UART_FCR_RFR | UARTx_FCR_FIFO_LEVEL14;
	wr_regl(s, REG_COM_FCR, fcr);
	
	rx_enable(s);
	rx_enabled(s) = 1;

	spin_unlock_irqrestore(&u->lock, flags);
}

static void n329_uart_disable_rx(struct uart_port *u)
{
	unsigned long flags;
	struct n329_uart_port *s = to_n329_uart_port(u);

	spin_lock_irqsave(&u->lock, flags);

	rx_disable(s);
	rx_enabled(s) = 0;

	spin_unlock_irqrestore(&u->lock, flags);
}
#endif

static void n329_uart_start_tx(struct uart_port *u)
{
	struct n329_uart_port *s = to_n329_uart_port(u);

	if (!tx_enabled(s)) {
		tx_enable(s);
		tx_enabled(s) = 1;
#if 0
		if (u->flags & UPF_CONS_FLOW)
			n329_uart_disable_rx(u);
#endif
	}
}

static void n329_uart_stop_tx(struct uart_port *u)
{
	struct n329_uart_port *s = to_n329_uart_port(u);

	if (tx_enabled(s)) {
		tx_disable(s);
		tx_enabled(s) = 0;
#if 0
		if (u->flags & UPF_CONS_FLOW)
			n329_uart_enable_rx(u);
#endif
	}
}

static void n329_uart_stop_rx(struct uart_port *u)
{
	struct n329_uart_port *s = to_n329_uart_port(u);

	if (rx_enabled(s)) {
		rx_disable(s);
		rx_enabled(s) = 0;
	}
}

static void n329_uart_break_ctl(struct uart_port *u, int ctl)
{
	struct n329_uart_port *s = to_n329_uart_port(u);
	unsigned int ucon;
	unsigned long flags;

	spin_lock_irqsave(&u->lock, flags);

	ucon = rd_regl(s, REG_COM_LCR);
	if (ctl)
		ucon |= UART_LCR_SBC;
	else
		ucon &= ~UART_LCR_SBC;
	wr_regl(s, REG_COM_LCR, ucon);

	spin_unlock_irqrestore(&u->lock, flags);
}

static void n329_uart_enable_ms(struct uart_port *port)
{
	/* Nothing to do */
}

static struct uart_ops n329_uart_ops = {
	.tx_empty       = n329_uart_tx_empty,
	.start_tx       = n329_uart_start_tx,
	.stop_tx        = n329_uart_stop_tx,
	.stop_rx        = n329_uart_stop_rx,
	.enable_ms      = n329_uart_enable_ms,
	.break_ctl      = n329_uart_break_ctl,
	.set_mctrl      = n329_uart_set_mctrl,
	.get_mctrl      = n329_uart_get_mctrl,
	.startup        = n329_uart_startup,
	.shutdown       = n329_uart_shutdown,
	.set_termios    = n329_uart_settermios,
	.type           = n329_uart_type,
	.release_port   = n329_uart_release_port,
	.request_port   = n329_uart_request_port,
	.config_port    = n329_uart_config_port,
	.verify_port    = n329_uart_verify_port,
};

static struct n329_uart_port *n329_uart_ports[N329_UART_PORTS];

#ifdef CONFIG_SERIAL_N329_UART_CONSOLE

static void n329_console_putchar(struct uart_port *u, int ch)
{
	struct n329_uart_port *s = to_n329_uart_port(u);

	/* Wait if the fifo is full */
	while (rd_regl(s, REG_COM_FSR) & UART_FSR_TFF)
		barrier();

	/* Send the character */
	wr_regl(s, REG_COM_TX, ch);
}

static void n329_console_write(struct console *co, const char *str, 
			unsigned int count)
{
	struct n329_uart_port *s;

	s = n329_uart_ports[co->index];

	clk_enable(s->clk);

	/* Send the string */
	uart_console_write(&(s->port), str, count, n329_console_putchar);

	/* Wait for the fifo to empty */
	while (~rd_regl(s, REG_COM_FSR) & UART_FSR_TFE)
		barrier();

	clk_disable(s->clk);
}

static void __init n329_console_get_options(struct uart_port *u, 
			int *baud, int *parity, int *bits)
{
	struct n329_uart_port *s = to_n329_uart_port(u);
	u32 a, b, clock;
	u32 lcr_register;
	u32 baud_register;

	clock = clk_get_rate(s->clk);

	lcr_register = rd_regl(s, REG_COM_LCR);
	baud_register = rd_regl(s, REG_COM_BAUD);

	switch (lcr_register & UART_LCR_CSMASK)
	{
		case UART_LCR_WLEN5:
			*bits = 5;
			break;
		case UART_LCR_WLEN6:
			*bits = 6;
			break;
		case UART_LCR_WLEN7:
			*bits = 7;
			break;
		default:
		case UART_LCR_WLEN8:
			*bits = 8;
			break;
	}
	if (lcr_register & UART_LCR_PARITY)
	{
		switch (lcr_register & UART_LCR_PMMASK) {
			case UART_LCR_EPAR:
				*parity = 'e';
				break;
			case UART_LCR_OPAR:
				*parity = 'o';
				break;
			default:
				*parity = 'n';
		}
	}
	else
		*parity = 'n';

	b = 16;
	a = baud_register & 0xffff; 

	if (baud_register & BIT(29)) {
		if (baud_register & BIT(29)) {
			b = 1;
		} else {
			b = ((baud_register >> 24) & 0xf) + 1;
		}
	}

	*baud = clock / (b * (a + 2));

	pr_info("calculated baud %d\n", *baud);
}

static int __init n329_console_setup(struct console *co, char *options)
{
	struct n329_uart_port *s;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	/* 
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support. */
	if (co->index == -1 || co->index >= ARRAY_SIZE(n329_uart_ports))
		co->index = 0;
	s = n329_uart_ports[co->index];
	if (!s)
		return -ENODEV;

	ret = clk_prepare_enable(s->clk);
	if (ret)
		return ret;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		n329_console_get_options(&s->port, &baud, &parity, &bits);

	ret = uart_set_options(&s->port, co, baud, parity, bits, flow);

	clk_disable(s->clk);

	return ret;
}

static struct console n329_uart_console = {
	.name           = "ttyS",
	.write          = n329_console_write,
	.device         = uart_console_device,
	.setup          = n329_console_setup,
	.flags          = CON_PRINTBUFFER,
	.index          = -1,
	.data           = &n329_uart_driver,
};
#endif

static struct uart_driver n329_uart_driver = {
	.owner          = THIS_MODULE,
	.driver_name    = "ttyS",
	.dev_name       = "ttyS",
	.major          = 0,
	.minor          = 0,
	.nr             = N329_UART_PORTS,
#ifdef CONFIG_SERIAL_N329_UART_CONSOLE
	.cons =         &n329_uart_console,
#endif
};

/*
 * This function returns 1 if pdev isn't a device instatiated by dt, 0 if it
 * could successfully get all information from dt or a negative errno.
 */
static int serial_n329_probe_dt(struct n329_uart_port *s,
		struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np)
		/* No device tree device */
		return 1;

	ret = of_alias_get_id(np, "serial");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id: %d\n", ret);
		return ret;
	}
	s->port.line = ret;

	if (of_get_property(np, "fsl,uart-has-rtscts", NULL))
		set_bit(N329_UART_FLAGS_RTSCTS, &s->flags);

	return 0;
}

static int n329_uart_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(n329_uart_dt_ids, &pdev->dev);
	struct n329_uart_port *s;
	int ret = 0;
	struct resource *r;

	s = kzalloc(sizeof(struct n329_uart_port), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		goto out;
	}

	ret = serial_n329_probe_dt(s, pdev);
	if (ret > 0)
		s->port.line = pdev->id < 0 ? 0 : pdev->id;
	else if (ret < 0)
		goto out_free;

	if (of_id) {
		pdev->id_entry = of_id->data;
		s->devtype = pdev->id_entry->driver_data;
	}

	s->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(s->clk)) {
		ret = PTR_ERR(s->clk);
		goto out_free;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		ret = -ENXIO;
		goto out_free_clk;
	}

	s->ctrl = 0;
	s->port.mapbase = r->start;
	s->port.membase = ioremap(r->start, resource_size(r));
	s->port.ops = &n329_uart_ops;
	s->port.iotype = UPIO_MEM;
	s->port.fifosize = N329_UART_FIFO_SIZE;
	s->port.uartclk = clk_get_rate(s->clk);
	s->port.type = PORT_N329;
	s->port.dev = s->dev = &pdev->dev;
	s->port.irq = s->irq = platform_get_irq(pdev, 0);

	platform_set_drvdata(pdev, s);

	n329_uart_ports[s->port.line] = s;

	n329_uart_reset(&s->port);

	ret = uart_add_one_port(&n329_uart_driver, &s->port);
	if (ret)
		goto out_free_irq;

	dev_info(&pdev->dev, "Found UART %d\n", (int) s->port.line);

	return 0;

out_free_irq:
	n329_uart_ports[pdev->id] = NULL;
	free_irq(s->irq, s);
out_free_clk:
	clk_put(s->clk);
out_free:
	kfree(s);
out:
	return ret;
}

static int n329_uart_remove(struct platform_device *pdev)
{
	struct n329_uart_port *s = platform_get_drvdata(pdev);

	uart_remove_one_port(&n329_uart_driver, &s->port);

	n329_uart_ports[pdev->id] = NULL;

	clk_put(s->clk);
	free_irq(s->irq, s);
	kfree(s);

	return 0;
}

static struct platform_driver n329_platform_uart_driver = {
	.probe = n329_uart_probe,
	.remove = n329_uart_remove,
	.driver = {
		.name = "n329-uart",
		.owner = THIS_MODULE,
		.of_match_table = n329_uart_dt_ids,
	},
};

static int __init n329_uart_init(void)
{
	int r;

	r = uart_register_driver(&n329_uart_driver);
	if (r)
		goto out;

	r = platform_driver_register(&n329_platform_uart_driver);
	if (r)
		goto out_err;

	return 0;
out_err:
	uart_unregister_driver(&n329_uart_driver);
out:
	return r;
}

static void __exit n329_uart_exit(void)
{
	platform_driver_unregister(&n329_platform_uart_driver);
	uart_unregister_driver(&n329_uart_driver);
}

module_init(n329_uart_init);
module_exit(n329_uart_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nuvoton N329XX application uart driver");
MODULE_ALIAS("platform:n329-uart");
