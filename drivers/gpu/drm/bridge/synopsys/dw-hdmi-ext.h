/*
 * Additional header file used with
 * DesignWare High-Definition Multimedia Interface (HDMI) driver
 * for Baikal Electronics BE-M1000 SoC
 *
 * This file contains constants which are missing in 4.9.x dw-hdmi.h
 * and dw-hdmi-audio.h files. These constants are needed for 
 * Baikal Electronics' version of dw-hdmi driver as it is essentially
 * a backported 4.13.x driver with some patches.
 *
 * Copyright (C) 2019 Baikal Electronics JSC
 *
 * Author: Pavel Parkhomenko <Pavel.Parkhomenko@baikalelectronics.ru>
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DW_HDMI_EXT_H__
#define __DW_HDMI_EXT_H__

enum {
/* PRODUCT_ID0 field values */
	HDMI_PRODUCT_ID0_HDMI_TX = 0xa0,

/* PRODUCT_ID1 field values */
	HDMI_PRODUCT_ID1_HDCP = 0xc0,
	HDMI_PRODUCT_ID1_HDMI_RX = 0x02,
	HDMI_PRODUCT_ID1_HDMI_TX = 0x01,

/* CONFIG0_ID field values */
	HDMI_CONFIG0_I2S = 0x10,

/* CONFIG3_ID field values */
	HDMI_CONFIG3_AHBAUDDMA = 0x02,
	HDMI_CONFIG3_GPAUD = 0x01,

/* IH_I2CM_STAT0 and IH_MUTE_I2CM_STAT0 field values */
	HDMI_IH_I2CM_STAT0_DONE = 0x2,
	HDMI_IH_I2CM_STAT0_ERROR = 0x1,

/* FC_DATAUTO0 field values */
	HDMI_FC_DATAUTO0_VSD_MASK = 0x08,
	HDMI_FC_DATAUTO0_VSD_OFFSET = 3,

/* AUD_CONF0 field values */
	HDMI_AUD_CONF0_SW_RESET = 0x80,
	HDMI_AUD_CONF0_I2S_ALL_ENABLE = 0x2F,

/* AUD_CONF1 field values */
	HDMI_AUD_CONF1_MODE_I2S = 0x00,
	HDMI_AUD_CONF1_MODE_RIGHT_J = 0x02,
	HDMI_AUD_CONF1_MODE_LEFT_J = 0x04,
	HDMI_AUD_CONF1_WIDTH_16 = 0x10,
	HDMI_AUD_CONF1_WIDTH_24 = 0x18,

/* HDMI_AUD_INPUTCLKFS field values */
	HDMI_AUD_INPUTCLKFS_128FS = 0,
	HDMI_AUD_INPUTCLKFS_256FS = 1,
	HDMI_AUD_INPUTCLKFS_512FS = 2,
	HDMI_AUD_INPUTCLKFS_64FS = 4,


/* I2CM_OPERATION field values */
	HDMI_I2CM_OPERATION_WRITE = 0x10,
	HDMI_I2CM_OPERATION_READ_EXT = 0x2,
	HDMI_I2CM_OPERATION_READ = 0x1,

/* I2CM_INT field values */
	HDMI_I2CM_INT_DONE_POL = 0x8,
	HDMI_I2CM_INT_DONE_MASK = 0x4,

/* I2CM_CTLINT field values */
	HDMI_I2CM_CTLINT_NAC_POL = 0x80,
	HDMI_I2CM_CTLINT_NAC_MASK = 0x40,
	HDMI_I2CM_CTLINT_ARB_POL = 0x8,
	HDMI_I2CM_CTLINT_ARB_MASK = 0x4,
};

/*
 * HDMI 3D TX PHY registers
 */
