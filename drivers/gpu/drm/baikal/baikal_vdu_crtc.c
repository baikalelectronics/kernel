/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019-2021 Baikal Electronics JSC
 *
 * Author: Pavel Parkhomenko <Pavel.Parkhomenko@baikalelectronics.ru>
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 * (C) COPYRIGHT 2012-2013 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms of
 * such GNU licence.
 *
 */

/**
 * baikal_vdu_crtc.c
 * Implementation of the CRTC functions for Baikal Electronics BE-M1000 VDU driver
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/version.h>
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_vblank.h>

#include "baikal_vdu_drm.h"
#include "baikal_vdu_regs.h"

struct baikal_vdu_crtc_mode_fixup {
	int vdisplay;
	int vfp_add;
};

static const struct baikal_vdu_crtc_mode_fixup mode_fixups[] = {
	{ 480, 38 },
	{ 600, 8 },
	{ 720, 43 },
	{ 768, 43 },
	{ 800, 71 },
	{ 864, 71 },
	{ 900, 71 },
	{ 960, 71 },
	{ 1024, 25 },
	{ 1050, 25 },
	{ 1080, 8 },
	{ 1200, 32 },
	{ 1440, 27 },
	{ ~0U },
};

irqreturn_t baikal_vdu_irq(int irq, void *data)
{
	struct drm_device *drm = data;
	struct baikal_vdu_private *priv = drm->dev_private;
	irqreturn_t status = IRQ_NONE;
	u32 raw_stat;
	u32 irq_stat;

	irq_stat = readl(priv->regs + IVR);
	raw_stat = readl(priv->regs + ISR);


	if (irq_stat & INTR_VCT) {
		priv->counters[10]++;
		drm_crtc_handle_vblank(&priv->crtc);
		status = IRQ_HANDLED;
	}

	if (irq_stat & INTR_FER) {
		priv->counters[11]++;
		priv->counters[12] = readl(priv->regs + DBAR);
		priv->counters[13] = readl(priv->regs + DCAR);
		priv->counters[14] = readl(priv->regs + MRR);
		status = IRQ_HANDLED;
	}

	priv->counters[3] |= raw_stat;

	/* Clear all interrupts */
	writel(irq_stat, priv->regs + ISR);

	return status;
}

bool baikal_vdu_crtc_mode_fixup(struct drm_crtc *crtc,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	struct baikal_vdu_private *priv = crtc->dev->dev_private;

	memcpy(adjusted_mode, mode, sizeof(*mode));

	if (!priv->mode_fixup)
		return true;

	if (priv->mode_fixup == -1) {
		const struct baikal_vdu_crtc_mode_fixup *fixups = mode_fixups;
		for (; fixups && fixups->vdisplay != ~0U; ++fixups) {
			if (mode->vdisplay <= fixups->vdisplay)
				break;
		}
		if (fixups->vdisplay == ~0U)
			return true;
		else
			priv->mode_fixup = fixups->vfp_add;
	}

	adjusted_mode->vtotal += priv->mode_fixup;
	adjusted_mode->vsync_start += priv->mode_fixup;
	adjusted_mode->vsync_end += priv->mode_fixup;
	adjusted_mode->clock = mode->clock * adjusted_mode->vtotal / mode->vtotal;

	return true;
}

