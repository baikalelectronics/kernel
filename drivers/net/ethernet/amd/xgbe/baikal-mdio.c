/*
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 *
 * This file is free software; you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License 2: Modified BSD
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/clk.h>

#include "xgbe.h"
#include "xgbe-common.h"

#ifndef VR_XS_PMA_MII_Gen5_MPLL_CTRL
#define VR_XS_PMA_MII_Gen5_MPLL_CTRL                    0x807A
#endif
#define VR_XS_PMA_MII_Gen5_MPLL_CTRL_REF_CLK_SEL_bit    (1 << 13)
#define VR_XS_PCS_DIG_CTRL1                             0x8000
#define VR_XS_PCS_DIG_CTRL1_VR_RST_Bit                  MDIO_CTRL1_RESET
#define SR_XC_or_PCS_MMD_Control1                       MDIO_CTRL1
#define SR_XC_or_PCS_MMD_Control1_RST_Bit               MDIO_CTRL1_RESET
#define DWC_GLBL_PLL_MONITOR                            0x8010
#define SDS_PCS_CLOCK_READY_mask                        0x1C
#define SDS_PCS_CLOCK_READY_bit                         0x10
#define VR_XS_PMA_MII_ENT_GEN5_GEN_CTL                  0x809C
#define VR_XS_PMA_MII_ENT_GEN5_GEN_CTL_LANE_MODE_KX4    (4 << 0)
#define VR_XS_PMA_MII_ENT_GEN5_GEN_CTL_LANE_MODE_MASK   0x0007
#define VR_XS_PMA_MII_ENT_GEN5_GEN_CTL_LINK_WIDTH_4     (2 << 8)
#define VR_XS_PMA_MII_ENT_GEN5_GEN_CTL_LINK_WIDTH_MASK  0x0700
#define VR_XS_OR_PCS_MMD_DIGITAL_CTL1_VR_RST            (1 << 15)

#define DELAY_COUNT     50

static int be_xgbe_an_restart_kr_training(struct xgbe_prv_data *pdata)
{
	int reg = 0;

	DBGPR("%s\n", __FUNCTION__);

	/* Restart training */
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, 0x0096, 3);
	msleep(500);
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, 0x0096, 1);
	
	/* The worse case when training continue till 500ms */
	msleep(500);

	reg = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, 0x0097);
	/* Check training failure */
	if (reg & (1 << 3))
		return -1;

	/* Success */
	return 0;
}

static int be_xgbe_an_enable_kr_training(struct xgbe_prv_data *pdata)
{
	DBGPR("%s\n", __FUNCTION__);
	
	/* Enable training */
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, 0x0096, 2);
	
	return 0;
}

static int be_xgbe_phy_pcs_power_cycle(struct xgbe_prv_data *pdata)
{
	int ret;
	DBGPR("%s\n", __FUNCTION__);

	ret = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);

	ret |= MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, ret);

	usleep_range(75, 100);

	ret &= ~MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, ret);

	return 0;
}

static int be_xgbe_phy_xgmii_mode_kx4(struct xgbe_prv_data *pdata)
{
	int  ret, count;

	DBGPR_MDIO("%s\n", __FUNCTION__);

	/* Write 2'b01 to Bits[1:0] of SR PCS Control2 to set the xpcx_kr_0
	 * output to 0.
	 */
	ret = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);

	ret &= ~MDIO_PCS_CTRL2_TYPE;
	ret |= MDIO_PCS_CTRL2_10GBX;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL2, ret);

	/* Set Bit 13 SR PMA MMD Control1 Register (for back plane) to 1. */
	ret = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_CTRL1);

	ret |= 0x2000;
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, MDIO_CTRL1, ret);

	/* Set LANE_MODE TO KX4 (4). */
	ret = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, VR_XS_PMA_MII_ENT_GEN5_GEN_CTL);

	ret &= ~VR_XS_PMA_MII_ENT_GEN5_GEN_CTL_LANE_MODE_MASK;
	ret |= VR_XS_PMA_MII_ENT_GEN5_GEN_CTL_LANE_MODE_KX4;
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, VR_XS_PMA_MII_ENT_GEN5_GEN_CTL, ret);

	/* Set LANE_WIDTH (2) 4 lanes per link. */
	ret = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, VR_XS_PMA_MII_ENT_GEN5_GEN_CTL);

	ret &= ~VR_XS_PMA_MII_ENT_GEN5_GEN_CTL_LINK_WIDTH_MASK;
	ret |= VR_XS_PMA_MII_ENT_GEN5_GEN_CTL_LINK_WIDTH_4;
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, VR_XS_PMA_MII_ENT_GEN5_GEN_CTL, ret);

	/* Initiate Software Reset. */
	ret = XMDIO_READ(pdata, MDIO_MMD_PCS, VR_XS_PCS_DIG_CTRL1);

	ret |= VR_XS_OR_PCS_MMD_DIGITAL_CTL1_VR_RST;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, VR_XS_PCS_DIG_CTRL1, ret);

	/* Wait until reset done. */
	count = DELAY_COUNT;
	do {
		msleep(20);
		ret = XMDIO_READ(pdata, MDIO_MMD_PCS, VR_XS_PCS_DIG_CTRL1);
	} while (!!(ret & VR_XS_OR_PCS_MMD_DIGITAL_CTL1_VR_RST) && --count);

	if (ret & VR_XS_OR_PCS_MMD_DIGITAL_CTL1_VR_RST)
		return -ETIMEDOUT;

	return 0;
}

