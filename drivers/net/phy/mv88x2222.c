/*
 * drivers/net/phy/mv88x2222c
 *
 * Driver for Marvell Integrated Dual-port
 * Multi-speed Ethernet Transceiver 88x2222
 *
 * Copyright (c) 2015, 2016, 2020 Baikal Electronics JSC.
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
#include <linux/phy.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mdio.h>
#include <linux/marvell_phy.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

MODULE_DESCRIPTION("Marvell Ethernet Transceiver driver");
MODULE_LICENSE("Proprietary");

/* 31.F002 Line side mode (ch.3.1.2, pg.46) */
#define MV_MODE_LINE_SHF			8
#define MV_MODE_LINE_10GBR			(0x71UL << 8)
#define MV_MODE_LINE_10GBW			(0x74UL << 8)
#define MV_MODE_LINE_2GBX_AN_OFF		(0x76UL << 8)
#define MV_MODE_LINE_1GBR_AN_OFF		(0x72UL << 8)
#define MV_MODE_LINE_1GBR_AN_ON			(0x73UL << 8)
#define MV_MODE_LINE_SGMII_SYS_AN_OFF		(0x7CUL << 8)
#define MV_MODE_LINE_SGMII_SYS_AN_ON		(0x7DUL << 8)
#define MV_MODE_LINE_SGMII_NET_AN_OFF		(0x7EUL << 8)
#define MV_MODE_LINE_SGMII_NET_AN_ON		(0x7FUL << 8)
#define MV_MODE_LINE_DEFAULT			MV_MODE_LINE_10GBR
#define MV_MODE_LINE_OF_NAME			"mv,line-mode"

/* 31.F002 Host side mode (ch.3.1.2, pg.46) */
#define MV_MODE_HOST_SHF			0
#define MV_MODE_HOST_10GBR			(0x71UL << 0)
#define MV_MODE_HOST_10GBX2			(0x72UL << 0)
#define MV_MODE_HOST_10GBX4			(0x73UL << 0)
#define MV_MODE_HOST_2GBX_AN_OFF		(0x76UL << 0)
#define MV_MODE_HOST_1GBR_AN_OFF		(0x7AUL << 0)
#define MV_MODE_HOST_1GBR_AN_ON			(0x7BUL << 0)
#define MV_MODE_HOST_SGMII_SYS_AN_OFF		(0x7CUL << 0)
#define MV_MODE_HOST_SGMII_SYS_AN_ON		(0x7DUL << 0)
#define MV_MODE_HOST_SGMII_NET_AN_OFF		(0x7EUL << 0)
#define MV_MODE_HOST_SGMII_NET_AN_ON		(0x7FUL << 0)
#define MV_MODE_HOST_DEFAULT			MV_MODE_HOST_10GBR
#define MV_MODE_HOST_OF_NAME			"mv,host-mode"

/* 31.F402 Host side line muxing (ch.3.1.5, pg.48) */
#define MV_ATT_10GBX2_SHF			11
#define MV_ATT_10GBX2_LANE_0145			(0UL << 11)
#define MV_ATT_10GBX2_LANE_0123			(1UL << 11)
#define MV_ATT_10GBR_SHF			9
#define MV_ATT_10GBR_LANE_0246			(0UL << 9)
#define MV_ATT_10GBR_LANE_0123			(1UL << 9)
#define MV_ATT_2GBR_SHF				8
#define MV_ATT_2GBR_LANE_0246			(0UL << 8)
#define MV_ATT_2GBR_LANE_0123			(1UL << 8)
#define MV_ATT_1GBR_SHF				8
#define MV_ATT_1GBR_LANE_0246			(0UL << 8)
#define MV_ATT_1GBR_LANE_0123			(1UL << 8)
#define MV_ATT_DEFAULT				0
#define MV_ATT_OF_NAME				"mv,mux"

/* 31.F003 Software reset (ch.3.2 pg.50) */
#define MV_SW_RST_HOST_SHF			7
#define MV_SW_RST_HOST				(1UL << 7)
#define MV_SW_RST_LINE_SHF			15
#define MV_SW_RST_LINE				(1UL << 15)
#define MV_SW_RST_ALL				(MV_SW_RST_HOST | MV_SW_RST_LINE)

/* 31.F012 GPIO data */
#define MV_GPIO_TXDISABLE_DATA_SHF		8