static void baikal_vdu_crtc_helper_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct baikal_vdu_private *priv = dev->dev_private;
	const struct drm_display_mode *orig_mode = &crtc->state->mode;
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	unsigned long rate;
	unsigned int ppl, hsw, hfp, hbp;
	unsigned int lpp, vsw, vfp, vbp;
	unsigned int reg;
	int ret = 0;

	drm_mode_debug_printmodeline(orig_mode);
	drm_mode_debug_printmodeline(mode);

	rate = mode->clock * 1000;

	if (rate != clk_get_rate(priv->clk))
	{
		DRM_DEV_DEBUG_DRIVER(dev->dev, "Requested pixel clock is %lu Hz\n", rate);

		/* hold clock domain reset; disable clocking */
		writel(0, priv->regs + PCTR);

		if (__clk_is_enabled(priv->clk))
			clk_disable_unprepare(priv->clk);
		ret = clk_set_rate(priv->clk, rate);

		if (ret >= 0) {
			clk_prepare_enable(priv->clk);
			if (!__clk_is_enabled(priv->clk))
				ret = -1;
		}

		/* release clock domain reset; enable clocking */
		reg = readl(priv->regs + PCTR);
		reg |= PCTR_PCR + PCTR_PCI;
		writel(reg, priv->regs + PCTR);
	}

	if (ret < 0)
		DRM_ERROR("Cannot set desired pixel clock (%lu Hz)\n", rate);

	ppl = mode->hdisplay / 16;
	if (priv->panel) {
		hsw = mode->hsync_end - mode->hsync_start;
		hfp = mode->hsync_start - mode->hdisplay - 1;
	} else {
		hsw = mode->hsync_end - mode->hsync_start - 1;
		hfp = mode->hsync_start - mode->hdisplay;
	}
	hbp = mode->htotal - mode->hsync_end;

	lpp = mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;

	writel((HTR_HFP(hfp) & HTR_HFP_MASK) |
			(HTR_PPL(ppl) & HTR_PPL_MASK) |
			(HTR_HBP(hbp) & HTR_HBP_MASK) |
			(HTR_HSW(hsw) & HTR_HSW_MASK),
			priv->regs + HTR);

	if (mode->hdisplay > 4080 || ppl * 16 != mode->hdisplay)
		writel((HPPLOR_HPPLO(mode->hdisplay) & HPPLOR_HPPLO_MASK) | HPPLOR_HPOE,
				priv->regs + HPPLOR);

	writel((VTR1_VSW(vsw) & VTR1_VSW_MASK) |
			(VTR1_VFP(vfp) & VTR1_VFP_MASK) |
			(VTR1_VBP(vbp) & VTR1_VBP_MASK),
			priv->regs + VTR1);

	writel(lpp & VTR2_LPP_MASK, priv->regs + VTR2);

	writel((HVTER_VSWE(vsw >> VTR1_VSW_LSB_WIDTH) & HVTER_VSWE_MASK) |
			(HVTER_HSWE(hsw >> HTR_HSW_LSB_WIDTH) & HVTER_HSWE_MASK) |
			(HVTER_VBPE(vbp >> VTR1_VBP_LSB_WIDTH) & HVTER_VBPE_MASK) |
			(HVTER_VFPE(vfp >> VTR1_VFP_LSB_WIDTH) & HVTER_VFPE_MASK) |
			(HVTER_HBPE(hbp >> HTR_HBP_LSB_WIDTH) & HVTER_HBPE_MASK) |
			(HVTER_HFPE(hfp >> HTR_HFP_LSB_WIDTH) & HVTER_HFPE_MASK),
			priv->regs + HVTER);

	/* Set polarities */
	reg = readl(priv->regs + CR1);
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg |= CR1_HSP;
	else
		reg &= ~CR1_HSP;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg |= CR1_VSP;
	else
		reg &= ~CR1_VSP;
	reg |= CR1_DEP; // set DE to active high;
	writel(reg, priv->regs + CR1);

	crtc->hwmode = crtc->state->adjusted_mode;
}

