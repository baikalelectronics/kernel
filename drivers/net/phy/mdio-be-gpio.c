/*
 * Baikal Electronics SFP+ mezzanine card MDIO bus driver
 * Supports OpenFirmware.
 *
 * Based on Bitbanged MDIO support driver.
 * drivers/net/phy/mdio-bitbang.c
 *
 * Author: Scott Wood <scottwood@freescale.com>
 * Copyright (c) 2007 Freescale Semiconductor
 *
 * Based on CPM2 MDIO code which is:
 *
 * Copyright (c) 2003 Intracom S.A.
 *  by Pantelis Antoniou <panto@intracom.gr>
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * Paritaly based on GPIO based MDIO bitbang driver.
 * drivers/net/phy/mdio-gpio.c
 *
 * Copyright (c) 2015 Baikal Electronics JSC.
 *
 * Author:
 *   Dmitry Dunaev <dmitry.dunaev@baikalelectronics.ru>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Baikal Electronics JSC nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mdio-bitbang.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/clk.h>

#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_mdio.h>

#define MDIO_READ 			2
#define MDIO_WRITE 			1

#define MDIO_C45			(1<<15)
#define MDIO_C45_ADDR		(MDIO_C45 | 0)
#define MDIO_C45_READ		(MDIO_C45 | 3)
#define MDIO_C45_WRITE		(MDIO_C45 | 1)
#define MDIO_C45_READ_INC	(MDIO_C45 | 2)

/*
 * Minimum MDC period is 400 ns, plus some margin for error.
 * MDIO_DELAY is done twice per period.
 * Baikal-T SoC GPIO pins trigger clock is 1 MHz.
 */
#define MDIO_DELAY 14

/*
 * The PHY may take up to 300 ns to produce data, plus some margin
 * for error.
 * Baikal-T SoC GPIO pins trigger clock is 1 MHz.
 */
#define MDIO_READ_DELAY_US	10
#define MDIO_RESET_DELAY_US	100

/*
 * Driver specific defines
 */
#define DRIVER_NAME	"Baikal Electronics mezzanine card MDIO bus driver"
#define DRIVER_VERSION	"1.04a"
#define DRIVER_DEV	"be-mdio"

/* Default GPIO trigger freq is 1 MHz */
#define MDIO_TRIG_FREQ	1000000

/*
 * Basic driver function
 */
struct be_mdio_data {
	struct phy_device *phydev;
	struct mii_bus *mii;
	struct clk *clk;
	int mdc, mdio, mdo, rst;
	int mdc_active_low, mdio_active_low;
	int mdo_active_low, rst_active_low;
	unsigned int delay, read_delay, reset_delay;
    /* PHY addresses to be ignored when probing */
    unsigned int phy_mask;
    /* IRQ mask */
	int irqs[PHY_MAX_ADDR];
};

/*
 * Physical level of MDIO bus
 */
static inline void be_mdio_dir(struct be_mdio_data *data, int dir)
{
	if (data->mdo >= 0) {
		/* Separate output pin. Always set its value to high
		 * when changing direction. If direction is input,
		 * assume the pin serves as pull-up. If direction is
		 * output, the default value is high.
		 */
		gpio_set_value(data->mdo, 1 ^ data->mdo_active_low);
		return;
	}

	if (dir)
		gpio_direction_output(data->mdio, 1 ^ data->mdio_active_low);
	else
		gpio_direction_input(data->mdio);
}

static inline int be_mdio_get(struct be_mdio_data *data)
{
	return gpio_get_value(data->mdio) ^ data->mdio_active_low;
}

static inline void be_mdio_set(struct be_mdio_data *data, int what)
{
	if (data->mdo >= 0)
		gpio_set_value(data->mdo, what ^ data->mdo_active_low);
	else
		gpio_set_value(data->mdio, what ^ data->mdio_active_low);
}

static inline void be_mdc_set(struct be_mdio_data *data, int what)
{
	gpio_set_value(data->mdc, what ^ data->mdc_active_low);
}

/*
 * Logical level of MDIO bus
 */

