/*
 * Copyright (C) Baikal Electronics 2019
 *
 * Author: Ekaterina Skachko (Ekaterina.Skachko@baikalelectronics.ru)
 *         for Baikal Electronics.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>

#include <sound/soc.h>
#include <sound/initval.h>

#include "local.h"

static struct snd_soc_dai_link snd_be_dai = {
	.name = "dw_i2s",
	.stream_name = "dw_i2s",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM,
};

static struct snd_soc_card snd_be_card = {
	.name		= "snd_be_card",
	.owner		= THIS_MODULE,
	.dai_link	= &snd_be_dai,
	.num_links	= 1,
};

static int snd_soc_be_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec_np, *be_np;
	struct snd_soc_dai_link_component *comp;

	snd_be_card.dev = &pdev->dev;

	be_np = of_parse_phandle(np, "baikal,cpu-dai", 0);
	codec_np  = of_parse_phandle(np, "baikal,audio-codec", 0);

	if (!(be_np && codec_np)) {
		dev_err(&pdev->dev, "Phandle missing or invalid\n");
		return -EINVAL;
	}
	/* for cpus/codecs/platforms */
	comp = devm_kzalloc(&pdev->dev, 3 * sizeof(*comp), GFP_KERNEL);
	if (!comp)
		return -ENOMEM;

	snd_be_dai.cpus = &comp[0];
	snd_be_dai.num_cpus = 1;
	snd_be_dai.codecs = &comp[1];
	snd_be_dai.num_codecs = 1;
	snd_be_dai.platforms = &comp[2];
	snd_be_dai.num_platforms = 1;

	snd_be_dai.codecs->of_node = codec_np;
	snd_be_dai.cpus->of_node = be_np;
	snd_be_dai.platforms->of_node = be_np;

	snd_soc_of_parse_card_name(&snd_be_card, "baikal,card-name");


	dev_dbg(&pdev->dev, "%s: Card %s: Set platform drvdata.\n",
		__func__, snd_be_card.name);
	platform_set_drvdata(pdev, &snd_be_card);

	snd_soc_card_set_drvdata(&snd_be_card, NULL);

	dev_dbg(&pdev->dev, "%s: Card %s: num_links = %d\n",
		__func__, snd_be_card.name, snd_be_card.num_links);
	dev_dbg(&pdev->dev, "%s: Card %s: DAI-link 0: name = %s\n",
		__func__, snd_be_card.name, snd_be_card.dai_link[0].name);
	dev_dbg(&pdev->dev, "%s: Card %s: DAI-link 0: stream_name = %s\n",
		__func__, snd_be_card.name,
		snd_be_card.dai_link[0].stream_name);

	ret = snd_soc_register_card(&snd_be_card);
	if (ret)
		dev_err(&pdev->dev,
			"Error: snd_soc_register_card failed (%d)!\n", ret);


	return ret;
}

static int snd_soc_be_remove(struct platform_device *pdev)
{
	struct snd_soc_card *snd_be_card = platform_get_drvdata(pdev);

	pr_debug("%s: Enter.\n", __func__);

	snd_soc_unregister_card(snd_be_card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id snd_soc_be_of_match[] = {
	{ .compatible = "baikal,snd_soc_be",	 },
	{},
};

MODULE_DEVICE_TABLE(of, snd_soc_be_of_match);
#endif


static struct platform_driver snd_soc_be_driver = {
	.probe		= snd_soc_be_probe,
	.remove		= snd_soc_be_remove,
	.driver		= {
		.name	= "snd_soc_be",
		.of_match_table = of_match_ptr(snd_soc_be_of_match),
	},
};

module_platform_driver(snd_soc_be_driver);

MODULE_AUTHOR("Ekaterina Skachko <Ekaterina.Skachko@baikalelectronics.ru>");
MODULE_DESCRIPTION("Baikal-M SoC Sound Driver");
MODULE_LICENSE("GPL");
