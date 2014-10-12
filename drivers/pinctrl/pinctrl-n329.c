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

#define SUFFIX_LEN	4

struct n329_pinctrl_data {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *base;
	void __iomem *gpio_base;
	struct n329_pinctrl_soc_data *soc;
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
	u32 i;

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
	u8 bank, shift;
	u16 pin;
	u32 val, i;

	for (i = 0; i < g->npins; i++) {
		bank = PINID_TO_BANK(g->pins[i]);
		pin = PINID_TO_PIN(g->pins[i]);
		reg = d->base + 0x80 + (bank << 4);
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
	u8 pull, bank, shift;
	u16 pin;
	u32 i;
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

		/* cache the config value for n329_pinconf_group_get() */
		g->config = config;

	} /* for each config */

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
	u32 val, i;

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
	const char *gpio_compat = "nuvoton,n329-gpio";
	const char *fn, *fnull = "";
	int i = 0, idxf = 0, idxg = 0;
	int ret;
	u32 val;

	child = of_get_next_child(np, NULL);
	if (!child) {
		dev_err(&pdev->dev, "no group is defined\n");
		return -ENOENT;
	}

	/* Count total functions and groups */
	fn = fnull;
	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, gpio_compat))
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
		if (of_device_is_compatible(child, gpio_compat))
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
		if (of_device_is_compatible(child, gpio_compat))
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

int n329_pinctrl_probe(struct platform_device *pdev,
		      struct n329_pinctrl_soc_data *soc)
{
	struct device_node *np = pdev->dev.of_node;
	struct n329_pinctrl_data *d;
	int ret;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->dev = &pdev->dev;
	d->soc = soc;

	d->base = of_iomap(np, 0);
	if (!d->base)
		return -EADDRNOTAVAIL;

	d->gpio_base = of_iomap(np, 1);
	if (!d->gpio_base)
		return -EADDRNOTAVAIL;

	n329_pinctrl_desc.pins = d->soc->pins;
	n329_pinctrl_desc.npins = d->soc->npins;
	n329_pinctrl_desc.name = dev_name(&pdev->dev);

	platform_set_drvdata(pdev, d);

	ret = n329_pinctrl_probe_dt(pdev, d);
	if (ret) {
		dev_err(&pdev->dev, "dt probe failed: %d\n", ret);
		goto err;
	}

	d->pctl = pinctrl_register(&n329_pinctrl_desc, &pdev->dev, d);
	if (!d->pctl) {
		dev_err(&pdev->dev, "Couldn't register N329 pinctrl driver\n");
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	iounmap(d->base);
	return ret;
}
EXPORT_SYMBOL_GPL(n329_pinctrl_probe);

int n329_pinctrl_remove(struct platform_device *pdev)
{
	struct n329_pinctrl_data *d = platform_get_drvdata(pdev);

	pinctrl_unregister(d->pctl);
	iounmap(d->base);

	return 0;
}
EXPORT_SYMBOL_GPL(n329_pinctrl_remove);