static int be_xgbe_phy_xgmii_mode_kr(struct xgbe_prv_data *pdata)
{
	int ret;
	DBGPR("%s\n", __FUNCTION__);
	
	/* Enable KR training */
	ret = be_xgbe_an_enable_kr_training(pdata);
	if (ret < 0)
		return ret;

	/* Set PCS to KR/10G speed */
	ret = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);

	ret &= ~MDIO_PCS_CTRL2_TYPE;
	ret |= MDIO_PCS_CTRL2_10GBR;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL2, ret);

	ret = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);

	ret &= ~MDIO_CTRL1_SPEEDSEL;
	ret |= MDIO_CTRL1_SPEED10G;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, ret);

	ret = be_xgbe_phy_pcs_power_cycle(pdata);
	if (ret < 0)
    		return ret;

	return 0;
}

static int be_xgbe_phy_xgmii_mode(struct xgbe_prv_data *pdata)
{
    struct device *dev = pdata->dev;
    char mode[32];
    const char *pm = mode;

    if(!of_property_read_string(dev->of_node, "be,pcs-mode", &pm)) {
        if(strcasecmp(pm, "KX4") == 0){
            DBGPR("xgbe: mode KX4 = 0x%X function: %s\n", mode, __FUNCTION__);
            return be_xgbe_phy_xgmii_mode_kx4(pdata);
        }
    }

    DBGPR("xgbe: mode KR = 0x%X function: %s\n", mode, __FUNCTION__);

    return be_xgbe_phy_xgmii_mode_kr(pdata);
}

static int __maybe_unused be_xgbe_phy_soft_reset(struct xgbe_prv_data *pdata)
{
	int count, ret;
	DBGPR("%s\n", __FUNCTION__);

	ret = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);

	ret |= MDIO_CTRL1_RESET;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, ret);

	count = DELAY_COUNT;
	do {
		msleep(20);
		ret = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
		if (ret < 0)
			return ret;
	} while ((ret & MDIO_CTRL1_RESET) && --count);

	if (ret & MDIO_CTRL1_RESET)
		return -ETIMEDOUT;

	return 0;
}

static int be_xgbe_phy_config_aneg(struct xgbe_prv_data *pdata)
{
	int reg;

	DBGPR("%s\n", __FUNCTION__);

	pdata->link_check = jiffies;
	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_CTRL1);
	
	/* Disable auto negotiation in any case! */
	reg &= ~MDIO_AN_CTRL1_ENABLE;
	pdata->phy.autoneg = AUTONEG_DISABLE;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_CTRL1, reg);

	return 0;
}

static int ext_phy_probe(struct device *pdev, struct phy_device **phy_dev)
{
        struct device_node *xmit_node;
        struct phy_device *phydev;
        struct device *dev = pdev;
        int ret;

        /* Retrieve the xmit-handle */
        xmit_node = of_parse_phandle(dev->of_node, "ext-phy-handle", 0);
        if (!xmit_node)
                return -ENODEV;

        phydev = of_phy_find_device(xmit_node);
        if (!phydev)
                return -ENODEV;

        ret = phy_init_hw(phydev);
        if (ret < 0)
                return ret;

        if ((phydev->speed != SPEED_10000) && (phydev->duplex != DUPLEX_FULL))
        	return -ENODEV;

        *phy_dev = phydev;

        return 0;
}

