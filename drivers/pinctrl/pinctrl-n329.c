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

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "core.h"
#include "pinctrl-n329.h"

#define SUFFIX_LEN		4
#define BADPINID		0xffff

/* Must start after 32 N329XX AIC hardware IRQs */
#define GPIO_IRQ_START	32

struct n329_pinctrl_data {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct gpio_chip gc;
	void __iomem *gcr_base;
	void __iomem *gpio_base;
	struct n329_pinctrl_soc_data *soc;
	unsigned hw_irq0;
	unsigned hw_irq1;
	unsigned hw_irq2;
	unsigned hw_irq3;
	unsigned rising[5];
	unsigned falling[5];
};

#define to_n329_pinctrl_data(u) container_of(u, struct n329_pinctrl_data, gc)

static unsigned n329_offset_to_pinid(unsigned offset)
{
	unsigned pinid = BADPINID;

	if (offset < 12)
		pinid = PINID(0, offset);
	else if (offset < 28)
		pinid = PINID(1, offset - 12);
	else if (offset < 44)
		pinid = PINID(2, offset - 28);
	else if (offset < 60)
		pinid = PINID(3, offset - 44);
	else if (offset < 72)
		pinid = PINID(4, offset - 60);

	return pinid;
}

static unsigned n329_pinid_to_offset(unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	unsigned offset;

	if (bank < 1)
		offset = pinid;
	else if (bank < 2)
		offset = 12 + pin;
	else if (bank < 3)
		offset = 28 + pin;
	else if (bank < 4)
		offset = 44 + pin;
	else
		offset = 60 + pin;

	return offset;
}

static int n329_pinctrl_gpio_get(struct n329_pinctrl_data *p, unsigned pinid) 
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + (bank << 4) + 0x0c;

	/* Return the value of the GPIO pin */
	return readl(reg) & (1 << pin) ? 1 : 0;
}

static void n329_pinctrl_gpio_set(struct n329_pinctrl_data *p, unsigned pinid, int state) 
{	
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + (bank << 4) + 0x08;

	/* Set the pin out to state */
	if (state)
		writel(readl(reg) | (1 << pin), reg); 
	else
		writel(readl(reg) & ~(1 << pin), reg);
}

static void n329_pinctrl_gpio_set_input(struct n329_pinctrl_data *p, unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + (bank << 4);

	/* Set direction of pin to input mode */
	writel(readl(reg) & ~(1 << pin), reg);
}

static void n329_pinctrl_gpio_set_output(struct n329_pinctrl_data *p, unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + (bank << 4);

	/* Set direction of pin to output mode */
	writel(readl(reg) | (1 << pin), reg);
}

static int n329_pinctrl_mux_select_gpio(struct n329_pinctrl_data *p, unsigned pinid)
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg;

	/* Sanity checks */
	if (bank > (N329_BANKS - 1))
		goto err;
	if ((pin > 15) || (((bank == 0) || (bank == 4)) && pin > 11))
		goto err;

	/* Mux register address */
	reg = p->gcr_base + 0x80 + (bank << 2);

	/* Select the mux to enable gpio function on the indicated pin */
	writel(readl(reg) & ~(0x3 << (pin << 1)), reg);

	return 1;
	
err:
	return 0;
}

static int n329_pinctrl_gpio_get_irq(struct n329_pinctrl_data *p, 
				unsigned pinid) 
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + 0x80 + (bank << 2);

	/* Return the irq index for the GPIO pin */
	int irq = (int) (readl(reg) >> (pin << 1)) & 0x3;

	/* GPIO IRQs are offset by two */
	return irq + 2;
}

static void n329_pinctrl_gpio_set_irq(struct n329_pinctrl_data *p, 
				unsigned pinid, unsigned irq) 
{
	unsigned bank = PINID_TO_BANK(pinid);
	unsigned pin = PINID_TO_PIN(pinid);
	void __iomem *reg = p->gpio_base + 0x80 + (bank << 2);

	/* Update the register with the new irq value */
	u32 val = readl(reg);
	val &= ~(0x03 << (pin << 1));
	val |= ((irq - 2) << (pin << 1));
	writel(val, reg);
}

static int n329_pinctrl_gpio_get_value(struct gpio_chip *gc, unsigned offset)
{
	struct n329_pinctrl_data *p = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return 0;

	/* Get the value */
	return n329_pinctrl_gpio_get(p, pinid);
}

