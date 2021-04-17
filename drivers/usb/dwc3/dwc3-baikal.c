/**
 * dwc3-baikal.c - Baikal Electronics SoCs Specific Glue layer
 *
 * Copyright (C) 2015 Baikal Electronics JSC - http://www.baikalelectronics.ru
 *
 * Author: Dmitry Dunaev <dmitry.dunaev@baikalelectronics.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/io.h>
#include <linux/of_platform.h>

struct dwc3_baikal {
	struct device	*dev;
	struct clk	*clk;
};

static int be_dwc3_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*node = pdev->dev.of_node;
	struct dwc3_baikal	*dwc;
	int			ret;

	dwc = devm_kzalloc(dev, sizeof(*dwc), GFP_KERNEL);
	if (!dwc)
		return -ENOMEM;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "DMA mask error %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, dwc);
	dwc->dev = dev;

	dwc->clk = devm_clk_get(dwc->dev, "usb");
	if (IS_ERR(dwc->clk)) {
		dev_err(dev, "no interface clk specified\n");
		return -EINVAL;
	}

	ret = clk_prepare_enable(dwc->clk);
	if (ret < 0) {
		dev_err(dwc->dev, "unable to enable usb clock\n");
		return ret;
	}

	if (node) {
			ret = of_platform_populate(node, NULL, NULL, dev);
			if (ret) {
				dev_err(&pdev->dev, "failed to create dwc3 core\n");
				goto __error;
			}
	} else {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		ret = -ENODEV;
		goto __error;
	}

	return 0;

__error:
	clk_disable_unprepare(dwc->clk);

	return ret;
}

static int be_dwc3_remove_core(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int be_dwc3_remove(struct platform_device *pdev)
{
	struct dwc3_baikal *dwc = platform_get_drvdata(pdev);

	device_for_each_child(&pdev->dev, NULL, be_dwc3_remove_core);
	clk_disable_unprepare(dwc->clk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id be_dwc3_of_match[] = {
	{ .compatible = "be,baikal-dwc3", },
	{},
};
MODULE_DEVICE_TABLE(of, be_dwc3_of_match);

static struct platform_driver be_dwc3_driver = {
	.probe		= be_dwc3_probe,
	.remove		= be_dwc3_remove,
	.driver		= {
		.name	= "baikal-dwc3",
		.of_match_table	= be_dwc3_of_match,
	},
};

module_platform_driver(be_dwc3_driver);

MODULE_ALIAS("platform:baikal-dwc3");
MODULE_AUTHOR("Dmitry Dunaev <dmitry.dunaev@baikalelectronics.ru>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 Baikal SoCs Glue Layer");
