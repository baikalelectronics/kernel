// SPDX-License-Identifier: GPL-2.0
/*
 * SMBus adapter driver for Baikal SoC
 *
 * Copyright (C) 2019-2021 Baikal Electronics, JSC
 * Authors: Georgy Vlasov <georgy.vlasov@baikalelectronics.ru>
 *	    Mikhail Ivanov <michail.ivanov@baikalelectronics.ru>
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/* Register definitions */
#define BE_SMBUS_CR1		0x00
#define BE_CR1_IRT		BIT(0)
#define BE_CR1_TRS		BIT(1)
#define BE_CR1_IEB		BIT(3)

#define BE_SMBUS_CR2		0x04
#define BE_CR2_FTE		BIT(1)

#define BE_SMBUS_FBCR1		0x08
#define BE_SMBUS_FIFO		0x0c
#define BE_SMBUS_SCD1		0x10

#define BE_SMBUS_SCD2		0x14
#define BE_SCD2_MASK		0x03
#define BE_SCD2_SHT		BIT(7)

#define BE_SMBUS_ADR1		0x18

#define BE_SMBUS_ISR1		0x20
#define BE_ISR1_MASK		0x7f
#define BE_ISR1_FER		BIT(2)
#define BE_ISR1_RNK		BIT(3)
#define BE_ISR1_TCS		BIT(6)

#define BE_SMBUS_IMR1		0x24
#define BE_SMBUS_FBCR2		0x2c
#define BE_SMBUS_IMR2		0x48

#define BE_SMBUS_FIFO_SIZE	16

struct be_smbus_dev {
	struct device		*dev;
	void __iomem		*base;
	struct clk		*smbus_clk;
	struct i2c_adapter	adapter;
	u32			bus_clk_rate;
};

static int be_smbus_init(struct be_smbus_dev *besmb)
{
	u32 divider = clk_get_rate(besmb->smbus_clk) / besmb->bus_clk_rate - 1;

	writel(BE_CR1_IRT, besmb->base + BE_SMBUS_CR1);
	writel(0, besmb->base + BE_SMBUS_CR1);
	writel(0, besmb->base + BE_SMBUS_CR2);
	writel(0, besmb->base + BE_SMBUS_FBCR2);
	writel(0, besmb->base + BE_SMBUS_IMR1);
	writel(0, besmb->base + BE_SMBUS_IMR2);
	writel(divider & 0xff, besmb->base + BE_SMBUS_SCD1);
	writel(BE_SCD2_SHT | ((divider >> 8) & BE_SCD2_MASK),
		besmb->base + BE_SMBUS_SCD2);

	return 0;
}