static void n329_pinctrl_gpio_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	struct n329_pinctrl_data *p = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return;

	n329_pinctrl_gpio_set(p, pinid, value); 
}

static int n329_pinctrl_gpio_dir_out(struct gpio_chip *gc, unsigned offset, int value)
{
	struct n329_pinctrl_data *p = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return -ENXIO;

	/* Select pin mux for gpio functionality */
	n329_pinctrl_mux_select_gpio(p, pinid);

	/* Set for output */
	n329_pinctrl_gpio_set_output(p, pinid);

	/* Set initial value */
	n329_pinctrl_gpio_set(p, pinid, value);

	return 0;
}

static int n329_pinctrl_gpio_dir_in(struct gpio_chip *gc, unsigned offset)
{
	struct n329_pinctrl_data *p = to_n329_pinctrl_data(gc);
	unsigned pinid = n329_offset_to_pinid(offset);

	if (pinid == BADPINID)
		return -ENXIO;

	/* Select pin mux for gpio functionality */
	n329_pinctrl_mux_select_gpio(p, pinid);

	/* Set for input */
	n329_pinctrl_gpio_set_input(p, pinid);

	return 0;
}

static int n329_pinctrl_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	return (int) offset + GPIO_IRQ_START;
}

static int n329_pinctrl_gpio_irq_set_type(struct irq_data *id, unsigned type)
{
	unsigned irq = id->irq;
	unsigned offset = irq - GPIO_IRQ_START;
	struct n329_pinctrl_data *d = irq_get_chip_data(irq);
	unsigned pinid, bank, pin;

	/* We only support rising and falling types */
	if (type & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	pinid = n329_offset_to_pinid(offset);
	if (pinid == BADPINID)
		return -EINVAL;

	bank = PINID_TO_BANK(pinid);
	pin = PINID_TO_PIN(pinid);

	if (type & IRQ_TYPE_EDGE_RISING) {
		d->rising[bank] |= (1 << pin);
	} else {
		d->rising[bank] &= ~(1 << pin);
	}

	if (type & IRQ_TYPE_EDGE_FALLING) {
		d->falling[bank] |= (1 << pin);
	} else {
		d->falling[bank] &= ~(1 << pin);
	}

	return 0;
}

static void n329_pinctrl_gpio_irq_mask(struct irq_data *id)
{
	unsigned irq = id->irq;
	unsigned offset = irq - GPIO_IRQ_START;
	struct n329_pinctrl_data *d = irq_get_chip_data(irq);
	unsigned pinid, bank, pin;
	void __iomem *reg;

	pinid = n329_offset_to_pinid(offset);
	if (pinid == BADPINID)
		return;

	bank = PINID_TO_BANK(pinid);
	pin = PINID_TO_PIN(pinid);
	reg = d->gpio_base + 0xa0 + (bank << 2);

	/* Clear falling and rising enable */
	writel(readl(reg) & ~(1 << pin), reg);
	writel(readl(reg) & ~(1 << (pin + 16)), reg);
}

static void n329_pinctrl_gpio_irq_unmask(struct irq_data *id)
{
	unsigned irq = id->irq;
	unsigned offset = irq - GPIO_IRQ_START;
	struct n329_pinctrl_data *d = irq_get_chip_data(irq);
	unsigned pinid, bank, pin;
	void __iomem *reg;

	pinid = n329_offset_to_pinid(offset);
	if (pinid == BADPINID)
		return;

	bank = PINID_TO_BANK(pinid);
	pin = PINID_TO_PIN(pinid);
	reg = d->gpio_base + 0xa0 + (bank << 2);

	/* Make sure pin is an input */
	n329_pinctrl_gpio_set_input(d, pinid);

	/* Set the GPIO IRQ0 value */
	n329_pinctrl_gpio_set_irq(d, pinid, d->hw_irq0);

	/* Clear or set falling enable */
	if (d->falling[bank] & (1 << pin)) {
		writel(readl(reg) | (1 << pin), reg);
	} else {
		writel(readl(reg) & ~(1 << pin), reg);
	}

	/* Clear or set rising enable */
	if (d->rising[bank] & (1 << pin)) {
		writel(readl(reg) | (1 << (pin + 16)), reg);
	} else {
		writel(readl(reg) & ~(1 << (pin + 16)), reg);
	}
}

static irqreturn_t n329_pinctrl_gpio_interrupt(int irq, void *dev_id)
{
	struct n329_pinctrl_data *d = dev_id;
	unsigned bank, pin, clear, pinid;
	unsigned long source, i;
	int sw_irq;
	void __iomem *reg;

	/* Loop over each bank */
	for (bank = 0; bank < N329_BANKS; bank++) {
		reg = d->gpio_base + 0xf0 + ((bank >> 1) << 2);

		/* Get the interrupt source bits for this bank */
		source = readl(reg);
		if (bank & 0x01)
			source = (source >> 16) & 0xffff;
		else
			source = source & 0xffff;
			
		/* Get the active pins for this bank */
		for_each_set_bit(i, &source, 16) {
			pin = (unsigned) i;
			pinid = PINID(bank, pin);

			/* Only process interrupts with this id */
			if (irq == n329_pinctrl_gpio_get_irq(d, pinid)) {

				/* Determine the software interrupt */
				sw_irq = n329_pinid_to_offset(PINID(bank, pin));
				sw_irq += GPIO_IRQ_START;

				/* Call the software interrupt handler */
				generic_handle_irq(sw_irq);
	
				/* Clear the interrupt */
				if (bank & 0x01)
					clear = (1 << (pin + 16));
				else
					clear = (1 << pin);
				writel(clear, reg);
			}
		}
	}

	return IRQ_HANDLED;
}

static struct irqaction n329_gpio_irq = {
	.name = "N329 GPIO handler",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = n329_pinctrl_gpio_interrupt,
};

static struct irq_chip n329_irqchip = {
	.name = "N329 GPIO chip",
	.irq_enable = n329_pinctrl_gpio_irq_unmask,
	.irq_disable = n329_pinctrl_gpio_irq_mask,
	.irq_unmask = n329_pinctrl_gpio_irq_unmask,
	.irq_mask = n329_pinctrl_gpio_irq_mask,
	.irq_set_type = n329_pinctrl_gpio_irq_set_type,
};

static int n329_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	return d->soc->ngroups;
}