/* 31.F013 Tristate Control */
#define MV_GPIO_TXDISABLE_OUTP_EN_SHF		8

/* 31.F016 Interrupt type 3 */
#define MV_GPIO_TXDISABLE_FN_SHF		3
#define MV_GPIO_TXDISABLE_FN_GPIO		0x1

/* Devices in package and registers */
#define MV_DEV_10GBW_IRQ_ENABLE			0x8000
#define MV_DEV_10GBW_IRQ_STATUS			0x8001
#define MV_DEV_10GBW_IRQ_REALTIME		0x8002

#define MV_DEV_10GBR_ANEG               0x2000
#define MV_DEV_10GBR_IRQ_ENABLE			0x8000
#define MV_DEV_10GBR_IRQ_STATUS			0x8001
#define MV_DEV_10GBR_IRQ_REALTIME		0x8002

#define MV_DEV_GBX_IRQ_ENABLE			0xA000
#define MV_DEV_GBX_IRQ_STATUS			0xA001
#define MV_DEV_GBX_IRQ_REALTIME			0xA002

#define MV_DEV_MISC_IRQ_ENABLE			0xF00A
#define MV_DEV_MISC_IRQ_STATUS			0xF00B

#define MV_DEV_GPIO_DATA			0xF012
#define MV_DEV_GPIO_TRISTATE_CTL		0xF013
#define MV_DEV_GPIO_INTERRUPT_TYPE_3		0xF016

#define MV_DEV_CHIP_HOST_LINE			0xF002
#define MV_DEV_CHIP_RESET			0xF003
#define MV_DEV_CHIP_MUX				0xF402
#define MV_DEV_CHIP_IRQ_STATUS			0xF420
#define MV_DEV_CHIP_IRQ_CONTROL			0xF421

#define MV_RESET_DELAY_US			500

struct mode
{
    unsigned int mode_num;
    char mode_name[16];
}; 

static struct mode line_modes[] =
{
        {MV_MODE_LINE_10GBR, "KR"},
        {MV_MODE_LINE_10GBW, "10GBW"},
        {MV_MODE_LINE_2GBX_AN_OFF, "2GBX_AN_OFF"},
        {MV_MODE_LINE_1GBR_AN_OFF, "1GBR_AN_OFF"},
        {MV_MODE_LINE_1GBR_AN_ON, "1GBR_AN_ON"},
        {MV_MODE_LINE_SGMII_SYS_AN_OFF, "SGMII_SYS_AN_OFF"},
        {MV_MODE_LINE_SGMII_SYS_AN_ON, "SGMI_SYS_AN_ON"},
        {MV_MODE_LINE_SGMII_NET_AN_OFF, "SMGII_NET_AN_OFF"},
        {MV_MODE_LINE_SGMII_NET_AN_ON, "SGMII_NET_AN_ON"}
};

static struct mode host_modes[] =
{
        {MV_MODE_HOST_10GBR, "KR"},
        {MV_MODE_HOST_10GBX2, "10GBX2"},
        {MV_MODE_HOST_10GBX4, "KX4"},
        {MV_MODE_HOST_2GBX_AN_OFF, "2GBX_AN_OFF"},
        {MV_MODE_HOST_1GBR_AN_OFF, "1GBR_AN_OFF"},
        {MV_MODE_HOST_1GBR_AN_ON, "1GBR_AN_ON"},
        {MV_MODE_HOST_SGMII_SYS_AN_OFF, "SGMII_SYS_AN_OFF"},
        {MV_MODE_HOST_SGMII_SYS_AN_ON, "SGMII_SYS_AN_ON"},
        {MV_MODE_HOST_SGMII_NET_AN_OFF, "SGMII_NE_AN_OFF"},
        {MV_MODE_HOST_SGMII_NET_AN_ON, "SGMII_NET_AN_ON"}
};

struct mv88x2222_data {
	int irq;
	int rst_active_low, irq_active_low;
	int line_mode, host_mode, mux;
};

