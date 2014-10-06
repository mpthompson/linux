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

#define HW_COM_TX		0x00
#define HW_COM_RX		0x00
#define HW_COM_IER		0x04
#define HW_COM_FCR		0x08
#define HW_COM_LCR		0x0C
#define HW_COM_MCR		0x10
#define HW_COM_MSR		0x14
#define HW_COM_FSR		0x18
#define HW_COM_ISR		0x1C
#define HW_COM_TOR		0x20
#define HW_COM_BAUD		0x24

#define UARTx_FCR_FIFO_LEVEL1	0x00
#define UARTx_FCR_FIFO_LEVEL4	0x10
#define UARTx_FCR_FIFO_LEVEL8	0x20
#define UARTx_FCR_FIFO_LEVEL14	0x30
#define UARTx_FCR_FIFO_LEVEL30	0x40
#define UARTx_FCR_FIFO_LEVEL46	0x50
#define UARTx_FCR_FIFO_LEVEL62	0x60

#define UART_FCR_RFR			0x02
#define UART_FCR_TFR			0x04

#define UART_TXRXFIFO_RESET	(UART_FCR_RFR | UART_FCR_TFR)

#define UART_FSR_ROE			0x00000000		// Rx Overrun error
#define UART_FSR_PE				0x00000010		// Parity error
#define UART_FSR_FE				0x00000020		// Frame error
#define UART_FSR_BI				0x00000040		// Break interrupt
#define UART_FSR_RFE			0x00004000		// Rx FIFO empty
#define UART_FSR_RFF			0x00008000		// Rx FIFO full
#define UART_FSR_RPMASK			(0x00003F00)	// Rx FIFO pointer
#define UART_FSR_TFE			0x00400000		// Tx FIFO empty
#define UART_FSR_TFF			0x00800000		// Tx FIFO full
#define UART_FSR_TPMASK			(0x003F0000)	// Tx FIFO pointer
#define UART_FSR_TOE			0x01000000		// Tx Overrun error
#define UART_FSR_TEMT			0x10000000		// Transmitter empty
	
#define UART_LCR_WLEN5			0x00
#define UART_LCR_WLEN6			0x01
#define UART_LCR_WLEN7			0x02
#define UART_LCR_WLEN8			0x03
#define UART_LCR_CSMASK			(0x3)
#define UART_LCR_PARITY			0x08
#define UART_LCR_NPAR			0x00
#define UART_LCR_OPAR			0x00
#define UART_LCR_EPAR			0x10
#define UART_LCR_PMMASK			(0x30)
#define UART_LCR_SPAR			0x20
#define UART_LCR_SBC			0x40
#define UART_LCR_NSB			0x00
#define UART_LCR_NSB1_5			0x04

#define UART_IER_TOUT			BIT(11)
#define UART_IER_RTO			BIT(4)
#define UART_IER_MSI			BIT(3)
#define UART_IER_RLSI			BIT(2)
#define UART_IER_THRI			BIT(1)
#define UART_IER_RDI			BIT(0)

