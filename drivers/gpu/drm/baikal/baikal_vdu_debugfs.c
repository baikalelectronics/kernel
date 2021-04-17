/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019-2021 Baikal Electronics JSC
 *
 * Author: Pavel Parkhomenko <Pavel.Parkhomenko@baikalelectronics.ru>
 *
 * Parts of this file were based on sources as follows:
 *
 *  Copyright © 2017 Broadcom
 */

#include <linux/seq_file.h>
#include <linux/device.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>

#include "baikal_vdu_drm.h"
#include "baikal_vdu_regs.h"

#define REGDEF(reg) { reg, #reg }
static const struct {
	u32 reg;
	const char *name;
} baikal_vdu_reg_defs[] = {
	REGDEF(CR1),
	REGDEF(HTR),
	REGDEF(VTR1),
	REGDEF(VTR2),
	REGDEF(PCTR),
	REGDEF(ISR),
	REGDEF(IMR),
	REGDEF(IVR),
	REGDEF(ISCR),
	REGDEF(DBAR),
	REGDEF(DCAR),
	REGDEF(DEAR),
	REGDEF(HVTER),
	REGDEF(HPPLOR),
	REGDEF(GPIOR),
	REGDEF(OWER),
	REGDEF(OWXSER0),
	REGDEF(OWYSER0),
	REGDEF(OWDBAR0),
	REGDEF(OWDCAR0),
	REGDEF(OWDEAR0),
	REGDEF(OWXSER1),
	REGDEF(OWYSER1),
	REGDEF(OWDBAR1),
	REGDEF(OWDCAR1),
	REGDEF(OWDEAR1),
	REGDEF(MRR),
};

int baikal_vdu_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct baikal_vdu_private *priv = dev->dev_private;
	int i;

	for (i = 0; i < ARRAY_SIZE(baikal_vdu_reg_defs); i++) {
		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   baikal_vdu_reg_defs[i].name, baikal_vdu_reg_defs[i].reg,
			   readl(priv->regs + baikal_vdu_reg_defs[i].reg));
	}

	for (i = 0; i < ARRAY_SIZE(priv->counters); i++) {
		seq_printf(m, "COUNTER[%d]: 0x%08x\n", i, priv->counters[i]);
	}

	return 0;
}

static ssize_t baikal_vdu_rg_read(struct file *file,
		char __user *user_buf, size_t count, loff_t *ppos)
{
	char *filename = file->f_path.dentry->d_iname;
	struct baikal_vdu_private *priv = file->f_inode->i_private;
	char buf[16];
	int len = 0;

	if (!strcmp("htr", filename))
		len = snprintf(buf, sizeof(buf), "%x\n", readl(priv->regs + HTR));
	else if (!strcmp("vtr", filename))
		len = snprintf(buf, sizeof(buf), "%x\n", readl(priv->regs + VTR1));
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t baikal_vdu_rg_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct baikal_vdu_private *priv = file->f_inode->i_private;
	char *filename = file->f_path.dentry->d_iname;
	u32 val;
	int ret;

	ret = kstrtouint_from_user(buf, count, 16, &val);
	if (ret)
		return ret;

	if (!strcmp("htr", filename))
		writel(val, priv->regs + HTR);
	else if (!strcmp("vtr", filename))
		writel(val, priv->regs + VTR1);

	return count;
}

static const struct file_operations baikal_vdu_rg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = baikal_vdu_rg_write,
	.read = baikal_vdu_rg_read,
};

static const struct drm_info_list baikal_vdu_debugfs_list[] = {
	{"regs", baikal_vdu_debugfs_regs, 0},
};

int baikal_vdu_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct baikal_vdu_private *priv = dev->dev_private;
	debugfs_create_file("htr", S_IWUSR, minor->debugfs_root, priv, &baikal_vdu_rg_fops);
	debugfs_create_file("vtr", S_IWUSR, minor->debugfs_root, priv, &baikal_vdu_rg_fops);
	return drm_debugfs_create_files(baikal_vdu_debugfs_list,
					ARRAY_SIZE(baikal_vdu_debugfs_list),
					minor->debugfs_root, minor);
}