static void *marvell_of_get_data(struct phy_device *phydev)
{
	struct device_node *np = phydev->mdio.dev.of_node;
	struct mv88x2222_data *pdata;
	enum of_gpio_flags flags;
	int ret;
	char mode[32];
	unsigned int i = 0;
	const char *pm = mode;

	pdata = devm_kzalloc(&phydev->mdio.dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	ret =  of_get_named_gpio_flags(np, "irq-pin", 0, &flags);
	if (ret >= 0) {
		pdata->irq = ret;
		pdata->irq_active_low = flags & OF_GPIO_ACTIVE_LOW;
		dev_info(&phydev->mdio.dev, "irq gpio pin=%d", ret);
	}

	pdata->line_mode = MV_MODE_LINE_DEFAULT;
	ret = of_property_read_string(np, MV_MODE_LINE_OF_NAME, &pm);
	if (!ret) {
		for(i = 0; i < sizeof(line_modes) / sizeof(struct mode); ++i) {
		    if(strcasecmp(line_modes[i].mode_name, pm) == 0) {
			pdata->line_mode = line_modes[i].mode_num;
			break;
		    }
		}
	}

	pdata->host_mode = MV_MODE_HOST_DEFAULT;
	ret = of_property_read_string(np, MV_MODE_HOST_OF_NAME, &pm);
	if (!ret) {
		for(i = 0; i < sizeof(host_modes) / sizeof(struct mode); ++i) {
		    if(strcasecmp(host_modes[i].mode_name, pm) == 0) {
			pdata->host_mode = host_modes[i].mode_num;
			break;
		    }
		}
	}

	/* Default value at now */
	pdata->mux = MV_ATT_DEFAULT;

	return pdata;
}

static int marvell_soft_reset(struct phy_device *phydev) {
	int ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_CHIP_RESET,
				MV_SW_RST_ALL);
	int count = 50;

	if (ret) {
		dev_warn(&phydev->mdio.dev, "software reset failed\n");
		return ret;
	}

	do {
		usleep_range(MV_RESET_DELAY_US, MV_RESET_DELAY_US + 100);
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_CHIP_RESET);
	} while ((ret & MV_SW_RST_ALL) || count--);

	return 0;
}

static int marvell_config_init(struct phy_device *phydev)
{
	struct mv88x2222_data *pdata = phydev->priv;
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_CHIP_HOST_LINE,
			    pdata->line_mode | pdata->host_mode);
	if (ret)
		return 1;

        phydev->speed = SPEED_10000;
        phydev->duplex = DUPLEX_FULL;

	/*
	 * This must be done after mode set;
	 */
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_CHIP_HOST_LINE);
	ret |= 0x8000;
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_CHIP_HOST_LINE, ret);
	
	marvell_soft_reset(phydev);

	dev_info(&phydev->mdio.dev, "phy(%d, %x)=%x\n", MDIO_MMD_VEND2,
			MV_DEV_CHIP_HOST_LINE,
			phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_CHIP_HOST_LINE));

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
			phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
			phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
			phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Backplane_BIT,
			phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
			phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Pause_BIT, phydev->supported, 1);

	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->interface = PHY_INTERFACE_MODE_XGMII;
	phydev->duplex = DUPLEX_FULL;

	switch (pdata->line_mode) {
	case MV_MODE_LINE_10GBR:
	case MV_MODE_LINE_10GBW:
		phydev->speed = SPEED_10000;
		break;
	case MV_MODE_LINE_2GBX_AN_OFF:
		phydev->speed = SPEED_2500;
		break;
	default:
		phydev->speed = SPEED_1000;
		break;
	}

	return 0;
}

static int marvell_adjust_tx(struct phy_device *phydev)
{
	int reg;
	int line_link = 1;

	/* Switch tristate to "write to pin/read from register" */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_TRISTATE_CTL);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_TRISTATE_CTL,\
	reg | (1 << MV_GPIO_TXDISABLE_OUTP_EN_SHF));

	/* Switch off TX_DISABLE */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_DATA);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_DATA, reg & \
		~(1 << MV_GPIO_TXDISABLE_DATA_SHF));

	/* Check if opto-cable is plugged */
	reg = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_STAT1);
	if ((reg < 0) || !(reg & MDIO_STAT1_LSTATUS))
		line_link = 0;

	if (line_link) {
		/* It's fine */
		return 0;

	} else {
		/* Switch on TX_DISABLE */
		reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_DATA);
		phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_DATA, reg | \
			(1 << MV_GPIO_TXDISABLE_DATA_SHF));
	}

	return 1;
}