#define UART_ISR_EDMA_RX_Flag	BIT(31)		// EDMA RX Mode Flag
#define UART_ISR_HW_Wake_INT	BIT(30)		// Wake up Interrupt pin status
#define UART_ISR_HW_Buf_Err_INT	BIT(29)		// Buffer Error Interrupt pin status
#define UART_ISR_HW_Tout_INT	BIT(28)		// Time out Interrupt pin status
#define UART_ISR_HW_Modem_INT	BIT(27)		// MODEM Status Interrupt pin status
#define UART_ISR_HW_RLS_INT		BIT(26)		// Receive Line Status Interrupt pin status
#define UART_ISR_Rx_ack_st		BIT(25)		// TX ack pin status
#define UART_ISR_Rx_req_St		BIT(24)		// TX req pin status
#define UART_ISR_EDMA_TX_Flag	BIT(23)		// EDMA TX Mode Flag
#define UART_ISR_HW_Wake_IF		BIT(22)		// Wake up Flag
#define UART_ISR_HW_Buf_Err_IF	BIT(21)		// Buffer Error Flag
#define UART_ISR_HW_Tout_IF		BIT(20)		// Time out Flag
#define UART_ISR_HW_Modem_IF	BIT(19)		// MODEM Status Flag
#define UART_ISR_HW_RLS_IF		BIT(18)		// Receive Line Status Flag
#define UART_ISR_Tx_ack_st		BIT(17)		// TX ack pin status
#define UART_ISR_Tx_req_St		BIT(16)		// TX req pin status
#define UART_ISR_Soft_RX_Flag	BIT(15)		// Software RX Mode Flag
#define UART_ISR_Wake_INT		BIT(14)		// Wake up Interrupt pin status
#define UART_ISR_Buf_Err_INT	BIT(13)		// Buffer Error Interrupt pin status
#define UART_ISR_Tout_INT		BIT(12)		// Time out interrupt Interrupt pin status
#define UART_ISR_Modem_INT		BIT(11)		// MODEM Status Interrupt pin status
#define UART_ISR_RLS_INT		BIT(10)		// Receive Line Status Interrupt pin status
#define UART_ISR_THRE_INT		BIT(9)		// Transmit Holding Register Empty Interrupt pin status
#define UART_ISR_RDA_INT		BIT(8)		// Receive Data Available Interrupt pin status
#define UART_ISR_Soft_TX_Flag	BIT(7)		// Software TX Mode Flag
#define UART_ISR_Wake_IF		BIT(6)		// Wake up Flag
#define UART_ISR_Buf_Err_IF		BIT(5)		// Buffer Error Flag
#define UART_ISR_Tout_IF		BIT(4)		// Time out interrupt Flag
#define UART_ISR_Modem_IF		BIT(3)		// MODEM Status Flag
#define UART_ISR_RLS_IF			BIT(2)		// Receive Line Status Flag
#define UART_ISR_THRE_IF		BIT(1)		// Transmit Holding Register Empty Flag
#define UART_ISR_RDA_IF			BIT(0)		// Receive Data Available Flag

#define N329_UART_PORTS 2
#define N329_UART_FIFO_SIZE 16         

/* register access controls */
#define portaddr(s, reg) ((s->port.membase) + (reg))
#define rd_regb(s, reg) (__raw_readb(portaddr(s, reg)))
#define rd_regl(s, reg) (__raw_readl(portaddr(s, reg)))
#define wr_regb(s, reg, val) \
	do { __raw_writeb(val, portaddr(s, reg)); } while(0)
#define wr_regl(s, reg, val) \
	do { __raw_writel(val, portaddr(s, reg)); } while(0)

enum n329_uart_type {
	N32905_UART
};

#define N329_UART_FLAGS_RTSCTS	1  /* bit 1 */

struct n329_uart_port {
	struct uart_port port;

	enum n329_uart_type devtype;

	unsigned long flags;
	unsigned int ctrl;

