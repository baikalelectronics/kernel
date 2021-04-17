/*
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/property.h>
#include <linux/io.h>

#include "spi-db-espi.h"


static void espi_set_cs       (struct spi_device *spi, bool enable);
static int  espi_setup        (struct spi_device *spi);

static void espi_writer       (struct espi_data *priv);
static void espi_reader       (struct espi_data *priv);
static void espi_reset        (struct espi_data *priv);
static void espi_enable_tx    (struct espi_data *priv);
static void espi_enable_rx    (struct espi_data *priv);


/* ---------------- */
/* LOW-LEVEL        */
/* ---------------- */
static void espi_reset (struct espi_data *priv)
{
	/* NORESET */
	espi_cr1_t cr1;
	cr1.bits.scr = ESPI_CR1_SCR_NORESET;
	espi_writel (priv, ESPI_CR1, cr1.val);

	/* RESET */
	cr1.bits.scr = ESPI_CR1_SCR_RESET;
	espi_writel (priv, ESPI_CR1, cr1.val);
}

static void espi_enable_rx (struct espi_data *priv)
{
	espi_cr2_t cr2;
	cr2.val = espi_readl(priv, ESPI_CR2);
	cr2.bits.srd = ESPI_CR2_SRD_RX_ENABLE;
	espi_writel(priv, ESPI_CR2, cr2.val);
}

static void espi_enable_tx (struct espi_data *priv)
{
	espi_cr2_t cr2;
	int cnt = espi_readl(priv, ESPI_TX_FBCAR);
	cr2.val = espi_readl(priv, ESPI_CR2);

	if (cnt && cr2.bits.mte != ESPI_CR2_MTE_TX_ENABLE){
		cr2.bits.mte = ESPI_CR2_MTE_TX_ENABLE;
		espi_writel(priv, ESPI_CR2, cr2.val);
	}
}

static void espi_set_cs (struct spi_device *spi, bool enable)
{
	struct espi_data *priv;
	espi_cr2_t cr2;
	int gpio;

	priv = spi_master_get_devdata(spi->master);

	/* cs-host */
	cr2.val = espi_readl (priv, ESPI_CR2);
	cr2.bits.sso = spi->chip_select;
	espi_writel (priv, ESPI_CR2, cr2.val);

	/* cs-gpio */
	if (spi->chip_select < priv->cnt_gpios) {
		gpio = priv->cs_gpios[spi->chip_select];
		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, !enable);
	}
}


static void espi_writer (struct espi_data *priv)
{
	volatile uint8_t data = ESPI_DUMMY_DATA;
	uint32_t free = ESPI_FIFO_LEN - espi_readl(priv, ESPI_TX_FBCAR);
	uint32_t cnt = min(free,priv->txlen);

	priv->txlen -= cnt;

	/* fifo */
	while (cnt--) {
		if (priv->tx)
			data = *priv->tx++;
		espi_writel(priv, ESPI_TX_FIFO, data);
	}

	/* mask irq */
	if (priv->txlen == 0) {
		espi_irq_t mask;
		mask.val = espi_readl(priv, ESPI_IMR);
		mask.bits.tx_almost_empty = ESPI_IRQ_DISABLE;
		espi_writel(priv, ESPI_IMR, mask.val);
	}

	/* start */
	espi_enable_tx (priv);
}


static void espi_reader (struct espi_data *priv)
{
	volatile uint8_t data;
	uint32_t have = espi_readl(priv, ESPI_RX_FBCAR);
	uint32_t cnt = min(have,priv->rxlen);

	priv->rxlen -= cnt;

	/* fifo */
	while (cnt--) {
		data = espi_readl(priv, ESPI_RX_FIFO);
		if (priv->rx)
			*priv->rx++ = data;
	}
}


