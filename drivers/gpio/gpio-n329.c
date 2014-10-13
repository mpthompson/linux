/*
 * N329XX GPIO support.
 * Copyright 2014 Michael P. Thompson, mpthompson@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>

enum n329_gpio_id {
	N32905_GPIO,
	N32916_GPIO
};

struct n329_gpio_port {
	struct gpio_chip gc;
	void __iomem *base;
	void __iomem *gpio_base;
	int id;
	enum n329_gpio_id devid;
};

#define to_n329_gpio_port(u) container_of(u, struct n329_gpio_port, gc)

/* GPIO bank definition */
#define GPIO_BANK_A 0
#define GPIO_BANK_B 1
#define GPIO_BANK_C 2
#define GPIO_BANK_D 3
#define GPIO_BANK_E 4

#define PINID(bank, pin)	(((bank) << 4) + (pin))
#define PINID_TO_BANK(p)	((p) >> 4)
#define PINID_TO_PIN(p)		((p) % 16)

/* returns the value of the GPIO pin */
static int n329_gpio_get(struct n329_gpio_port *p, u16 pinid) 
{
	u16 bank = PINID_TO_BANK(pinid);
	u16 pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + (bank << 4) + 0x0c;

	return readl(reg) & (1 << pin) ? 1 : 0;
}

/* set direction of pin to input mode */
static void n329_gpio_set_input(struct n329_gpio_port *p, u16 pinid)
{
	u16 bank = PINID_TO_BANK(pinid);
	u16 pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + (bank << 4);

	writel(readl(reg) & ~(1 << pin), reg);
}

/* set direction of pin to output mode */
static void n329_gpio_set_output(struct n329_gpio_port *p, u16 pinid)
{
	u16 bank = PINID_TO_BANK(pinid);
	u16 pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + (bank << 4);

	writel(readl(reg) | (1 << pin), reg);
}

/* set the pin out to state */
static void n329_gpio_set(struct n329_gpio_port *p, u16 pinid, int state) 
{	
	u16 bank = PINID_TO_BANK(pinid);
	u16 pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + (bank << 4) + 0x08;

	if (state)
		writel(readl(reg) | (1 << pin), reg); 
	else
		writel(readl(reg) & ~(1 << pin), reg);
}

/* select the mux to enable gpio function on the indicated pin */
static int n329_gpio_select(struct n329_gpio_port *p, u16 pinid)
{
	u16 bank = PINID_TO_BANK(pinid);
	u16 pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + 0x80 + (bank << 2);

	if (pin > 16)
		goto err;
		
	switch (bank)
	{
		case GPIO_BANK_A:	
			if (pin > 11)
				goto err;
			writel(readl(reg) & ~(0x3 << (pin << 1)), reg);
			break;
		
		case GPIO_BANK_B:
			writel(readl(reg) & ~(0x3 << (pin << 1)), reg);
			break;

		case GPIO_BANK_C:
			writel(readl(reg) & ~(0x3 << (pin << 1)), reg);
			break;
				
		case GPIO_BANK_D:			
			writel(readl(reg) & ~(0x3 << (pin << 1)), reg);
			break;
		
		case GPIO_BANK_E:
			if (pin > 11)
				goto err;
			writel(readl(reg) & ~(0x3 << (pin << 1)), reg);
			break;
		default:
			break;
	}
	return 1;
	
err:
	return 0;
}

static int n329_gpio_get_value(struct gpio_chip *gc, unsigned offset)
{
	struct n329_gpio_port *p = to_n329_gpio_port(gc);
	u16 bank = p->id;
	
	/* Get the value */
	return n329_gpio_get(p, PINID(bank, offset));
}

static void n329_gpio_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	struct n329_gpio_port *p = to_n329_gpio_port(gc);
	u16 bank = p->id;

	/* Set the value */
	n329_gpio_set(p, PINID(bank, offset), value); 
}

static int n329_gpio_dir_out(struct gpio_chip *gc, unsigned offset, int value)
{
	struct n329_gpio_port *p = to_n329_gpio_port(gc);
	u16 bank = p->id;

	/* Set the pin function mux GPIO */
	n329_gpio_select(p, PINID(bank, offset));

	/* Set for output */
	n329_gpio_set_output(p, PINID(bank, offset));

	/* Set initial value */
	n329_gpio_set(p, PINID(bank, offset), value);

	return 0;
}

static int n329_gpio_dir_in(struct gpio_chip *gc, unsigned offset)
{
	struct n329_gpio_port *p = to_n329_gpio_port(gc);
	u16 bank = p->id;

	/* Set the pin function mux GPIO */
	n329_gpio_select(p, PINID(bank, offset));

	/* Set for input */
	n329_gpio_set_input(p, PINID(bank, offset));

	return 0;
}

static struct of_device_id n329_gpio_of_match[] = {
	{ .compatible = "nuvoton,n32905-gpio", .data = (void *) N32905_GPIO, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n329_gpio_of_match);

static struct platform_device_id n329_gpio_ids[] = {
	{
		.name = "n32905-gpio",
		.driver_data = N32905_GPIO,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, n329_gpio_ids);

static int n329_gpio_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(n329_gpio_of_match, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	struct device_node *parent;
	static void __iomem *base;
	static void __iomem *gpio_base;
	struct n329_gpio_port *port;
	u32 ngpio;
	int err;

	port = devm_kzalloc(&pdev->dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->id = of_alias_get_id(np, "gpio");
	if (port->id < 0)
		return port->id;
	port->devid = (enum n329_gpio_id) of_id->data;

	/* count of pins for this port */
	err = of_property_read_u32(np, "ngpio", &ngpio);
	if (err)
		return err;

	/*
	 * map memory region only once, as all the gpio ports
	 * share the same one
	 */
	if (!base || !gpio_base) {
		parent = of_get_parent(np);
		base = of_iomap(parent, 0);
		gpio_base = of_iomap(parent, 1);
		of_node_put(parent);
		if (!base || !gpio_base)
			return -EADDRNOTAVAIL;
	}
	port->base = base;
	port->gpio_base = base;

	port->gc.label = "n32905-gpio";
	port->gc.base = 0;
	port->gc.ngpio = ngpio;
	port->gc.owner = THIS_MODULE;

	port->gc.direction_input = n329_gpio_dir_in;
	port->gc.direction_output = n329_gpio_dir_out;
	port->gc.get = n329_gpio_get_value;
	port->gc.set = n329_gpio_set_value;
	port->gc.can_sleep = 0;

	err = gpiochip_add(&port->gc);
	if (err)
		goto err;

	return 0;

err:
	return err;
}

static struct platform_driver n329_gpio_driver = {
	.driver		= {
		.name	= "n329-gpio",
		.owner	= THIS_MODULE,
		.of_match_table = n329_gpio_of_match,
	},
	.probe		= n329_gpio_probe,
	.id_table	= n329_gpio_ids,
};

static int __init n329_gpio_init(void)
{
	return platform_driver_register(&n329_gpio_driver);
}
postcore_initcall(n329_gpio_init);

MODULE_AUTHOR("Michael P. Thompson <mpthompson@gmail.com>");
MODULE_DESCRIPTION("Nuvoton N329XX GPIO driver");
MODULE_LICENSE("GPL");