static const char *n329_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	return d->soc->groups[group].name;
}

static int n329_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
			      const unsigned **pins, unsigned *num_pins)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	*pins = d->soc->groups[group].pins;
	*num_pins = d->soc->groups[group].npins;

	return 0;
}

static void n329_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}

static int n329_dt_node_to_map(struct pinctrl_dev *pctldev,
			      struct device_node *np,
			      struct pinctrl_map **map, unsigned *num_maps)
{
	struct pinctrl_map *new_map;
	char *group = NULL;
	unsigned new_num = 1;
	unsigned long config = 0;
	unsigned long *pconfig;
	int length = strlen(np->name) + SUFFIX_LEN;
	bool purecfg = false;
	u32 val, reg;
	int ret, i = 0;

	/* Check for pin config node which has no 'reg' property */
	if (of_property_read_u32(np, "reg", &reg))
		purecfg = true;

	ret = of_property_read_u32(np, "nuvoton,pull-up", &val);
	if (!ret)
		config |= val << PULL_SHIFT | PULL_PRESENT;

	/* Check for group node which has both mux and config settings */
	if (!purecfg && config)
		new_num = 2;

	new_map = kzalloc(sizeof(*new_map) * new_num, GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	if (!purecfg) {
		new_map[i].type = PIN_MAP_TYPE_MUX_GROUP;
		new_map[i].data.mux.function = np->name;

		/* Compose group name */
		group = kzalloc(length, GFP_KERNEL);
		if (!group) {
			ret = -ENOMEM;
			goto free;
		}
		snprintf(group, length, "%s.%d", np->name, reg);
		new_map[i].data.mux.group = group;
		i++;
	}

	if (config) {
		pconfig = kmemdup(&config, sizeof(config), GFP_KERNEL);
		if (!pconfig) {
			ret = -ENOMEM;
			goto free_group;
		}

		new_map[i].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		new_map[i].data.configs.group_or_pin = purecfg ? np->name :
								 group;
		new_map[i].data.configs.configs = pconfig;
		new_map[i].data.configs.num_configs = 1;
	}

	*map = new_map;
	*num_maps = new_num;

	return 0;

free_group:
	if (!purecfg)
		kfree(group);
free:
	kfree(new_map);
	return ret;
}

static void n329_dt_free_map(struct pinctrl_dev *pctldev,
			    struct pinctrl_map *map, unsigned num_maps)
{
	unsigned i;