static void baikal_vdu_crtc_helper_enable(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_state)
{
	struct baikal_vdu_private *priv = crtc->dev->dev_private;
	struct drm_panel *panel = priv->panel;
	struct device_node *panel_node;
	const char *data_mapping;
	u32 cntl, gpio;

	DRM_DEV_DEBUG_DRIVER(crtc->dev->dev, "enabling pixel clock\n");
	clk_prepare_enable(priv->clk);

	drm_panel_prepare(panel);

	writel(ISCR_VSC_VFP, priv->regs + ISCR);

	/* Set 16-word input FIFO watermark */
	/* Enable and Power Up */
	cntl = readl(priv->regs + CR1);
	cntl &= ~CR1_FDW_MASK;
	cntl |= CR1_LCE + CR1_FDW_16_WORDS;

	if (priv->type == VDU_TYPE_LVDS) {
		panel_node = panel->dev->of_node;
		if (of_property_read_string(panel_node, "data-mapping", &data_mapping)) {
			cntl |= CR1_OPS_LCD18;
		} else if (strncmp(data_mapping, "vesa-24", 7))
			cntl |= CR1_OPS_LCD24;
		else if (strncmp(data_mapping, "jeida-18", 8))
			cntl |= CR1_OPS_LCD18;
		else {
			dev_warn(crtc->dev->dev, "%s data mapping is not supported, vesa-24 is set\n", data_mapping);
			cntl |= CR1_OPS_LCD24;
		}
		gpio = GPIOR_UHD_ENB;
		if (priv->ep_count == 4)
			gpio |= GPIOR_UHD_QUAD_PORT;
		else if (priv->ep_count == 2)
			gpio |= GPIOR_UHD_DUAL_PORT;
		else
			gpio |= GPIOR_UHD_SNGL_PORT;
		writel(gpio, priv->regs + GPIOR);
	} else
		cntl |= CR1_OPS_LCD24;
	writel(cntl, priv->regs + CR1);

	drm_panel_enable(priv->panel);
	drm_crtc_vblank_on(crtc);
}

void baikal_vdu_crtc_helper_disable(struct drm_crtc *crtc)
{
	struct baikal_vdu_private *priv = crtc->dev->dev_private;

	drm_panel_disable(priv->panel);

	drm_panel_unprepare(priv->panel);
	drm_crtc_vblank_off(crtc);

	/* Disable clock */
	DRM_DEV_DEBUG_DRIVER(crtc->dev->dev, "disabling pixel clock\n");
	clk_disable_unprepare(priv->clk);
}

static void baikal_vdu_crtc_helper_atomic_flush(struct drm_crtc *crtc,
					   struct drm_crtc_state *old_state)
{
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (crtc->state->active && drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

int baikal_vdu_enable_vblank(struct drm_crtc *crtc)
{
	struct baikal_vdu_private *priv = crtc->dev->dev_private;

	/* clear interrupt status */
	writel(0x3ffff, priv->regs + ISR);

	writel(INTR_VCT + INTR_FER, priv->regs + IMR);

	return 0;
}

void baikal_vdu_disable_vblank(struct drm_crtc *crtc)
{
	struct baikal_vdu_private *priv = crtc->dev->dev_private;

	/* clear interrupt status */
	writel(0x3ffff, priv->regs + ISR);

	writel(INTR_FER, priv->regs + IMR);
}

const struct drm_crtc_funcs crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = baikal_vdu_enable_vblank,
	.disable_vblank = baikal_vdu_disable_vblank,
};

const struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.mode_fixup = baikal_vdu_crtc_mode_fixup,
	.mode_set_nofb = baikal_vdu_crtc_helper_mode_set_nofb,
	.atomic_flush = baikal_vdu_crtc_helper_atomic_flush,
	.disable = baikal_vdu_crtc_helper_disable,
	.atomic_enable = baikal_vdu_crtc_helper_enable,
};

int baikal_vdu_crtc_create(struct drm_device *dev)
{
	struct baikal_vdu_private *priv = dev->dev_private;
	struct drm_crtc *crtc = &priv->crtc;

	drm_crtc_init_with_planes(dev, crtc,
				  &priv->primary, NULL,
				  &crtc_funcs, "primary");
	drm_crtc_helper_add(crtc, &crtc_helper_funcs);

	/* XXX: The runtime clock disabling still results in
	 * occasional system hangs, and needs debugging.
	 */

	DRM_DEV_DEBUG_DRIVER(crtc->dev->dev, "enabling pixel clock\n");
	clk_prepare_enable(priv->clk);

	return 0;
}