/* MDIO must already be configured as output. */
static void be_mdio_send_bit(struct be_mdio_data *data, int val) {
	be_mdio_set(data, val);
	//ndelay(MDIO_DELAY);	//udelay(1..3);ndelay(400);
	udelay(MDIO_DELAY);
	be_mdc_set(data, 1);
	//ndelay(MDIO_DELAY);	//udelay(1..3);ndelay(400);
	udelay(MDIO_DELAY);
	be_mdc_set(data, 0);
}

/* MDIO must already be configured as output. */
/*static void be_mdio_send_ta(struct be_mdio_data *data, int val) {
	be_mdio_set(data, val);
	mdelay(data->read_delay);
	be_mdc_set(data, 1);
	mdelay(data->read_delay);
	be_mdc_set(data, 0);
}*/

/* MDIO must already be configured as input. */
static int be_mdio_get_bit(struct be_mdio_data *data) {
	udelay(MDIO_DELAY);
	be_mdc_set(data, 1);
	udelay(MDIO_DELAY);
	be_mdc_set(data, 0);
	return be_mdio_get(data);
}

/* MDIO must already be configured as output. */
static void be_mdio_send_num(struct be_mdio_data *data, u16 val, int bits) {
	int i;

	be_mdio_dir(data, 1);

	for (i = bits - 1; i >= 0; i--)
		be_mdio_send_bit(data, (val >> i) & 1);
}

/* MDIO must already be configured as input. */
static u16 be_mdio_get_num(struct be_mdio_data *data, int bits) {
	int i;
	u16 ret = 0;

	be_mdio_dir(data, 0);

	for (i = bits - 1; i >= 0; i--) {
		ret <<= 1;
		ret |= be_mdio_get_bit(data);
	}
	return ret;
}

/*
 * Utility to send the preamble, address, and
 * register (common to read and write).
 */
static void be_mdio_cmd(struct be_mdio_data *data, int op, u8 phy, u8 reg) {
	int i;

	be_mdio_dir(data, 1);
	/*
	 * Send a 32 bit preamble ('1's) with an extra '1' bit for good
	 * measure.  The IEEE spec says this is a PHY optional
	 * requirement. This means that we are doing more preambles
	 * than we need, but it is safer and will be much more robust.
	 */
	for (i = 0; i < 32; i++)
		be_mdio_send_bit(data, 1);
	/*
	 * Send the start bit (01) and the read opcode (10) or write (10).
	 * Clause 45 operation uses 00 for the start and 11, 10 for
	 * read/write.
	 */
	be_mdio_send_bit(data, 0);
	if (op & MDIO_C45)
		be_mdio_send_bit(data, 0);
	else
		be_mdio_send_bit(data, 1);
	be_mdio_send_bit(data, (op >> 1) & 1);
	be_mdio_send_bit(data, (op >> 0) & 1);

	be_mdio_send_num(data, phy, 5);
	be_mdio_send_num(data, reg, 5);
}

/* In clause 45 mode all commands are prefixed by MDIO_ADDR to specify the
   lower 16 bits of the 21 bit address. This transfer is done identically to a
   MDIO_WRITE except for a different code. To enable clause 45 mode or
   MII_ADDR_C45 into the address. Theoretically clause 45 and normal devices
   can exist on the same bus. Normal devices should ignore the MDIO_ADDR
   phase. */
static int be_mdio_cmd_addr(struct be_mdio_data *data, int phy, u32 addr) {
	unsigned int dev_addr = (addr >> 16) & 0x1F;
	unsigned int reg = addr & 0xFFFF;

	be_mdio_cmd(data, MDIO_C45_ADDR, phy, dev_addr);

	/* send the turnaround (10) */
	be_mdio_send_bit(data, 1);
	be_mdio_send_bit(data, 0);

	be_mdio_send_num(data, reg, 16);

	be_mdio_dir(data, 0);
	be_mdio_get_bit(data);

	return dev_addr;
}