	for (i = 0; i < num_maps; i++) {
		if (map[i].type == PIN_MAP_TYPE_MUX_GROUP)
			kfree(map[i].data.mux.group);
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);
	}

	kfree(map);
}

static const struct pinctrl_ops n329_pinctrl_ops = {
	.get_groups_count = n329_get_groups_count,
	.get_group_name = n329_get_group_name,
	.get_group_pins = n329_get_group_pins,
	.pin_dbg_show = n329_pin_dbg_show,
	.dt_node_to_map = n329_dt_node_to_map,
	.dt_free_map = n329_dt_free_map,
};

static int n329_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	return d->soc->nfunctions;
}

static const char *n329_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
					     unsigned function)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	return d->soc->functions[function].name;
}

static int n329_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
				       unsigned group,
				       const char * const **groups,
				       unsigned * const num_groups)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	*groups = d->soc->functions[group].groups;
	*num_groups = d->soc->functions[group].ngroups;

	return 0;
}

static int n329_pinctrl_enable(struct pinctrl_dev *pctldev, unsigned selector,
			      unsigned group)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);
	struct n329_group *g = &d->soc->groups[group];
	void __iomem *reg;
	unsigned bank, pin, shift, val, i;

	for (i = 0; i < g->npins; i++) {
		bank = PINID_TO_BANK(g->pins[i]);
		pin = PINID_TO_PIN(g->pins[i]);
		reg = d->gcr_base + 0x80 + (bank << 4);
		shift = pin << 1;

		val = readl(reg);
		val &= ~(0x3 << shift);
		val |= (g->muxsel[i] << shift);
		writel(val, reg);
	}

	return 0;
}

static const struct pinmux_ops n329_pinmux_ops = {
	.get_functions_count = n329_pinctrl_get_funcs_count,
	.get_function_name = n329_pinctrl_get_func_name,
	.get_function_groups = n329_pinctrl_get_func_groups,
	.enable = n329_pinctrl_enable,
};

static int n329_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned pin, unsigned long *config)
{
	return -ENOTSUPP;
}

static int n329_pinconf_set(struct pinctrl_dev *pctldev,
			   unsigned pin, unsigned long *configs,
			   unsigned num_configs)
{
	return -ENOTSUPP;
}

static int n329_pinconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned group, unsigned long *config)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	*config = d->soc->groups[group].config;

	return 0;
}

static int n329_pinconf_group_set(struct pinctrl_dev *pctldev,
				 unsigned group, unsigned long *configs,
				 unsigned num_configs)
{
	struct n329_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);
	struct n329_group *g = &d->soc->groups[group];
	void __iomem *reg;
	unsigned pull, bank, pin, shift, i;
	int n;
	unsigned long config;

	for (n = 0; n < num_configs; n++) {
		config = configs[n];

		pull = CONFIG_TO_PULL(config);

		for (i = 0; i < g->npins; i++) {
			bank = PINID_TO_BANK(g->pins[i]);
			pin = PINID_TO_PIN(g->pins[i]);

			/* pull */
			if (config & PULL_PRESENT) {
				reg = d->gpio_base;
				reg += (bank * 0x10) + 0x04;
				shift = pin;
				if (pull)
					writel(readl(reg) | (1 << shift), reg);
				else
					writel(readl(reg) & ~(1 << shift), reg);
			}
		}

		/* Cache the config value for n329_pinconf_group_get() */
		g->config = config;

	}

	return 0;
}

static void n329_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				 struct seq_file *s, unsigned pin)
{
	/* not supported */
}

static void n329_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				       struct seq_file *s, unsigned group)
{
	unsigned long config;

	if (!n329_pinconf_group_get(pctldev, group, &config))
		seq_printf(s, "0x%lx", config);
}

static const struct pinconf_ops n329_pinconf_ops = {
	.pin_config_get = n329_pinconf_get,
	.pin_config_set = n329_pinconf_set,
	.pin_config_group_get = n329_pinconf_group_get,
	.pin_config_group_set = n329_pinconf_group_set,
	.pin_config_dbg_show = n329_pinconf_dbg_show,
	.pin_config_group_dbg_show = n329_pinconf_group_dbg_show,
};

static struct pinctrl_desc n329_pinctrl_desc = {
	.pctlops = &n329_pinctrl_ops,
	.pmxops = &n329_pinmux_ops,
	.confops = &n329_pinconf_ops,
	.owner = THIS_MODULE,
};