int be_xgbe_phy_config_init(struct xgbe_prv_data *pdata)
{
	int ret = 0;
    	int count = DELAY_COUNT;
	DBGPR("%s\n", __FUNCTION__);

        if(ext_phy_probe(&pdata->platdev->dev, &pdata->phydev)) {
                pr_info("XGMAC: can't probe external PHY\n");
                return 1;
        }

        pr_info("XGMAC: probe external PHY with success\n");

	/* Initialize supported features */
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
			pdata->phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
			pdata->phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
			pdata->phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Backplane_BIT,
			pdata->phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			pdata->phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
			pdata->phydev->supported, 1);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Pause_BIT,
			pdata->phydev->supported, 1);
	linkmode_copy(pdata->phydev->advertising, pdata->phydev->supported);

	pdata->phy.pause_autoneg = 0;
	pdata->phy.tx_pause = 0;
	pdata->phy.rx_pause = 0;
	
        /* Switch XGMAC PHY PLL to use external ref clock from pad */
	ret = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, VR_XS_PMA_MII_Gen5_MPLL_CTRL);
	ret &= ~(VR_XS_PMA_MII_Gen5_MPLL_CTRL_REF_CLK_SEL_bit);
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, VR_XS_PMA_MII_Gen5_MPLL_CTRL, ret);
	wmb();

	/* Make vendor specific soft reset */
	ret = XMDIO_READ(pdata, MDIO_MMD_PCS, VR_XS_PCS_DIG_CTRL1);
	ret |= VR_XS_PCS_DIG_CTRL1_VR_RST_Bit;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, VR_XS_PCS_DIG_CTRL1, ret);
	wmb();

	/* Wait reset finish */
	count = DELAY_COUNT;
	do {
		usleep_range(500, 600);
		ret = XMDIO_READ(pdata, MDIO_MMD_PCS, VR_XS_PCS_DIG_CTRL1);
	} while(((ret & VR_XS_PCS_DIG_CTRL1_VR_RST_Bit) != 0) && count--);


	DBGPR("%s %x\n", __FUNCTION__, ret);
	/*
	 * Wait for the RST (bit 15) of the “SR XS or PCS MMD Control1” Register is 0.
	 * This bit is self-cleared when Bits[4:2] in VR XS or PCS MMD Digital
	 * Status Register are equal to 3’b100, that is, Tx/Rx clocks are stable
	 * and in Power_Good state.
	 */
	count = DELAY_COUNT;
	do {
		usleep_range(500, 600);
		ret = XMDIO_READ(pdata, MDIO_MMD_PCS, SR_XC_or_PCS_MMD_Control1);
	} while(((ret & SR_XC_or_PCS_MMD_Control1_RST_Bit) != 0) && count--);

	/*
	 * This bit is self-cleared when Bits[4:2] in VR XS or PCS MMD Digital
	 * Status Register are equal to 3’b100, that is, Tx/Rx clocks are stable
	 * and in Power_Good state.
	 */
	count = DELAY_COUNT;
	do {
		usleep_range(500, 600);
		ret = XMDIO_READ(pdata, MDIO_MMD_PCS, DWC_GLBL_PLL_MONITOR);
	} while(((ret & SDS_PCS_CLOCK_READY_mask) != SDS_PCS_CLOCK_READY_bit) && count-- );

	/* Turn off and clear interrupts */
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INTMASK, 0);
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INT, 0);
	wmb();

	be_xgbe_phy_config_aneg(pdata);

	ret = be_xgbe_phy_xgmii_mode(pdata);
    
	count = DELAY_COUNT;
	do
	{
		msleep(10);
		ret = XMDIO_READ(pdata, MDIO_MMD_PCS, 0x0001);
	} while(((ret & 0x0004) != 0x0004) && count--);

	return 0;
}

static int be_xgbe_phy_aneg_done(struct xgbe_prv_data *pdata)
{
	int reg;
	DBGPR("%s\n", __FUNCTION__);

	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_STAT1);

	return (reg & MDIO_AN_STAT1_COMPLETE) ? 1 : 0;
}

