// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Baikal Electronics SoC DWMAC glue layer
 *
 * Copyright (C) 2015,2016,2021 Baikal Electronics JSC
 * Authors: Dmitry Dunaev <dmitry.dunaev@baikalelectronics.ru>
 *          Alexey Sheplyakov <asheplyakov@altlinux.org>
 */

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "stmmac.h"
#include "stmmac_platform.h"
#include "common.h"
#include "dwmac_dma.h"
#include "dwmac1000_dma.h"

#define MAC_GPIO	0x00e0		/* GPIO register */
#define MAC_GPIO_GPO	(1 << 8)	/* Output port */

struct baikal_dwmac {
	struct device	*dev;
	struct clk	*tx2_clk;
};

static int baikal_dwmac_dma_reset(void __iomem *ioaddr)
{
	int err;
	u32 value;

	/* DMA SW reset */
	value = readl(ioaddr + DMA_BUS_MODE);
	value |= DMA_BUS_MODE_SFT_RESET;
	writel(value, ioaddr + DMA_BUS_MODE);

	udelay(100);

	/* Clear PHY reset */
	value = readl(ioaddr + MAC_GPIO);
	value |= MAC_GPIO_GPO;
	writel(value, ioaddr + MAC_GPIO);
	pr_info("PHY re-inited for Baikal DWMAC\n");

	err = readl_poll_timeout(ioaddr + DMA_BUS_MODE, value,
				 !(value & DMA_BUS_MODE_SFT_RESET),
				 10000, 1000000);
	if (err) {
		return -EBUSY;
	}

	return 0;
}

static const struct stmmac_dma_ops baikal_dwmac_dma_ops = {
	.reset = baikal_dwmac_dma_reset,
	.init = dwmac1000_dma_init,
	.init_rx_chan = dwmac1000_dma_init_rx,
	.init_tx_chan = dwmac1000_dma_init_tx,
	.axi = dwmac1000_dma_axi,
	.dump_regs = dwmac1000_dump_dma_regs,
	.dma_rx_mode = dwmac1000_dma_operation_mode_rx,
	.dma_tx_mode = dwmac1000_dma_operation_mode_tx,
	.enable_dma_transmission = dwmac_enable_dma_transmission,
	.enable_dma_irq = dwmac_enable_dma_irq,
	.disable_dma_irq = dwmac_disable_dma_irq,
	.start_tx = dwmac_dma_start_tx,
	.stop_tx = dwmac_dma_stop_tx,
	.start_rx = dwmac_dma_start_rx,
	.stop_rx = dwmac_dma_stop_rx,
	.dma_interrupt = dwmac_dma_interrupt,
	.get_hw_feature = dwmac1000_get_hw_feature,
	.rx_watchdog = dwmac1000_rx_watchdog
};

static struct mac_device_info *baikal_dwmac_setup(void *ppriv)
{
	struct mac_device_info *mac, *old_mac;
	struct stmmac_priv *priv = ppriv;
	int ret;
	u32 value;

	mac = devm_kzalloc(priv->device, sizeof(*mac), GFP_KERNEL);
	if (!mac) {
		return NULL;
	}

	/* Clear PHY reset */
	value = readl(priv->ioaddr + MAC_GPIO);
	value |= MAC_GPIO_GPO;
	writel(value, priv->ioaddr + MAC_GPIO);

	mac->dma = &baikal_dwmac_dma_ops;
	old_mac = priv->hw;
	priv->hw = mac;
	ret = dwmac1000_setup(priv);
	priv->hw = old_mac;
	if (ret) {
		dev_err(priv->device, "dwmac1000_setup: error %d", ret);
		return NULL;
	}

	return mac;
}

static void baikal_dwmac_fix_mac_speed(void *priv, unsigned int speed)
{
	struct baikal_dwmac *dwmac = priv;
	unsigned long tx2_clk_freq = 0;
	dev_info(dwmac->dev, "fix_mac_speed new speed %u\n", speed);
	switch (speed) {
	case SPEED_1000:
		tx2_clk_freq = 250000000;
		break;
	case SPEED_100:
		tx2_clk_freq = 50000000;
		break;
	case SPEED_10:
		tx2_clk_freq = 5000000;
		break;
	}

	if (dwmac->tx2_clk != NULL && tx2_clk_freq) {
		dev_info(dwmac->dev, "setting TX2 clock frequency to %lu\n", tx2_clk_freq);
		clk_set_rate(dwmac->tx2_clk, tx2_clk_freq);
	}
}

static int dwmac_baikal_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct baikal_dwmac *dwmac;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret) {
		return ret;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_warn(&pdev->dev, "no suitable DMA available\n");
		return ret;
	}

	if (pdev->dev.of_node) {
		plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
		if (IS_ERR(plat_dat)) {
			dev_err(&pdev->dev, "dt configuration failed\n");
			return PTR_ERR(plat_dat);
		}
	} else {
		plat_dat = dev_get_platdata(&pdev->dev);
		if (!plat_dat) {
			dev_err(&pdev->dev, "no platform data provided\n");
			return -EINVAL;
		}

		/* Set default value for multicast hash bins */
		plat_dat->multicast_filter_bins = HASH_TABLE_SIZE;

		/* Set default value for unicast filter entries */
		plat_dat->unicast_filter_entries = 1;
	}

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac) {
		ret = -ENOMEM;
		goto err_remove_config_dt;
	}

	dwmac->dev = &pdev->dev;
	dwmac->tx2_clk = devm_clk_get(dwmac->dev, "tx2_clk");
	if (IS_ERR(dwmac->tx2_clk)) {
		dev_warn(&pdev->dev, "couldn't get TX2 clock\n");
		dwmac->tx2_clk = NULL;
	}

	plat_dat->fix_mac_speed = baikal_dwmac_fix_mac_speed;
	plat_dat->bsp_priv = dwmac;
	plat_dat->has_gmac = 1;
	plat_dat->enh_desc = 1;
	plat_dat->tx_coe = 1;
	plat_dat->rx_coe = 1;
	/* TODO: set CSR correct clock in dts */
	plat_dat->clk_csr = 3;
	plat_dat->setup = baikal_dwmac_setup;

	dev_info(&pdev->dev, "Baikal Electronics DWMAC glue driver\n");

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret) {
		goto err_remove_config_dt;
	}

	return 0;

err_remove_config_dt:
	stmmac_remove_config_dt(pdev, plat_dat);

	return ret;
}

static const struct of_device_id dwmac_baikal_match[] = {
	{ .compatible = "be,dwmac" },
	{ .compatible = "be,dwmac-3.710" },
	{ }
};
MODULE_DEVICE_TABLE(of, dwmac_baikal_match);

static struct platform_driver dwmac_baikal_driver = {
	.probe	= dwmac_baikal_probe,
	.remove	= stmmac_pltfr_remove,
	.driver	= {
		.name = "baikal-dwmac",
		.pm = &stmmac_pltfr_pm_ops,
		.of_match_table = of_match_ptr(dwmac_baikal_match)
	}
};
module_platform_driver(dwmac_baikal_driver);

MODULE_DESCRIPTION("Baikal DWMAC glue driver");
MODULE_LICENSE("GPL v2");