static int be_smbus_xfer(struct i2c_adapter *adap, u16 addr,
				unsigned short flags, char read_write,
				u8 command, int size, union i2c_smbus_data *data)
{
	struct be_smbus_dev *const besmb = adap->dev.driver_data;
	int ret = -EINVAL;

	switch (size) {
	case I2C_SMBUS_BYTE:
		writel(BE_CR1_IRT, besmb->base + BE_SMBUS_CR1);
		writel(0, besmb->base + BE_SMBUS_CR1);
		writel(addr, besmb->base + BE_SMBUS_ADR1);
		writel(1, besmb->base + BE_SMBUS_FBCR1);

		if (read_write == I2C_SMBUS_WRITE) {
			writel(BE_CR1_TRS | BE_CR1_IEB,
				besmb->base + BE_SMBUS_CR1);
			writel(command, besmb->base + BE_SMBUS_FIFO);
		} else {
			writel(BE_CR1_IEB, besmb->base + BE_SMBUS_CR1);
		}

		writel(BE_ISR1_MASK, besmb->base + BE_SMBUS_ISR1);
		writel(BE_CR2_FTE, besmb->base + BE_SMBUS_CR2);

		while ((readl(besmb->base + BE_SMBUS_CR2) & BE_CR2_FTE) &&
			!(readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_TCS));

		if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_RNK) {
			ret = -ENXIO;
			goto exit;
		}

		if (read_write == I2C_SMBUS_READ) {
			data->byte = readl(besmb->base + BE_SMBUS_FIFO);

			if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_FER) {
				ret = -EMSGSIZE;
				goto exit;
			}
		}

		ret = 0;
		break;
	case I2C_SMBUS_BYTE_DATA:
		writel(BE_CR1_IRT, besmb->base + BE_SMBUS_CR1);
		writel(0, besmb->base + BE_SMBUS_CR1);
		writel(addr, besmb->base + BE_SMBUS_ADR1);
		writel(BE_CR1_TRS | BE_CR1_IEB, besmb->base + BE_SMBUS_CR1);

		if (read_write == I2C_SMBUS_WRITE) {
			writel(2, besmb->base + BE_SMBUS_FBCR1);
			writel(command, besmb->base + BE_SMBUS_FIFO);
			writel(data->byte, besmb->base + BE_SMBUS_FIFO);
		} else {
			writel(1, besmb->base + BE_SMBUS_FBCR1);
			writel(command, besmb->base + BE_SMBUS_FIFO);
		}

		writel(BE_ISR1_MASK, besmb->base + BE_SMBUS_ISR1);
		writel(BE_CR2_FTE, besmb->base + BE_SMBUS_CR2);

		while ((readl(besmb->base + BE_SMBUS_CR2) & BE_CR2_FTE) &&
			!(readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_TCS));

		if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_RNK) {
			ret = -ENXIO;
			goto exit;
		}

		if (read_write == I2C_SMBUS_READ) {
			writel(BE_CR1_IEB, besmb->base + BE_SMBUS_CR1);
			writel(1, besmb->base + BE_SMBUS_FBCR1);
			writel(BE_ISR1_MASK, besmb->base + BE_SMBUS_ISR1);
			writel(BE_CR2_FTE, besmb->base + BE_SMBUS_CR2);

			while ((readl(besmb->base + BE_SMBUS_CR2) & BE_CR2_FTE) &&
				!(readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_TCS));

			if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_RNK) {
				ret = -ENXIO;
				goto exit;
			}

			data->byte = readl(besmb->base + BE_SMBUS_FIFO);
			if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_FER) {
				ret = -EMSGSIZE;
				goto exit;
			}
		}

		ret = 0;
		break;
	case I2C_SMBUS_WORD_DATA:
		writel(BE_CR1_IRT, besmb->base + BE_SMBUS_CR1);
		writel(0, besmb->base + BE_SMBUS_CR1);
		writel(addr, besmb->base + BE_SMBUS_ADR1);
		writel(BE_CR1_TRS | BE_CR1_IEB, besmb->base + BE_SMBUS_CR1);

		if (read_write == I2C_SMBUS_WRITE) {
			writel(3, besmb->base + BE_SMBUS_FBCR1);
			writel(command, besmb->base + BE_SMBUS_FIFO);
			writel((data->word >> 0) & 0xff,
				besmb->base + BE_SMBUS_FIFO);
			writel((data->word >> 8) & 0xff,
				besmb->base + BE_SMBUS_FIFO);
		} else {
			writel(1, besmb->base + BE_SMBUS_FBCR1);
			writel(command, besmb->base + BE_SMBUS_FIFO);
		}

		writel(BE_ISR1_MASK, besmb->base + BE_SMBUS_ISR1);
		writel(BE_CR2_FTE, besmb->base + BE_SMBUS_CR2);

		while ((readl(besmb->base + BE_SMBUS_CR2) & BE_CR2_FTE) &&
			!(readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_TCS));

		if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_RNK) {
			ret = -ENXIO;
			goto exit;
		}

		if (read_write == I2C_SMBUS_READ) {
			writel(BE_CR1_IEB, besmb->base + BE_SMBUS_CR1);
			writel(2, besmb->base + BE_SMBUS_FBCR1);
			writel(BE_ISR1_MASK, besmb->base + BE_SMBUS_ISR1);
			writel(BE_CR2_FTE, besmb->base + BE_SMBUS_CR2);

			while ((readl(besmb->base + BE_SMBUS_CR2) & BE_CR2_FTE) &&
				!(readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_TCS));

			if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_RNK) {
				ret = -ENXIO;
				goto exit;
			}

			data->word  = readl(besmb->base + BE_SMBUS_FIFO);
			data->word |= readl(besmb->base + BE_SMBUS_FIFO) << 8;
			if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_FER) {
				ret = -EMSGSIZE;
				goto exit;
			}
		}

		ret = 0;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		writel(BE_CR1_IRT, besmb->base + BE_SMBUS_CR1);
		writel(0, besmb->base + BE_SMBUS_CR1);
		writel(addr, besmb->base + BE_SMBUS_ADR1);
		writel(BE_CR1_TRS | BE_CR1_IEB, besmb->base + BE_SMBUS_CR1);

		if (read_write == I2C_SMBUS_WRITE) {
			unsigned txedsize = 0;

			while (txedsize < data->block[0] + 1) {
				unsigned xblocknum;
				unsigned xfersize;

				xfersize = data->block[0] + 1 - txedsize;
				if (xfersize > BE_SMBUS_FIFO_SIZE) {
					xfersize = BE_SMBUS_FIFO_SIZE;
				}

				writel(xfersize, besmb->base + BE_SMBUS_FBCR1);
				if (!txedsize) {
					/*
					 * The first xfer should start with
					 * command byte
					 */
					writel(command,
						besmb->base + BE_SMBUS_FIFO);
					xblocknum = 1;
					++txedsize;
				} else {
					xblocknum = 0;
				}

				while (xblocknum < xfersize) {
					writel(data->block[txedsize++],
						besmb->base + BE_SMBUS_FIFO);
					++xblocknum;
				}

				writel(BE_ISR1_MASK, besmb->base + BE_SMBUS_ISR1);
				writel(BE_CR2_FTE, besmb->base + BE_SMBUS_CR2);

				while ((readl(besmb->base + BE_SMBUS_CR2) & BE_CR2_FTE) &&
					!(readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_TCS));

				if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_RNK) {
					ret = -ENXIO;
					goto exit;
				}
			}
		} else {
			unsigned rxedsize = 0;

			writel(1, besmb->base + BE_SMBUS_FBCR1);
			writel(command, besmb->base + BE_SMBUS_FIFO);
			writel(BE_ISR1_MASK, besmb->base + BE_SMBUS_ISR1);
			writel(BE_CR2_FTE, besmb->base + BE_SMBUS_CR2);

			while ((readl(besmb->base + BE_SMBUS_CR2) & BE_CR2_FTE) &&
				!(readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_TCS));

			if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_RNK) {
				ret = -ENXIO;
				goto exit;
			}

			writel(BE_CR1_IEB, besmb->base + BE_SMBUS_CR1);

			while (rxedsize < I2C_SMBUS_BLOCK_MAX) {
				unsigned i;

				writel(BE_SMBUS_FIFO_SIZE, besmb->base + BE_SMBUS_FBCR1);
				writel(BE_ISR1_MASK, besmb->base + BE_SMBUS_ISR1);
				writel(BE_CR2_FTE, besmb->base + BE_SMBUS_CR2);

				while ((readl(besmb->base + BE_SMBUS_CR2) & BE_CR2_FTE) &&
					!(readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_TCS));

				if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_RNK) {
					ret = -ENXIO;
					goto exit;
				}

				for (i = 0; i < BE_SMBUS_FIFO_SIZE; ++i) {
					data->block[1 + rxedsize++] =
						readl(besmb->base + BE_SMBUS_FIFO);

					if (readl(besmb->base + BE_SMBUS_ISR1) & BE_ISR1_FER) {
						ret = -EMSGSIZE;
						goto exit;
					}
				}
			}

			data->block[0] = rxedsize;
		}

		ret = 0;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