static int n329_pinctrl_parse_group(struct platform_device *pdev,
				   struct device_node *np, int idx,
				   const char **out_name)
{
	struct n329_pinctrl_data *d = platform_get_drvdata(pdev);
	struct n329_group *g = &d->soc->groups[idx];
	struct property *prop;
	const char *propname = "nuvoton,pinmux-ids";
	char *group;
	int length = strlen(np->name) + SUFFIX_LEN;
	unsigned i;
	u32 val;

	group = devm_kzalloc(&pdev->dev, length, GFP_KERNEL);
	if (!group)
		return -ENOMEM;
	if (of_property_read_u32(np, "reg", &val))
		snprintf(group, length, "%s", np->name);
	else
		snprintf(group, length, "%s.%d", np->name, val);
	g->name = group;

	prop = of_find_property(np, propname, &length);
	if (!prop)
		return -EINVAL;
	g->npins = length / sizeof(u32);

	g->pins = devm_kzalloc(&pdev->dev, g->npins * sizeof(*g->pins),
			       GFP_KERNEL);
	if (!g->pins)
		return -ENOMEM;

	g->muxsel = devm_kzalloc(&pdev->dev, g->npins * sizeof(*g->muxsel),
				 GFP_KERNEL);
	if (!g->muxsel)
		return -ENOMEM;

	of_property_read_u32_array(np, propname, g->pins, g->npins);
	for (i = 0; i < g->npins; i++) {
		g->muxsel[i] = MUXID_TO_MUXSEL(g->pins[i]);
		g->pins[i] = MUXID_TO_PINID(g->pins[i]);
	}
	if (out_name)
		*out_name = g->name;

	return 0;
}

static int n329_pinctrl_probe_dt(struct platform_device *pdev,
				struct n329_pinctrl_data *d)
{
	struct n329_pinctrl_soc_data *soc = d->soc;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct n329_function *f;
	const char *fn, *fnull = "";
	int i = 0, idxf = 0, idxg = 0;
	int ret;
	u32 val;

	child = of_get_next_child(np, NULL);
	if (!child) {
		dev_err(&pdev->dev, "no group is defined\n");
		return -ENOENT;
	}

	/* Count total non-gpio functions and groups */
	fn = fnull;
	for_each_child_of_node(np, child) {
		if (of_find_property(child, "gpio-controller", NULL))
			continue;
		soc->ngroups++;
		/* Skip pure pinconf node */
		if (of_property_read_u32(child, "reg", &val))
			continue;
		if (strcmp(fn, child->name)) {
			fn = child->name;
			soc->nfunctions++;
		}
	}

	soc->functions = devm_kzalloc(&pdev->dev, soc->nfunctions *
				      sizeof(*soc->functions), GFP_KERNEL);
	if (!soc->functions)
		return -ENOMEM;

	soc->groups = devm_kzalloc(&pdev->dev, soc->ngroups *
				   sizeof(*soc->groups), GFP_KERNEL);
	if (!soc->groups)
		return -ENOMEM;

	/* Count groups for each function */
	fn = fnull;
	f = &soc->functions[idxf];
	for_each_child_of_node(np, child) {
		if (of_find_property(child, "gpio-controller", NULL))
			continue;
		if (of_property_read_u32(child, "reg", &val))
			continue;
		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->name = fn = child->name;
		}
		f->ngroups++;
	};

	/* Get groups for each function */
	idxf = 0;
	fn = fnull;
	for_each_child_of_node(np, child) {
		if (of_find_property(child, "gpio-controller", NULL))
			continue;
		if (of_property_read_u32(child, "reg", &val)) {
			ret = n329_pinctrl_parse_group(pdev, child,
						      idxg++, NULL);
			if (ret)
				return ret;
			continue;
		}

		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->groups = devm_kzalloc(&pdev->dev, f->ngroups *
						 sizeof(*f->groups),
						 GFP_KERNEL);
			if (!f->groups)
				return -ENOMEM;
			fn = child->name;
			i = 0;
		}
		ret = n329_pinctrl_parse_group(pdev, child, idxg++,
					      &f->groups[i++]);
		if (ret)
			return ret;
	}

	return 0;
}

static struct device_node *n329_get_first_gpio(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *np;

	for_each_child_of_node(node, np) {
		if (of_find_property(np, "gpio-controller", NULL))
			return np;
	}

	return NULL;
}