static int be_xgbe_phy_update_link(struct xgbe_prv_data *pdata)
{
	int new_state = 0;
	int ret = 0;
	struct phy_device *phydev;

	if(!pdata || !pdata->phydev)
	    return 1;

	phydev = pdata->phydev;
	ret = phy_read_mmd(phydev, MDIO_MMD_PHYXS, 0x1001);

	if (pdata->phy.link) {
		/* Flow control support */
		pdata->pause_autoneg = pdata->phy.pause_autoneg;

		if (pdata->tx_pause != pdata->phy.tx_pause) {
			new_state = 1;
			pdata->hw_if.config_tx_flow_control(pdata);
			pdata->tx_pause = pdata->phy.tx_pause;
		}

		if (pdata->rx_pause != pdata->phy.rx_pause) {
			new_state = 1;
			pdata->hw_if.config_rx_flow_control(pdata);
			pdata->rx_pause = pdata->phy.rx_pause;
		}

		/* Speed support */
		if (pdata->phy_speed != pdata->phy.speed) {
			new_state = 1;
			pdata->phy_speed = pdata->phy.speed;
		}

		if (pdata->phy_link != pdata->phy.link) {
			new_state = 1;
			pdata->phy_link = pdata->phy.link;
		}
	} else if (pdata->phy_link) {
		new_state = 1;
		pdata->phy_link = 0;
		pdata->phy_speed = SPEED_UNKNOWN;
	}

	return 0;
}

static void be_xgbe_phy_read_status(struct xgbe_prv_data *pdata)
{
	int reg, link_aneg;
	
	if (!pdata->phydev)
		return;

	pdata->phy.link = 1;

	if (test_bit(XGBE_LINK_ERR, &pdata->dev_state)) {
		netif_carrier_off(pdata->netdev);

		pdata->phy.link = 0;
		goto update_link;
	}

	link_aneg = (pdata->phy.autoneg == AUTONEG_ENABLE);

	if (pdata->phydev) {
		pdata->phydev->drv->read_status(pdata->phydev);
		/* Pop out old values */
		pdata->phydev->drv->read_status(pdata->phydev);
		if (!pdata->phydev->link){
			pdata->phydev->link = 0;
			pdata->phy.link &= pdata->phydev->link;
		}
	}
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1);
	pdata->phy.link &= (reg & MDIO_STAT1_LSTATUS) ? 1 : 0;

	reg = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_STAT1);
	pdata->phy.link &= (reg & MDIO_STAT1_LSTATUS) ? 1 : 0;

	if (pdata->phy.link) {
		if (link_aneg && !be_xgbe_phy_aneg_done(pdata)) {
			return;
		}

		if (test_bit(XGBE_LINK_INIT, &pdata->dev_state))
			clear_bit(XGBE_LINK_INIT, &pdata->dev_state);

		netif_carrier_on(pdata->netdev);
	} else {
		if (test_bit(XGBE_LINK_INIT, &pdata->dev_state)) 
			if (link_aneg)
				return;

		netif_carrier_off(pdata->netdev);

		/* If KX4 mode is enabled training doesn't affect behavior */
		be_xgbe_an_restart_kr_training(pdata);
		/* Pop out old values */
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1);
		XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_STAT1);
	}

update_link:
	be_xgbe_phy_update_link(pdata);
}

static void be_xgbe_phy_stop(struct xgbe_prv_data *pdata)
{
	netif_dbg(pdata, link, pdata->netdev, "stopping PHY\n");

	/* Disable auto-negotiation interrupts */
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INTMASK, 0);

	pdata->phy.link = 0;
	netif_carrier_off(pdata->netdev);

	be_xgbe_phy_update_link(pdata);
}

/**
 * be_xgbe_phy_start() - dummy
 */
int be_xgbe_phy_start(struct xgbe_prv_data *pdata)
{
        return 0;
}

/**
 * be_xgbe_phy_exit() - dummy
 */
void be_xgbe_phy_exit(struct xgbe_prv_data *pdata)
{
	return;
}

/**
 * be_an_isr() - dummy
 */
irqreturn_t be_an_isr(struct xgbe_prv_data *pdata)
{
	return IRQ_HANDLED;
}

void xgbe_init_function_ptrs_phy_baikal(struct xgbe_phy_if *phy_if)
{
	phy_if->phy_init        = be_xgbe_phy_config_init;
	phy_if->phy_exit	= be_xgbe_phy_exit;
	phy_if->phy_reset       = be_xgbe_phy_soft_reset;
	phy_if->phy_stop        = be_xgbe_phy_stop;
	phy_if->phy_status      = be_xgbe_phy_read_status;
	phy_if->phy_config_aneg = be_xgbe_phy_config_aneg;
	phy_if->phy_start	= be_xgbe_phy_start;
	phy_if->an_isr		= be_an_isr;
}