exit:
	writel(0, besmb->base + BE_SMBUS_CR1);
	return ret;
}

static u32 be_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm be_smbus_algorithm = {
	.smbus_xfer	= be_smbus_xfer,
	.functionality	= be_functionality,
};

static irqreturn_t be_smbus_isr(int irq, void *_dev)
{
	return IRQ_HANDLED;
}

static int be_smbus_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct be_smbus_dev *besmb;
	struct resource *res;
	void __iomem *base;
	int irq;
	int ret;

	besmb = devm_kzalloc(&pdev->dev, sizeof(*besmb), GFP_KERNEL);
	if (besmb == NULL) {
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		return PTR_ERR(base);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "missing interrupt resource\n");
		return irq;
	}

	besmb->smbus_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(besmb->smbus_clk)) {
		dev_err(&pdev->dev, "missing clock\n");
		return PTR_ERR(besmb->smbus_clk);
	}

	besmb->base = base;
	besmb->dev = &pdev->dev;

	of_property_read_u32(np, "clock-frequency", &besmb->bus_clk_rate);
	if (!besmb->bus_clk_rate) {
		besmb->bus_clk_rate = 100000; /* default clock rate */
	}

	ret = devm_request_irq(&pdev->dev, irq, be_smbus_isr, 0, pdev->name, besmb);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim IRQ%d\n", irq);
		return ret;
	}

	ret = clk_prepare_enable(besmb->smbus_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		return ret;
	}

	i2c_set_adapdata(&besmb->adapter, besmb);
	strlcpy(besmb->adapter.name, pdev->name, sizeof(besmb->adapter.name));
	besmb->adapter.owner = THIS_MODULE;
	besmb->adapter.algo = &be_smbus_algorithm;
	besmb->adapter.dev.parent = &pdev->dev;
	besmb->adapter.dev.of_node = pdev->dev.of_node;

	platform_set_drvdata(pdev, besmb);

	ret = be_smbus_init(besmb);
	if (ret) {
		dev_err(&pdev->dev, "failed to init SMBus\n");
		return ret;
	}

	ret = i2c_add_adapter(&besmb->adapter);
	if (ret) {
		clk_disable_unprepare(besmb->smbus_clk);
		return ret;
	}

	return 0;
}

static int be_smbus_remove(struct platform_device *pdev)
{
	struct be_smbus_dev *besmb = platform_get_drvdata(pdev);

	clk_disable_unprepare(besmb->smbus_clk);
	i2c_del_adapter(&besmb->adapter);

	return 0;
}

static const struct of_device_id be_smbus_match[] = {
	{ .compatible = "be,smbus" },
	{}
};
MODULE_DEVICE_TABLE(of, be_smbus_match);

static struct platform_driver be_smbus_driver = {
	.driver = {
		.name = "baikal-smbus",
		.of_match_table = be_smbus_match
	},
	.probe  = be_smbus_probe,
	.remove = be_smbus_remove
};

module_platform_driver(be_smbus_driver);

MODULE_AUTHOR("Georgy Vlasov <georgy.vlasov@baikalelectronics.ru>");
MODULE_DESCRIPTION("Baikal-M SMBus adapter driver");
MODULE_LICENSE("GPL v2");
