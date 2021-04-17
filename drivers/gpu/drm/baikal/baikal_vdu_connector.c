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
 */

/**
 * baikal_vdu_connector.c
 * Implementation of the connector functions for Baikal Electronics
 * BE-M1000 SoC's VDU
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include "baikal_vdu_drm.h"

static void baikal_vdu_connector_destroy(struct drm_connector *connector)
{
	struct baikal_vdu_private *priv =
		to_baikal_vdu_private(connector);

	if (priv->panel)
		drm_panel_detach(priv->panel);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status baikal_vdu_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct baikal_vdu_private *priv =
		to_baikal_vdu_private(connector);

	return (priv->panel ?
		connector_status_connected :
		connector_status_disconnected);
}

static int baikal_vdu_connector_helper_get_modes(
		struct drm_connector *connector)
{
	struct baikal_vdu_private *priv =
		to_baikal_vdu_private(connector);

	if (!priv->panel)
		return 0;

	return drm_panel_get_modes(priv->panel);
}

int baikal_vdu_lvds_set_property(struct drm_connector *connector,
				       struct drm_property *property,
				       uint64_t value)
{
	struct drm_encoder *encoder = connector->encoder;

	if (!encoder)
		return -1;

	if (!strcmp(property->name, "scaling mode")) {
		uint64_t curval;
		switch (value) {
		case DRM_MODE_SCALE_FULLSCREEN:
			break;
		case DRM_MODE_SCALE_NO_SCALE:
			break;
		case DRM_MODE_SCALE_ASPECT:
			break;
		default:
			goto set_prop_error;
		}

		if (drm_object_property_get_value(&connector->base,
						     property,
						     &curval))
			goto set_prop_error;

		if (curval == value)
			goto set_prop_done;

		if (drm_object_property_set_value(&connector->base,
							property,
							value))
			goto set_prop_error;

	} else if (!strcmp(property->name, "backlight")) {
		if (drm_object_property_set_value(&connector->base,
							property,
							value))
			goto set_prop_error;
		else {
			// TBD set backlight
		}
	} else if (!strcmp(property->name, "DPMS")) {
		const struct drm_encoder_helper_funcs *hfuncs
						= encoder->helper_private;
		hfuncs->dpms(encoder, value);
	}

set_prop_done:
	return 0;
set_prop_error:
	return -1;
}

static void baikal_vdu_lvds_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	// TBD
}

const struct drm_connector_funcs connector_funcs = {
	//.dpms = drm_atomic_helper_connector_dpms, // TODO sort this out
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = baikal_vdu_connector_detect,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.set_property = baikal_vdu_lvds_set_property,
	.destroy = baikal_vdu_connector_destroy,
};

const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = baikal_vdu_connector_helper_get_modes,
};

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_encoder_helper_funcs baikal_vdu_lvds_helper_funcs = {
	.dpms = baikal_vdu_lvds_encoder_dpms,
	//.prepare = TBD,
	//.mode_set = TBD,
	//.commit = TBD,
};

int baikal_vdu_lvds_connector_create(struct drm_device *dev)
{
	struct baikal_vdu_private *priv = dev->dev_private;
	struct drm_connector *connector = &priv->connector;
	struct drm_encoder *encoder = &priv->encoder;

	drm_connector_init(dev, connector, &connector_funcs,
			DRM_MODE_CONNECTOR_LVDS);
	drm_encoder_init(dev, encoder, &encoder_funcs,
			DRM_MODE_ENCODER_LVDS, NULL);
	encoder->crtc = &priv->crtc;
	encoder->possible_crtcs = BIT(drm_crtc_index(encoder->crtc));

	drm_connector_attach_encoder(connector, encoder);

	drm_encoder_helper_add(encoder, &baikal_vdu_lvds_helper_funcs);
	drm_connector_helper_add(connector, &connector_helper_funcs);

	drm_connector_register(connector);

	drm_panel_attach(priv->panel, connector);

	return 0;
}
