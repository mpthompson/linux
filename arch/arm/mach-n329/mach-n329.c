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

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/irqchip/n329.h>
#include <linux/reboot.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/sys_soc.h>
#include <linux/semaphore.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/system_misc.h>

#define HW_GCR_CHIPID 		0x00
#define HW_GCR_CHIPID_MASK	0x00ffffff
#define HW_GCR_CHIPID_N32905	0xfa5c30

#define N329_CHIP_REV_UNKNOWN	0xff

static u32 chipid;
static u32 socid;

/* Semaphore for preventing concurrent DMAC devices activity */
DEFINE_SEMAPHORE(dmac_sem);
EXPORT_SYMBOL(dmac_sem);

/* Semaphore for preventing concurrent FMI devices activity */
DEFINE_SEMAPHORE(fmi_sem);
EXPORT_SYMBOL(fmi_sem);

static void __iomem *wtcr_addr;

static void __init n32905_mcuzone_init(void)
{
	/* Do nothing for now */
}

static void __init n32905_demo_board_init(void)
{
	/* Do nothing for now */
}

static const char __init *n329_get_soc_id(void)
{
	struct device_node *np;
	void __iomem *gcr_base;

	np = of_find_compatible_node(NULL, NULL, "nuvoton,n329-gcr");
	gcr_base = of_iomap(np, 0);
	WARN_ON(!gcr_base);

	chipid = readl(gcr_base + HW_GCR_CHIPID);
	socid = chipid & HW_GCR_CHIPID_MASK;

	iounmap(gcr_base);
	of_node_put(np);

	switch (socid) {
	case HW_GCR_CHIPID_N32905:
		return "N32905";
	default:
		return "Unknown";
	}
}

static u32 __init n329_get_cpu_rev(void)
{
	return N329_CHIP_REV_UNKNOWN;
}

static const char __init *n329_get_revision(void)
{
	u32 rev = n329_get_cpu_rev();

	if (rev != N329_CHIP_REV_UNKNOWN)
		return kasprintf(GFP_KERNEL, "%d.%d", (rev >> 4) & 0xf,
				rev & 0xf);
	else
		return kasprintf(GFP_KERNEL, "%s", "Unknown");
}

#define HW_TMR_WTCR	0x1C

static int __init n329_restart_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "nuvoton,tmr");
	wtcr_addr = of_iomap(np, 0);
	if (!wtcr_addr)
		return -ENODEV;

	wtcr_addr += HW_TMR_WTCR;
	of_node_put(np);

	return 0;
}

static void __init n329_machine_init(void)
{
	struct device_node *root;
	struct device *parent;
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;
	int ret;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return;

	root = of_find_node_by_path("/");
	ret = of_property_read_string(root, "model", &soc_dev_attr->machine);
	if (ret)
		return;

	soc_dev_attr->family = "Nuvoton N329 Family";
	soc_dev_attr->soc_id = n329_get_soc_id();
	soc_dev_attr->revision = n329_get_revision();

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr);
		return;
	}

	parent = soc_device_to_device(soc_dev);

	if (of_machine_is_compatible("nuvoton,n32905-mcuzone"))
		n32905_mcuzone_init();
	if (of_machine_is_compatible("nuvoton,n32905-demo-board"))
		n32905_demo_board_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, parent);

	n329_restart_init();
}

/*
 * Reset the system. It is called by machine_restart().
 */
static void n329_restart(enum reboot_mode mode, const char *cmd)
{
	if (wtcr_addr) {

		/* XXX Turn off speaker. */

		/* XXX Turn off video out. */

		/* Turn off power and reset via the watchdog. */
		__raw_writel((__raw_readl(wtcr_addr) &
				~(3 << 4 | 1 << 10)) | 0x2C2,
				wtcr_addr);

		/* Delay for reset to occur. */
		mdelay(500);

		pr_err("Failed to assert the chip reset\n");

		/* Delay to allow the serial port to show the message. */
		mdelay(50);
	}

	/* We'll take a jump through zero as a poor second. */
	soft_restart(0);
}

static const char *n329_dt_compat[] __initdata = {
	"nuvoton,n32905",
	NULL,
};

DT_MACHINE_START(N329, "Nuvoton N329 (Device Tree)")
	.handle_irq = aic_handle_irq,
	.init_machine = n329_machine_init,
	.dt_compat = n329_dt_compat,
	.restart = n329_restart,
MACHINE_END