static int be_mdio_read(struct mii_bus *bus, int phy, int reg) {
	struct be_mdio_data *data = bus->priv;
	int ret, i;

	if (reg & MII_ADDR_C45) {
		reg = be_mdio_cmd_addr(data, phy, reg);
		be_mdio_cmd(data, MDIO_C45_READ, phy, reg);
	} else
		be_mdio_cmd(data, MDIO_READ, phy, reg);

	be_mdio_dir(data, 0);

	/* check the turnaround bit: the PHY should be driving it to zero */
	if (be_mdio_get_bit(data) != 0) {
		/* PHY didn't drive TA low -- flush any bits it may be trying to send. */
		for (i = 0; i < 32; i++)
			be_mdio_get_bit(data);

		return 0xffff;
	}

	ret = be_mdio_get_num(data, 16);
	be_mdio_get_bit(data);
	return ret;
}

static int be_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val) {
	struct be_mdio_data *data = bus->priv;

	if (reg & MII_ADDR_C45) {
		reg = be_mdio_cmd_addr(data, phy, reg);
		be_mdio_cmd(data, MDIO_C45_WRITE, phy, reg);
	} else
		be_mdio_cmd(data, MDIO_WRITE, phy, reg);

	/* send the turnaround (10) */
	be_mdio_send_bit(data, 1);
	be_mdio_send_bit(data, 0);

	be_mdio_send_num(data, val, 16);

	be_mdio_dir(data, 0);
	be_mdio_get_bit(data);

	return 0;
}

static int __maybe_unused be_mdio_reset(struct mii_bus *bus) {
	struct be_mdio_data *data = bus->priv;

	if (data->rst < 0)
		return 0;

	gpio_set_value(data->rst, 1 ^ data->rst_active_low);
	mdelay(data->reset_delay);

	gpio_set_value(data->rst, 0 ^ data->rst_active_low);
	mdelay(data->reset_delay);

	return 0;
}

/*
 * MDIO bus open firmware data
 */
static void *be_mdio_of_get_data(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct be_mdio_data *pdata;
	enum of_gpio_flags flags;
	unsigned int freq = 0; 
	int ret;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	ret =  of_get_named_gpio_flags(np, "mdc-pin", 0, &flags);
	if (ret < 0)
		return NULL;

	pdata->mdc = ret;
	pdata->mdc_active_low = flags & OF_GPIO_ACTIVE_LOW;

	ret =  of_get_named_gpio_flags(np, "mdio-pin", 0, &flags);
	if (ret < 0)
		return NULL;
	pdata->mdio = ret;
	pdata->mdio_active_low = flags & OF_GPIO_ACTIVE_LOW;

	pdata->mdo = -1;
	ret = of_get_named_gpio_flags(np, "mdo-pin", 0, &flags);
	if (ret >= 0) {
		pdata->mdo = ret;
		pdata->mdo_active_low = flags & OF_GPIO_ACTIVE_LOW;
	}

	pdata->rst = -1;
	ret =  of_get_named_gpio_flags(np, "rst-pin", 0, &flags);
	if (ret >= 0) {
		pdata->rst = ret;
		pdata->rst_active_low = flags & OF_GPIO_ACTIVE_LOW;
	}

	pdata->clk = of_clk_get(np, 0);

	if (IS_ERR(pdata->clk))
		of_property_read_u32(pdev->dev.of_node, "clock-frequency", &freq);
	else
		freq =  clk_get_rate(pdata->clk);

	if (!freq)
		freq = MDIO_TRIG_FREQ;

	ret = 1000000 / freq;

	/* Timing */
	pdata->read_delay = (ret > MDIO_READ_DELAY_US) ? ret : MDIO_READ_DELAY_US;
	pdata->reset_delay = (ret > MDIO_RESET_DELAY_US) ?  ret : MDIO_RESET_DELAY_US;

	return pdata;
}

/*
 * MDIO bus init
 */
static struct mii_bus *be_mdio_bus_init(struct device *dev, struct be_mdio_data *pdata, int bus_id) {
	struct mii_bus *bus;
	int i;

	bus = mdiobus_alloc();
	if (!bus) {
		dev_err(dev, "Unable to allocate MDIO bus\n");
		goto error;
	}

	bus->read = be_mdio_read;
	bus->write = be_mdio_write;
	bus->priv = pdata;

	bus->name = "Baikal GPIO MDIO bus";