	unsigned int irq;
	struct clk *clk;
	struct device *dev;
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

static int n329_uart_request_port(struct uart_port *u)
{
	/* nothing to do */
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
	/* nothing to do */
}

static const char *n329_uart_type(struct uart_port *u)
{
	struct n329_uart_port *s = to_n329_uart_port(u);

	return dev_name(s->dev);
}

static void n329_uart_release_port(struct uart_port *u)
{
	/* nothing to do */
}

static void n329_uart_set_mctrl(struct uart_port *u, unsigned mctrl)
{
	// XXX TBD
}

static u32 n329_uart_get_mctrl(struct uart_port *u)
{
	// XXX TBD
	struct n329_uart_port *s = to_n329_uart_port(u);
	u32 mctrl = s->ctrl;

	mctrl |= TIOCM_RTS;

	return mctrl;
}

static void n329_uart_settermios(struct uart_port *u,
				 struct ktermios *termios,
				 struct ktermios *old)
{
	// XXX TBD
}

static irqreturn_t n329_uart_irq_handle(int irq, void *context)
{
	// XXX TBD
	return IRQ_HANDLED;
}

static void n329_uart_reset(struct uart_port *u)
{
	// XXX TBD
}

static int n329_uart_startup(struct uart_port *u)
{
	// XXX TBD
	int ret;
	struct n329_uart_port *s = to_n329_uart_port(u);

	ret = clk_prepare_enable(s->clk);
	if (ret)
		return ret;

	return 0;
}

static void n329_uart_shutdown(struct uart_port *u)
{
	// XXX TBD
	struct n329_uart_port *s = to_n329_uart_port(u);

	clk_disable_unprepare(s->clk);
}

static unsigned int n329_uart_tx_empty(struct uart_port *u)
{
	// XXX TBD

	return TIOCSER_TEMT;
}

static void n329_uart_start_tx(struct uart_port *u)
{
	// XXX TBD
}

static void n329_uart_stop_tx(struct uart_port *u)
{
	// XXX TBD
}

static void n329_uart_stop_rx(struct uart_port *u)
{
	// XXX TBD
}

static void n329_uart_break_ctl(struct uart_port *u, int ctl)
{
	// XXX TBD
}

static void n329_uart_enable_ms(struct uart_port *port)
{
	/* nothing to do */
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

	/* wait if the fifo is full */
	while (rd_regl(s, HW_COM_FSR) & UART_FSR_TFF)
		barrier();

	/* send the character */
	wr_regl(s, HW_COM_TX, ch);
}

static void n329_console_write(struct console *co, const char *str, 
			unsigned int count)
{
	struct n329_uart_port *s;

	s = n329_uart_ports[co->index];

	clk_enable(s->clk);

	/* send the string */
	uart_console_write(&(s->port), str, count, n329_console_putchar);

	/* wait for the fifo to empty */
	while (~rd_regl(s, HW_COM_FSR) & UART_FSR_TFE)
		barrier();

	clk_disable(s->clk);
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
	 * console support.
	 */
	if (co->index == -1 || co->index >= ARRAY_SIZE(n329_uart_ports))
		co->index = 0;
	s = n329_uart_ports[co->index];
	if (!s)
		return -ENODEV;

	ret = clk_prepare_enable(s->clk);
	if (ret)
	 	return ret;

	/*
	// XXX TBD
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		auart_console_get_options(&s->port, &baud, &parity, &bits);
	*/

	ret = uart_set_options(&s->port, co, baud, parity, bits, flow);

	clk_disable(s->clk);

	return ret;
}

static struct console n329_uart_console = {
	.name			= "ttyS",
	.write			= n329_console_write,
	.device			= uart_console_device,
	.setup			= n329_console_setup,
	.flags			= CON_PRINTBUFFER,
	.index			= -1,
	.data			= &n329_uart_driver,
};
#endif

static struct uart_driver n329_uart_driver = {
	.owner			= THIS_MODULE,
	.driver_name	= "ttyS",
	.dev_name		= "ttyS",
	.major			= 0,
	.minor			= 0,
	.nr				= N329_UART_PORTS,
#ifdef CONFIG_SERIAL_N329_UART_CONSOLE
	.cons =			&n329_uart_console,
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
		/* no device tree device */
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

	s->port.mapbase = r->start;
	s->port.membase = ioremap(r->start, resource_size(r));
	s->port.ops = &n329_uart_ops;
	s->port.iotype = UPIO_MEM;
	s->port.fifosize = N329_UART_FIFO_SIZE;
	s->port.uartclk = clk_get_rate(s->clk);
	s->port.type = PORT_N329;
	s->port.dev = s->dev = &pdev->dev;

	s->ctrl = 0;

	s->irq = platform_get_irq(pdev, 0);
	s->port.irq = s->irq;
	ret = request_irq(s->irq, n329_uart_irq_handle, 0, dev_name(&pdev->dev), s);
	if (ret)
		goto out_free_clk;

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