static int marvell_update_link(struct phy_device *phydev)
{
	int reg;
	int host_mode = 0;
	int line_mode = 0;

	/* Default link status */
	phydev->link = 1;

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_CHIP_HOST_LINE);
	if (reg < 0)
	{
		phydev->link = 0;
		return 0;
	}

	host_mode = reg & 0x007F;
	line_mode = reg & 0x7F00;

	/* Read host link status */
	if (host_mode == MV_MODE_HOST_10GBX4)
		reg = phy_read_mmd(phydev, MDIO_MMD_PHYXS, 0x1001);
	else
		reg = phy_read_mmd(phydev, MDIO_MMD_PHYXS, MDIO_STAT1);

	if ((reg < 0) || !(reg & MDIO_STAT1_LSTATUS))
		phydev->link = 0;

	/* Read line link status */
	if (line_mode == MV_MODE_LINE_10GBR)
		reg = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_STAT1);
	else
		reg = phy_read_mmd(phydev, MDIO_MMD_PCS, 0x2001);
    
	if ((reg < 0) || !(reg & MDIO_STAT1_LSTATUS))
		phydev->link = 0;

    	/* 
     	 * PMAPMD link status is always broken
     	 * later we need to update this driver;
     	 */
	reg = marvell_adjust_tx(phydev);
	if (reg < 0)
		phydev->link = 0;

	return 0;
}

static int marvell_read_status(struct phy_device *phydev) 
{
	int reg;

    	/* Update the link, but return if there was an error */
	reg = marvell_update_link(phydev);
	if (reg < 0)
		return reg;

	/* Read line control reg */
	reg = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1);
	if (reg < 0)
		return reg;

	return 0;
}

static int marvell_config_aneg(struct phy_device *phydev)
{
	linkmode_copy(phydev->advertising, phydev->supported);

	return 0;
}

static int marvell_probe(struct phy_device *phydev)
{
	struct mv88x2222_data *pdata = NULL;
	int reg = 0;

	if (phydev->mdio.dev.of_node)
		pdata = marvell_of_get_data(phydev);

	if (!pdata) {
		dev_err(&phydev->mdio.dev, "No PHY platform data\n");
		return -ENODEV;
	}

	phydev->priv = pdata;
	dev_info(&phydev->mdio.dev, "probed %s at 0x%02x\n",
		 phydev->drv->name, phydev->mdio.addr);
    	reg = phy_read_mmd(phydev, MDIO_MMD_PCS, 0x0002);
	
    return 0;
}

static int marvell_suspend(struct phy_device *phydev)
{
	int reg;
	mutex_lock(&phydev->lock);

	/* Switch tristate to "write to pin/read from register" */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_TRISTATE_CTL);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_TRISTATE_CTL,\
	reg | (1 << MV_GPIO_TXDISABLE_OUTP_EN_SHF));

	/* Switch on TX_DISABLE */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_DATA);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_DEV_GPIO_DATA, reg | \
		(1 << MV_GPIO_TXDISABLE_DATA_SHF));
	/* TBD Probably switch to lowpower mode */

	mutex_unlock(&phydev->lock);

	return 0;
}

static int marvell_match_phy_device(struct phy_device *phydev)
{
	unsigned int phy_id = phydev->c45_ids.device_ids[MDIO_MMD_PCS] & MARVELL_PHY_ID_MASK;
	
    return  (phy_id == MARVELL_PHY_ID_88X2222) || (phy_id == MARVELL_PHY_ID_88X2222R);
}

static struct phy_driver marvell_drivers[] = {
	{
		.phy_id = MARVELL_PHY_ID_88X2222,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88X2222",
		.features = 0,
		.config_init = marvell_config_init,
		.config_aneg = marvell_config_aneg,
		.probe = marvell_probe,
		.match_phy_device = marvell_match_phy_device,
		.read_status = marvell_read_status,
		.soft_reset = marvell_soft_reset,
		.resume = genphy_resume,
		.suspend = marvell_suspend,
	},
};
module_phy_driver(marvell_drivers);

static struct mdio_device_id __maybe_unused marvell_tbl[] = {
	{ MARVELL_PHY_ID_88X2222, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88X2222R, MARVELL_PHY_ID_MASK },
	{ }
};
MODULE_DEVICE_TABLE(mdio, marvell_tbl);