static irqreturn_t espi_irq_handler (int irq, void *dev_id)
{
	espi_irq_t status;
	struct spi_master *master = dev_id;
	struct espi_data  *priv   = spi_master_get_devdata(master);
	int ret = -1;

	/* get */
	status.val = espi_readl(priv, ESPI_IVR);
	espi_writel(priv, ESPI_ISR, status.val);

	/* check */
	if (!status.val) {
		dev_err(&master->dev, "irq_none\n");
		return IRQ_NONE;
	}
	if (status.bits.tx_underflow) {
		dev_err(&master->dev, "tx_underflow\n");
		goto exit;
	}
	if (status.bits.tx_overrun) {
		dev_err(&master->dev, "tx_overrun\n");
		goto exit;
	}
	if (status.bits.rx_underrun) {
		dev_err(&master->dev, "rx_underrun\n");
		goto exit;
	}
	if (status.bits.rx_overrun) {
		dev_err(&master->dev, "rx_overrun\n");
		goto exit;
	}
	/*
	if (status.bits.tx_done_master && (priv->txlen || priv->rxlen)) {
		dev_err(&master->dev, "tx_done_master && len != 0\n");
		goto exit;
	}
	*/


	/* transfer */
	espi_reader(priv);
	espi_writer(priv);
	ret = 0;

exit:
	/* reset */
	if (ret)
		espi_reset(priv);

	/* finalize */
	if (ret || (priv->txlen == 0 && priv->rxlen == 0)) {
		espi_writel(priv, ESPI_IMR, ESPI_IMR_DISABLE);
		espi_writel(priv, ESPI_ISR, ESPI_ISR_CLEAR);

		if (master->cur_msg) {
			if(ret)
				master->cur_msg->status = -EIO;
			spi_finalize_current_transfer(master);
		}
	}
	return IRQ_HANDLED;
}


static int espi_transfer_one(
		struct spi_master   *master,
		struct spi_device   *spi,
		struct spi_transfer *transfer)
{
	struct espi_data *priv = spi_master_get_devdata(spi->master);

	/* transfer */
	priv->tx = (void*) transfer->tx_buf;
	priv->rx = (void*) transfer->rx_buf;
	priv->txlen = transfer->len;
	priv->rxlen = transfer->len;

	/* clear */
	espi_writel(priv, ESPI_IMR, ESPI_IMR_DISABLE);
	espi_writel(priv, ESPI_ISR, ESPI_ISR_CLEAR);

	/* threshold */
	espi_writel(priv, ESPI_TX_FAETR, ESPI_FIFO_LEN/2);
	espi_writel(priv, ESPI_RX_FAFTR, ESPI_FIFO_LEN/8);

	/* clk */
	if (transfer->speed_hz && (transfer->speed_hz != spi->max_speed_hz))
		clk_set_rate(priv->clk, transfer->speed_hz);

	espi_enable_rx(priv);  /* enable rx */
	espi_writer(priv);     /* prepare data */
	espi_writel(priv, ESPI_IMR, ESPI_IMR_ENABLE);  /* mask */

	return 0;
}


static int espi_setup (struct spi_device *spi)
{
	struct espi_data *priv = spi_master_get_devdata(spi->master);
	espi_cr1_t cr1;
	espi_cr2_t cr2;

	/* reset */
	espi_reset(priv);

	/* control */
	cr1.val = espi_readl(priv, ESPI_CR1);
	cr1.bits.scr = ESPI_CR1_SCR_NORESET;
	cr1.bits.sce = ESPI_CR1_SCE_CORE_ENABLE;
	cr1.bits.mss = ESPI_CR1_MSS_MODE_MASTER;
	cr1.bits.cph = (spi->mode|SPI_CPHA)? ESPI_CR1_CPH_CLKPHASE_ODD : ESPI_CR1_CPH_CLKPHASE_EVEN;
	cr1.bits.cpo = (spi->mode|SPI_CPOL)? ESPI_CR1_CPO_CLKPOLAR_LOW : ESPI_CR1_CPO_CLKPOLAR_HIGH;
	espi_writel(priv, ESPI_CR1, cr1.val);

	cr2.val = espi_readl(priv, ESPI_CR2);
	cr2.bits.srd = ESPI_CR2_SRD_RX_ENABLE;
	cr2.bits.mte = ESPI_CR2_MTE_TX_DISABLE;
	cr2.bits.sri = ESPI_CR2_SRI_FIRST_RESIEV;
	cr2.bits.mlb = (spi->mode|SPI_LSB_FIRST)? ESPI_CR2_MLB_LSB : ESPI_CR2_MLB_MSB;
	espi_writel(priv, ESPI_CR2, cr2.val);

	return 0;
}