	bus->phy_mask = pdata->phy_mask;
	memcpy(bus->irq, pdata->irqs, PHY_MAX_ADDR);
	bus->parent = dev;

	if (bus->phy_mask == ~0) {
		dev_err(dev, "All PHY's are masked - nothing to attach\n");
		goto error_free_bus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		if (!bus->irq[i])
			bus->irq[i] = PHY_POLL;

	snprintf(bus->id, MII_BUS_ID_SIZE, "mdio-gpio%d", bus_id);

	if (devm_gpio_request(dev, pdata->mdc, "mdc")) {
		dev_err(dev, "MDC line (gpio%d) request failed\n", pdata->mdc);
		goto error_free_bus;
	}

	if (devm_gpio_request(dev, pdata->mdio, "mdio")){
		dev_err(dev, "MDIO line (gpio%d) request failed\n", pdata->mdc);
		goto error_free_bus;
	}

	if (pdata->mdo >= 0) {
		if (devm_gpio_request(dev, pdata->mdo, "mdo"))
			goto error_free_bus;
		gpio_direction_output(pdata->mdo, 1);
		gpio_direction_input(pdata->mdio);
	}

	if (pdata->rst >= 0) {
		if (devm_gpio_request(dev, pdata->rst, "rst"))
			pdata->rst= -1;
		else 
			gpio_direction_output(pdata->rst, 0);
	}

	gpio_direction_output(pdata->mdc, 0);

	dev_set_drvdata(dev, bus);

	return bus;

error_free_bus:
	mdiobus_free(bus);
error:
	return NULL;
}

static int be_mdio_probe(struct platform_device *pdev)
{
	struct be_mdio_data *pdata;
	struct mii_bus *bus;
	int ret, bus_id;

	if (pdev->dev.of_node) {
		pdata = be_mdio_of_get_data(pdev);
		bus_id = of_alias_get_id(pdev->dev.of_node, "mdio-gpio");
		if (bus_id < 0) {
			dev_warn(&pdev->dev, "failed to get alias id\n");
			bus_id = 0;
		}
	} else {
		pdata = dev_get_platdata(&pdev->dev);
		bus_id = pdev->id;
	}

	if (!pdata) {
		dev_err(&pdev->dev, "No MDIO bus platform data\n");
		return -ENODEV;
	}

	bus = be_mdio_bus_init(&pdev->dev, pdata, bus_id);
	if (!bus) {
		dev_err(&pdev->dev, "MDIO bus init failed\n");
		return -ENODEV;
	}

	if (pdev->dev.of_node)
		ret = of_mdiobus_register(bus, pdev->dev.of_node);
	else
		ret = mdiobus_register(bus);

	if (ret) {
		dev_err(&pdev->dev, "MDIO bus register failed\n");
		goto err_mdiobus_register;
	}

	// bus->reset = be_mdio_reset;

	pdata->mii = bus;
	dev_info(&pdev->dev, "MDIO ptr=%p\n", bus);

	dev_info(&pdev->dev, DRIVER_NAME);
	dev_info(&pdev->dev, "Version: " DRIVER_VERSION);

	return 0;

err_mdiobus_register:
	mdiobus_free(bus);

	return ret;
}

static int be_mdio_remove(struct platform_device *pdev)
{

	struct mii_bus *bus = dev_get_drvdata(&pdev->dev);
	mdiobus_unregister(bus);
	mdiobus_free(bus);

	return 0;
}

static struct of_device_id be_mdio_of_match[] = {
	{ .compatible = "be,mdio-gpio", },
	{ /* sentinel */ }
};

static struct platform_driver be_mdio_driver = {
	.probe = be_mdio_probe,
	.remove = be_mdio_remove,
	.driver		= {
		.name	= "be-mdio",
		.of_match_table = be_mdio_of_match,
	},
};

module_platform_driver(be_mdio_driver);

MODULE_ALIAS("platform:be-mdio");
MODULE_AUTHOR("Dmitry Dunaev");
MODULE_LICENSE("Proprinetary");
MODULE_VERSION(DRIVER_VERSION);
MODULE_DESCRIPTION(DRIVER_NAME);