int n329_pinctrl_probe(struct platform_device *pdev,
		      struct n329_pinctrl_soc_data *soc)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *gp;
	struct n329_pinctrl_data *d;
	struct clk *clk_mux;
	struct clk *clk_div;
	struct clk *clk_gate;
	unsigned sw_irq;
	int ret;

	/* We must have at least one child gpio node */
	gp = n329_get_first_gpio(pdev);
	if (!gp) {
		ret = -EINVAL;
		goto err;
	}

	/* Initialize gpio clocks */
	clk_mux = of_clk_get(gp, 0);
	clk_div = of_clk_get(gp, 1);
	clk_gate = of_clk_get(gp, 2);
	if (IS_ERR(clk_mux) || IS_ERR(clk_div) || IS_ERR(clk_gate)) {
		ret = -ENXIO;
		goto err;
	}
	clk_prepare_enable(clk_mux);
	clk_prepare_enable(clk_div);
	clk_prepare_enable(clk_gate);

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d) {
		ret = -ENOMEM;
		goto err;
	}

	d->dev = &pdev->dev;
	d->soc = soc;

	d->gpio_base = of_iomap(np, 0);
	d->gcr_base = of_iomap(np, 1);
	if (!d->gpio_base || !d->gcr_base) {
		ret = -EADDRNOTAVAIL;
		goto err_free;
	}

	platform_set_drvdata(pdev, d);

	ret = n329_pinctrl_probe_dt(pdev, d);
	if (ret) {
		dev_err(&pdev->dev, "pinctrl dt probe failed: %d\n", ret);
		goto err_unmapio;
	}

	d->gc.label = "n329-gpio";
	d->gc.base = 0;
	d->gc.ngpio = d->soc->npins;
	d->gc.owner = THIS_MODULE;

	d->gc.direction_input = n329_pinctrl_gpio_dir_in;
	d->gc.direction_output = n329_pinctrl_gpio_dir_out;
	d->gc.get = n329_pinctrl_gpio_get_value;
	d->gc.set = n329_pinctrl_gpio_set_value;
	d->gc.to_irq = n329_pinctrl_gpio_to_irq;
	d->gc.can_sleep = 0;

	d->gc.of_node = gp;

	/* Add GPIOs */
	ret = gpiochip_add(&d->gc);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register N329 gpio driver\n");
		goto err_unmapio;
	}

	/* Configure GPIO IRQs */
	for (sw_irq = 0; sw_irq < d->soc->npins; sw_irq++) {
		irq_set_chip_data(sw_irq + GPIO_IRQ_START, d);
		irq_set_chip(sw_irq + GPIO_IRQ_START, &n329_irqchip);
		set_irq_flags(sw_irq + GPIO_IRQ_START, IRQF_VALID);
	}
	d->hw_irq0 = irq_of_parse_and_map(gp, 0);
	d->hw_irq1 = irq_of_parse_and_map(gp, 1);
	d->hw_irq2 = irq_of_parse_and_map(gp, 2);
	d->hw_irq3 = irq_of_parse_and_map(gp, 3);
	setup_irq(d->hw_irq0, &n329_gpio_irq);
	setup_irq(d->hw_irq1, &n329_gpio_irq);
	setup_irq(d->hw_irq2, &n329_gpio_irq);
	setup_irq(d->hw_irq3, &n329_gpio_irq);

	/* Add pin control */
	n329_pinctrl_desc.pins = d->soc->pins;
	n329_pinctrl_desc.npins = d->soc->npins;
	n329_pinctrl_desc.name = dev_name(&pdev->dev);
	d->pctl = pinctrl_register(&n329_pinctrl_desc, &pdev->dev, d);
	if (!d->pctl) {
		dev_err(&pdev->dev, "Couldn't register N329 pinctrl driver\n");
		ret = gpiochip_remove(&d->gc);
		ret = -EINVAL;
		goto err_unmapio;
	}

	return 0;

err_unmapio:
	if (d->gcr_base)
		iounmap(d->gcr_base);
	if (d->gpio_base)
		iounmap(d->gpio_base);
err_free:
	devm_kfree(&pdev->dev, d);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(n329_pinctrl_probe);

int n329_pinctrl_remove(struct platform_device *pdev)
{
	struct n329_pinctrl_data *d = platform_get_drvdata(pdev);

	pinctrl_unregister(d->pctl);
	iounmap(d->gcr_base);

	return 0;
}
EXPORT_SYMBOL_GPL(n329_pinctrl_remove);