/* ---------------- */
/* DRIVER           */
/* ---------------- */
static int espi_probe (struct platform_device *pdev)
{
	struct device *dev = &(pdev->dev);
	struct espi_data *priv;
	struct resource *mem;
	int i;
	int err;
	struct spi_master *master;

	/* ------ */
	/* PRIV   */
	/* ------ */

	/* init */
	priv = devm_kzalloc(dev, sizeof(struct espi_data), GFP_KERNEL);
	if (!priv){
		dev_err(dev, "no memory\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, priv);

	/* reg */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem){
		return -EINVAL;
	}

	priv->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "region map failed\n");
		return PTR_ERR(priv->regs);
	}

	/* irq */
	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(&pdev->dev, "no irq resource\n");
		return priv->irq;
	}

	/* clk */
	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "could not get spi clock\n");
		return PTR_ERR(priv->clk);
	}

	/* gpio */
	priv->cnt_gpios = of_gpio_named_count(dev->of_node, "cs-gpios");
	priv->cs_gpios = devm_kzalloc (dev, sizeof(int) * priv->cnt_gpios, GFP_KERNEL);

	for (i = 0; i < priv->cnt_gpios; i++)
		priv->cs_gpios[i] = -ENOENT;

	for (i = 0; i < priv->cnt_gpios; i++) {
		int gpio = of_get_named_gpio(dev->of_node, "cs-gpios", i);
		if (gpio_is_valid(gpio) && !devm_gpio_request(dev, gpio, dev_name(dev))){
			priv->cs_gpios[i] = gpio;
			gpio_direction_output (gpio, 1);
		}
	}

	/* ------ */
	/* MASTER */
	/* ------ */

	/* alloc */
	master = spi_alloc_master(dev, 0);
	if (!master){
		return -ENOMEM;
	}
	priv->master = master;

	/* cs-num */
	master->num_chipselect = 8;
	device_property_read_u16(dev, "num-cs", &master->num_chipselect);
	master->num_chipselect = max_t(int, master->num_chipselect, priv->cnt_gpios);

	/* clk */
	err = clk_prepare_enable(priv->clk);
	if (err){
		return err;
	}
	if (device_property_read_u32(dev, "spi-max-frequency", &master->max_speed_hz)){
		dev_err(dev, "no valid 'spi-max-frequency' property\n");
		master->max_speed_hz = clk_get_rate(priv->clk);
	}

	/* misk */
	master->mode_bits = SPI_CPHA | SPI_CPOL;
	master->dev.of_node = dev->of_node;

	/* operation */
	master->transfer_one = espi_transfer_one;
	master->set_cs = espi_set_cs;
	master->setup = espi_setup;
	master->bus_num = of_alias_get_id(master->dev.of_node, "espi");

	/* irq */
	err = devm_request_irq(dev, priv->irq, espi_irq_handler, IRQF_SHARED, pdev->name, master);
	if (err) {
		dev_err(&pdev->dev, "unable to request irq %d\n", priv->irq);
		return err;
	}

	/* data */
	spi_master_set_devdata(master, priv);


	/* register */
	err = devm_spi_register_master(dev, master);
	if (err) {
		dev_err(&master->dev, "problem registering spi master\n");
		goto err_free_master;
	}
	return 0;

err_free_master:
	spi_master_put(master);
	return err;
}


static int espi_remove (struct platform_device *pdev)
{
	struct espi_data *priv = platform_get_drvdata(pdev);
	espi_reset(priv);
	clk_disable_unprepare(priv->clk);
	return 0;
}


/* ---------------- */
/* PLATFORM         */
/* ---------------- */
static const struct of_device_id espi_table[] = {
	{ .compatible = "be,espi", },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, espi_table);

static struct platform_driver espi_driver = {
	.probe		= espi_probe,
	.remove		= espi_remove,
	.driver		= {
		.name	= "be,espi",
		.of_match_table = espi_table,
	},
};
module_platform_driver(espi_driver);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("");
MODULE_LICENSE("GPL v2");