#define HDMI_3D_TX_PHY_PWRCTRL			0x00
#define HDMI_3D_TX_PHY_SERDIVCTRL		0x01
#define HDMI_3D_TX_PHY_SERCKCTRL		0x02
#define HDMI_3D_TX_PHY_SERCKKILLCTRL		0x03
#define HDMI_3D_TX_PHY_TXRESCTRL		0x04
#define HDMI_3D_TX_PHY_CKCALCTRL		0x05
#define HDMI_3D_TX_PHY_CPCE_CTRL		0x06
#define HDMI_3D_TX_PHY_TXCLKMEASCTRL		0x07
#define HDMI_3D_TX_PHY_TXMEASCTRL		0x08
#define HDMI_3D_TX_PHY_CKSYMTXCTRL		0x09
#define HDMI_3D_TX_PHY_CMPSEQCTRL		0x0a
#define HDMI_3D_TX_PHY_CMPPWRCTRL		0x0b
#define HDMI_3D_TX_PHY_CMPMODECTRL		0x0c
#define HDMI_3D_TX_PHY_MEASCTRL			0x0d
#define HDMI_3D_TX_PHY_VLEVCTRL			0x0e
#define HDMI_3D_TX_PHY_D2ACTRL			0x0f
#define HDMI_3D_TX_PHY_CURRCTRL			0x10
#define HDMI_3D_TX_PHY_DRVANACTRL		0x11
#define HDMI_3D_TX_PHY_PLLMEASCTRL		0x12
#define HDMI_3D_TX_PHY_PLLPHBYCTRL		0x13
#define HDMI_3D_TX_PHY_GRP_CTRL			0x14
#define HDMI_3D_TX_PHY_GMPCTRL			0x15
#define HDMI_3D_TX_PHY_MPLLMEASCTRL		0x16
#define HDMI_3D_TX_PHY_MSM_CTRL			0x17
#define HDMI_3D_TX_PHY_SCRPB_STATUS		0x18
#define HDMI_3D_TX_PHY_TXTERM			0x19
#define HDMI_3D_TX_PHY_PTRPT_ENBL		0x1a
#define HDMI_3D_TX_PHY_PATTERNGEN		0x1b
#define HDMI_3D_TX_PHY_SDCAP_MODE		0x1c
#define HDMI_3D_TX_PHY_SCOPEMODE		0x1d
#define HDMI_3D_TX_PHY_DIGTXMODE		0x1e
#define HDMI_3D_TX_PHY_STR_STATUS		0x1f
#define HDMI_3D_TX_PHY_SCOPECNT0		0x20
#define HDMI_3D_TX_PHY_SCOPECNT1		0x21
#define HDMI_3D_TX_PHY_SCOPECNT2		0x22
#define HDMI_3D_TX_PHY_SCOPECNTCLK		0x23
#define HDMI_3D_TX_PHY_SCOPESAMPLE		0x24
#define HDMI_3D_TX_PHY_SCOPECNTMSB01		0x25
#define HDMI_3D_TX_PHY_SCOPECNTMSB2CK		0x26

/* HDMI_3D_TX_PHY_CKCALCTRL values */
#define HDMI_3D_TX_PHY_CKCALCTRL_OVERRIDE		BIT(15)

/* HDMI_3D_TX_PHY_MSM_CTRL values */
#define HDMI_3D_TX_PHY_MSM_CTRL_MPLL_PH_SEL_CK		BIT(13)
#define HDMI_3D_TX_PHY_MSM_CTRL_CKO_SEL_CLK_REF_MPLL	(0 << 1)
#define HDMI_3D_TX_PHY_MSM_CTRL_CKO_SEL_OFF		(1 << 1)
#define HDMI_3D_TX_PHY_MSM_CTRL_CKO_SEL_PCLK		(2 << 1)
#define HDMI_3D_TX_PHY_MSM_CTRL_CKO_SEL_FB_CLK		(3 << 1)
#define HDMI_3D_TX_PHY_MSM_CTRL_SCOPE_CK_SEL		BIT(0)

/* HDMI_3D_TX_PHY_PTRPT_ENBL values */
#define HDMI_3D_TX_PHY_PTRPT_ENBL_OVERRIDE		BIT(15)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_PG_SKIP_BIT2		BIT(8)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_PG_SKIP_BIT1		BIT(7)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_PG_SKIP_BIT0		BIT(6)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_CK_REF_ENB		BIT(5)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_RCAL_ENB		BIT(4)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_TX_CLK_ALIGN_ENB	BIT(3)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_TX_READY		BIT(2)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_CKO_WORD_ENB		BIT(1)
#define HDMI_3D_TX_PHY_PTRPT_ENBL_REFCLK_ENB		BIT(0)

struct dw_hdmi_i2s_audio_data {
	struct dw_hdmi *hdmi;

	void (*write)(struct dw_hdmi *hdmi, u8 val, int offset);
	u8 (*read)(struct dw_hdmi *hdmi, int offset);
};

#endif /* __DW_HDMI_EXT_H__ */
