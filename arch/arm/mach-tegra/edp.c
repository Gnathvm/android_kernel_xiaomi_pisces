/*
 * arch/arm/mach-tegra/edp.c
 *
 * Copyright (c) 2011-2013, NVIDIA CORPORATION. All Rights Reserved.
 * Copyright (C) 2016 XiaoMi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/edp.h>
#include <mach/edp.h>

#include "fuse.h"
#include "dvfs.h"
#include "clock.h"
#include "cpu-tegra.h"

#define FREQ_STEP 12750000
#define OVERRIDE_DEFAULT 6000

static struct tegra_edp_limits *edp_limits;
static int edp_limits_size;
static unsigned int regulator_cur;
/* Value to subtract from regulator current limit */
static unsigned int edp_reg_override_mA = OVERRIDE_DEFAULT;

static const unsigned int *system_edp_limits;

static struct tegra_system_edp_entry *power_edp_limits;
static int power_edp_limits_size;

/*
 * Temperature step size cannot be less than 4C because of hysteresis
 * delta
 * Code assumes different temperatures for the same speedo_id /
 * regulator_cur are adjacent in the table, and higest regulator_cur
 * comes first
 */
static char __initdata tegra_edp_vdd_cpu_map[] = {
	0x00, 0x2f, 0x17, 0x7d, 0x73, 0x73, 0x73, 0x00,
	0x2f, 0x2d, 0x82, 0x78, 0x78, 0x78, 0x00, 0x2f,
	0x3c, 0x82, 0x78, 0x78, 0x78, 0x00, 0x2f, 0x4b,
	0x82, 0x78, 0x78, 0x78, 0x00, 0x2f, 0x55, 0x82,
	0x78, 0x78, 0x78, 0x00, 0x28, 0x17, 0x7d, 0x73,
	0x73, 0x73, 0x00, 0x28, 0x2d, 0x82, 0x78, 0x78,
	0x78, 0x00, 0x28, 0x3c, 0x82, 0x78, 0x78, 0x78,
	0x00, 0x28, 0x4b, 0x82, 0x78, 0x78, 0x73, 0x00,
	0x28, 0x55, 0x82, 0x78, 0x78, 0x69, 0x00, 0x23,
	0x17, 0x7d, 0x73, 0x73, 0x73, 0x00, 0x23, 0x2d,
	0x82, 0x78, 0x78, 0x78, 0x00, 0x23, 0x3c, 0x82,
	0x78, 0x78, 0x6e, 0x00, 0x23, 0x4b, 0x82, 0x78,
	0x78, 0x64, 0x00, 0x23, 0x55, 0x82, 0x78, 0x6e,
	0x5a, 0x00, 0x1e, 0x17, 0x7d, 0x73, 0x73, 0x64,
	0x00, 0x1e, 0x2d, 0x82, 0x78, 0x78, 0x69, 0x00,
	0x1e, 0x3c, 0x82, 0x78, 0x78, 0x64, 0x00, 0x1e,
	0x4b, 0x82, 0x78, 0x6e, 0x5a, 0x00, 0x1e, 0x55,
	0x82, 0x78, 0x64, 0x50, 0x00, 0x19, 0x17, 0x7d,
	0x73, 0x69, 0x55, 0x00, 0x19, 0x2d, 0x82, 0x78,
	0x6e, 0x5a, 0x00, 0x19, 0x3c, 0x82, 0x78, 0x69,
	0x55, 0x00, 0x19, 0x4b, 0x82, 0x78, 0x5f, 0x4b,
	0x00, 0x19, 0x55, 0x82, 0x73, 0x55, 0x3c, 0x01,
	0x2f, 0x17, 0x7d, 0x73, 0x73, 0x73, 0x01, 0x2f,
	0x2d, 0x82, 0x78, 0x78, 0x78, 0x01, 0x2f, 0x3c,
	0x82, 0x78, 0x78, 0x78, 0x01, 0x2f, 0x4b, 0x82,
	0x78, 0x78, 0x78, 0x01, 0x2f, 0x55, 0x82, 0x78,
	0x78, 0x78, 0x01, 0x28, 0x17, 0x7d, 0x73, 0x73,
	0x73, 0x01, 0x28, 0x2d, 0x82, 0x78, 0x78, 0x78,
	0x01, 0x28, 0x3c, 0x82, 0x78, 0x78, 0x78, 0x01,
	0x28, 0x4b, 0x82, 0x78, 0x78, 0x73, 0x01, 0x28,
	0x55, 0x82, 0x78, 0x78, 0x69, 0x01, 0x23, 0x17,
	0x7d, 0x73, 0x73, 0x73, 0x01, 0x23, 0x2d, 0x82,
	0x78, 0x78, 0x78, 0x01, 0x23, 0x3c, 0x82, 0x78,
	0x78, 0x6e, 0x01, 0x23, 0x4b, 0x82, 0x78, 0x78,
	0x64, 0x01, 0x23, 0x55, 0x82, 0x78, 0x6e, 0x5a,
	0x01, 0x1e, 0x17, 0x7d, 0x73, 0x73, 0x64, 0x01,
	0x1e, 0x2d, 0x82, 0x78, 0x78, 0x69, 0x01, 0x1e,
	0x3c, 0x82, 0x78, 0x78, 0x64, 0x01, 0x1e, 0x4b,
	0x82, 0x78, 0x6e, 0x5a, 0x01, 0x1e, 0x55, 0x82,
	0x78, 0x64, 0x50, 0x01, 0x19, 0x17, 0x7d, 0x73,
	0x69, 0x55, 0x01, 0x19, 0x2d, 0x82, 0x78, 0x6e,
	0x5a, 0x01, 0x19, 0x3c, 0x82, 0x78, 0x69, 0x55,
	0x01, 0x19, 0x4b, 0x82, 0x78, 0x5f, 0x4b, 0x01,
	0x19, 0x55, 0x82, 0x73, 0x55, 0x3c, 0x02, 0x3d,
	0x17, 0x87, 0x7d, 0x7d, 0x7d, 0x02, 0x3d, 0x2d,
	0x8c, 0x82, 0x82, 0x82, 0x02, 0x3d, 0x3c, 0x8c,
	0x82, 0x82, 0x82, 0x02, 0x3d, 0x4b, 0x8c, 0x82,
	0x82, 0x82, 0x02, 0x3d, 0x55, 0x8c, 0x82, 0x82,
	0x82, 0x02, 0x32, 0x17, 0x87, 0x7d, 0x7d, 0x7d,
	0x02, 0x32, 0x2d, 0x8c, 0x82, 0x82, 0x82, 0x02,
	0x32, 0x3c, 0x8c, 0x82, 0x82, 0x82, 0x02, 0x32,
	0x4b, 0x8c, 0x82, 0x82, 0x78, 0x02, 0x32, 0x55,
	0x8c, 0x82, 0x82, 0x6e, 0x02, 0x28, 0x17, 0x87,
	0x7d, 0x7d, 0x73, 0x02, 0x28, 0x2d, 0x8c, 0x82,
	0x82, 0x78, 0x02, 0x28, 0x3c, 0x8c, 0x82, 0x82,
	0x73, 0x02, 0x28, 0x4b, 0x8c, 0x82, 0x78, 0x69,
	0x02, 0x28, 0x55, 0x8c, 0x82, 0x6e, 0x5a, 0x02,
	0x23, 0x17, 0x87, 0x7d, 0x7d, 0x69, 0x02, 0x23,
	0x2d, 0x8c, 0x82, 0x82, 0x6e, 0x02, 0x23, 0x3c,
	0x8c, 0x82, 0x78, 0x69, 0x02, 0x23, 0x4b, 0x8c,
	0x82, 0x6e, 0x5a, 0x02, 0x23, 0x55, 0x8c, 0x82,
	0x64, 0x50, 0x03, 0x3d, 0x17, 0x87, 0x7d, 0x7d,
	0x7d, 0x03, 0x3d, 0x2d, 0x8c, 0x82, 0x82, 0x82,
	0x03, 0x3d, 0x3c, 0x8c, 0x82, 0x82, 0x82, 0x03,
	0x3d, 0x4b, 0x8c, 0x82, 0x82, 0x82, 0x03, 0x3d,
	0x55, 0x8c, 0x82, 0x82, 0x82, 0x03, 0x32, 0x17,
	0x87, 0x7d, 0x7d, 0x7d, 0x03, 0x32, 0x2d, 0x8c,
	0x82, 0x82, 0x82, 0x03, 0x32, 0x3c, 0x8c, 0x82,
	0x82, 0x82, 0x03, 0x32, 0x4b, 0x8c, 0x82, 0x82,
	0x78, 0x03, 0x32, 0x55, 0x8c, 0x82, 0x82, 0x6e,
	0x03, 0x28, 0x17, 0x87, 0x7d, 0x7d, 0x73, 0x03,
	0x28, 0x2d, 0x8c, 0x82, 0x82, 0x78, 0x03, 0x28,
	0x3c, 0x8c, 0x82, 0x82, 0x73, 0x03, 0x28, 0x4b,
	0x8c, 0x82, 0x78, 0x69, 0x03, 0x28, 0x55, 0x8c,
	0x82, 0x6e, 0x5a, 0x03, 0x23, 0x17, 0x87, 0x7d,
	0x7d, 0x69, 0x03, 0x23, 0x2d, 0x8c, 0x82, 0x82,
	0x6e, 0x03, 0x23, 0x3c, 0x8c, 0x82, 0x78, 0x69,
	0x03, 0x23, 0x4b, 0x8c, 0x82, 0x6e, 0x5a, 0x03,
	0x23, 0x55, 0x8c, 0x82, 0x64, 0x50, 0x04, 0x32,
	0x17, 0x91, 0x87, 0x87, 0x87, 0x04, 0x32, 0x2d,
	0x96, 0x8c, 0x8c, 0x8c, 0x04, 0x32, 0x3c, 0x96,
	0x8c, 0x8c, 0x8c, 0x04, 0x32, 0x46, 0x96, 0x8c,
	0x8c, 0x8c, 0x04, 0x32, 0x4b, 0x82, 0x78, 0x78,
	0x78, 0x04, 0x32, 0x55, 0x82, 0x78, 0x78, 0x78,
	0x04, 0x2f, 0x17, 0x91, 0x87, 0x87, 0x87, 0x04,
	0x2f, 0x2d, 0x96, 0x8c, 0x8c, 0x8c, 0x04, 0x2f,
	0x3c, 0x96, 0x8c, 0x8c, 0x8c, 0x04, 0x2f, 0x46,
	0x96, 0x8c, 0x8c, 0x82, 0x04, 0x2f, 0x4b, 0x82,
	0x78, 0x78, 0x78, 0x04, 0x2f, 0x55, 0x82, 0x78,
	0x78, 0x78, 0x04, 0x28, 0x17, 0x91, 0x87, 0x87,
	0x87, 0x04, 0x28, 0x2d, 0x96, 0x8c, 0x8c, 0x82,
	0x04, 0x28, 0x3c, 0x96, 0x8c, 0x8c, 0x82, 0x04,
	0x28, 0x46, 0x96, 0x8c, 0x8c, 0x78, 0x04, 0x28,
	0x4b, 0x82, 0x78, 0x78, 0x78, 0x04, 0x28, 0x55,
	0x82, 0x78, 0x78, 0x6e, 0x04, 0x23, 0x17, 0x91,
	0x87, 0x87, 0x73, 0x04, 0x23, 0x2d, 0x96, 0x8c,
	0x8c, 0x78, 0x04, 0x23, 0x3c, 0x96, 0x8c, 0x82,
	0x78, 0x04, 0x23, 0x46, 0x96, 0x8c, 0x82, 0x6e,
	0x04, 0x23, 0x4b, 0x82, 0x78, 0x78, 0x6e, 0x04,
	0x23, 0x55, 0x82, 0x78, 0x78, 0x64, 0x04, 0x1e,
	0x17, 0x91, 0x87, 0x7d, 0x69, 0x04, 0x1e, 0x2d,
	0x96, 0x8c, 0x82, 0x6e, 0x04, 0x1e, 0x3c, 0x96,
	0x8c, 0x78, 0x64, 0x04, 0x1e, 0x46, 0x96, 0x8c,
	0x78, 0x5a, 0x04, 0x1e, 0x4b, 0x82, 0x78, 0x78,
	0x5a, 0x04, 0x1e, 0x55, 0x82, 0x78, 0x64, 0x50,
	0x04, 0x19, 0x17, 0x91, 0x87, 0x69, 0x55, 0x04,
	0x19, 0x2d, 0x96, 0x8c, 0x6e, 0x5a, 0x04, 0x19,
	0x3c, 0x96, 0x82, 0x6e, 0x55, 0x04, 0x19, 0x46,
	0x96, 0x82, 0x64, 0x50, 0x04, 0x19, 0x4b, 0x82,
	0x78, 0x64, 0x50, 0x04, 0x19, 0x55, 0x82, 0x78,
	0x55, 0x3c, 0x05, 0x64, 0x17, 0xa5, 0x9b, 0x9b,
	0x9b, 0x05, 0x64, 0x2d, 0xaa, 0xa0, 0xa0, 0xa0,
	0x05, 0x64, 0x3c, 0xaa, 0xa0, 0xa0, 0xa0, 0x05,
	0x64, 0x46, 0xaa, 0xa0, 0xa0, 0xa0, 0x05, 0x64,
	0x4b, 0x8c, 0x82, 0x82, 0x82, 0x05, 0x64, 0x55,
	0x8c, 0x82, 0x82, 0x82, 0x05, 0x50, 0x17, 0xa5,
	0x9b, 0x9b, 0x9b, 0x05, 0x50, 0x2d, 0xaa, 0xa0,
	0xa0, 0xa0, 0x05, 0x50, 0x3c, 0xaa, 0xa0, 0xa0,
	0x96, 0x05, 0x50, 0x46, 0xaa, 0xa0, 0xa0, 0x96,
	0x05, 0x50, 0x4b, 0x8c, 0x82, 0x82, 0x82, 0x05,
	0x50, 0x55, 0x8c, 0x82, 0x82, 0x82, 0x05, 0x3c,
	0x17, 0xa5, 0x9b, 0x9b, 0x87, 0x05, 0x3c, 0x2d,
	0xaa, 0xa0, 0xa0, 0x8c, 0x05, 0x3c, 0x3c, 0xaa,
	0xa0, 0x96, 0x82, 0x05, 0x3c, 0x46, 0xaa, 0xa0,
	0x96, 0x78, 0x05, 0x3c, 0x4b, 0x8c, 0x82, 0x82,
	0x78, 0x05, 0x3c, 0x55, 0x8c, 0x82, 0x82, 0x6e,
	0x05, 0x28, 0x17, 0xa5, 0x91, 0x7d, 0x69, 0x05,
	0x28, 0x2d, 0xaa, 0x96, 0x82, 0x6e, 0x05, 0x28,
	0x3c, 0xaa, 0x96, 0x78, 0x64, 0x05, 0x28, 0x46,
	0xaa, 0x8c, 0x6e, 0x5a, 0x05, 0x28, 0x4b, 0x8c,
	0x82, 0x6e, 0x5a, 0x05, 0x28, 0x55, 0x8c, 0x82,
	0x64, 0x50, 0x06, 0x3d, 0x17, 0xa5, 0x9b, 0x7d,
	0x7d, 0x06, 0x3d, 0x2d, 0xaa, 0xa0, 0x82, 0x82,
	0x06, 0x3d, 0x3c, 0xaa, 0xa0, 0x82, 0x82, 0x06,
	0x3d, 0x46, 0xaa, 0xa0, 0x82, 0x82, 0x06, 0x3d,
	0x4b, 0x8c, 0x82, 0x82, 0x82, 0x06, 0x3d, 0x55,
	0x8c, 0x82, 0x82, 0x82, 0x06, 0x32, 0x17, 0xa5,
	0x9b, 0x7d, 0x7d, 0x06, 0x32, 0x2d, 0xaa, 0xa0,
	0x82, 0x82, 0x06, 0x32, 0x3c, 0xaa, 0xa0, 0x82,
	0x82, 0x06, 0x32, 0x46, 0xaa, 0xa0, 0x82, 0x78,
	0x06, 0x32, 0x4b, 0x8c, 0x82, 0x82, 0x78, 0x06,
	0x32, 0x55, 0x8c, 0x82, 0x82, 0x6e, 0x06, 0x28,
	0x17, 0xa5, 0x9b, 0x7d, 0x73, 0x06, 0x28, 0x2d,
	0xaa, 0xa0, 0x82, 0x78, 0x06, 0x28, 0x3c, 0xaa,
	0x96, 0x82, 0x73, 0x06, 0x28, 0x46, 0xaa, 0x96,
	0x78, 0x69, 0x06, 0x28, 0x4b, 0x8c, 0x82, 0x78,
	0x69, 0x06, 0x28, 0x55, 0x8c, 0x82, 0x6e, 0x5a,
	0x06, 0x23, 0x17, 0xa5, 0x91, 0x7d, 0x69, 0x06,
	0x23, 0x2d, 0xaa, 0x96, 0x82, 0x6e, 0x06, 0x23,
	0x3c, 0xaa, 0x96, 0x78, 0x69, 0x06, 0x23, 0x46,
	0xaa, 0x8c, 0x6e, 0x5a, 0x06, 0x23, 0x4b, 0x8c,
	0x82, 0x6e, 0x5a, 0x06, 0x23, 0x55, 0x8c, 0x82,
	0x64, 0x50, 0x07, 0x3b, 0x17, 0x7d, 0x73, 0x73,
	0x73, 0x07, 0x3b, 0x2d, 0x82, 0x78, 0x78, 0x78,
	0x07, 0x3b, 0x3c, 0x82, 0x78, 0x78, 0x78, 0x07,
	0x3b, 0x4b, 0x82, 0x78, 0x78, 0x78, 0x07, 0x3b,
	0x5a, 0x82, 0x78, 0x78, 0x78, 0x07, 0x32, 0x17,
	0x7d, 0x73, 0x73, 0x73, 0x07, 0x32, 0x2d, 0x82,
	0x78, 0x78, 0x78, 0x07, 0x32, 0x3c, 0x82, 0x78,
	0x78, 0x78, 0x07, 0x32, 0x4b, 0x82, 0x78, 0x78,
	0x78, 0x07, 0x32, 0x5a, 0x82, 0x78, 0x6e, 0x64,
	0x07, 0x28, 0x17, 0x7d, 0x73, 0x73, 0x69, 0x07,
	0x28, 0x2d, 0x82, 0x78, 0x78, 0x6e, 0x07, 0x28,
	0x3c, 0x82, 0x78, 0x78, 0x64, 0x07, 0x28, 0x4b,
	0x82, 0x78, 0x78, 0x64, 0x07, 0x28, 0x5a, 0x82,
	0x78, 0x64, 0x50, 0x07, 0x23, 0x17, 0x7d, 0x73,
	0x73, 0x5f, 0x07, 0x23, 0x2d, 0x82, 0x78, 0x78,
	0x64, 0x07, 0x23, 0x3c, 0x82, 0x78, 0x78, 0x64,
	0x07, 0x23, 0x4b, 0x82, 0x78, 0x64, 0x50, 0x07,
	0x23, 0x5a, 0x82, 0x78, 0x5a, 0x46, 0x08, 0x3b,
	0x17, 0x7d, 0x73, 0x73, 0x73, 0x08, 0x3b, 0x2d,
	0x82, 0x78, 0x78, 0x78, 0x08, 0x3b, 0x3c, 0x82,
	0x78, 0x78, 0x78, 0x08, 0x3b, 0x4b, 0x82, 0x78,
	0x78, 0x78, 0x08, 0x3b, 0x5a, 0x82, 0x78, 0x78,
	0x78, 0x08, 0x32, 0x17, 0x7d, 0x73, 0x73, 0x73,
	0x08, 0x32, 0x2d, 0x82, 0x78, 0x78, 0x78, 0x08,
	0x32, 0x3c, 0x82, 0x78, 0x78, 0x78, 0x08, 0x32,
	0x4b, 0x82, 0x78, 0x78, 0x78, 0x08, 0x32, 0x5a,
	0x82, 0x78, 0x6e, 0x64, 0x08, 0x28, 0x17, 0x7d,
	0x73, 0x73, 0x69, 0x08, 0x28, 0x2d, 0x82, 0x78,
	0x78, 0x6e, 0x08, 0x28, 0x3c, 0x82, 0x78, 0x78,
	0x64, 0x08, 0x28, 0x4b, 0x82, 0x78, 0x78, 0x64,
	0x08, 0x28, 0x5a, 0x82, 0x78, 0x64, 0x50, 0x08,
	0x23, 0x17, 0x7d, 0x73, 0x73, 0x5f, 0x08, 0x23,
	0x2d, 0x82, 0x78, 0x78, 0x64, 0x08, 0x23, 0x3c,
	0x82, 0x78, 0x78, 0x64, 0x08, 0x23, 0x4b, 0x82,
	0x78, 0x64, 0x50, 0x08, 0x23, 0x5a, 0x82, 0x78,
	0x5a, 0x46, 0x0c, 0x52, 0x17, 0xa5, 0x9b, 0x9b,
	0x9b, 0x0c, 0x52, 0x2d, 0xaa, 0xa0, 0xa0, 0xa0,
	0x0c, 0x52, 0x3c, 0xaa, 0xa0, 0xa0, 0xa0, 0x0c,
	0x52, 0x46, 0xaa, 0xa0, 0xa0, 0xa0, 0x0c, 0x52,
	0x4b, 0x8c, 0x82, 0x82, 0x82, 0x0c, 0x52, 0x55,
	0x8c, 0x82, 0x82, 0x82, 0x0c, 0x42, 0x17, 0xa5,
	0x9b, 0x9b, 0x91, 0x0c, 0x42, 0x2d, 0xaa, 0xa0,
	0xa0, 0x96, 0x0c, 0x42, 0x3c, 0xaa, 0xa0, 0xa0,
	0x96, 0x0c, 0x42, 0x46, 0xaa, 0xa0, 0xa0, 0x96,
	0x0c, 0x42, 0x4b, 0x8c, 0x82, 0x82, 0x82, 0x0c,
	0x42, 0x55, 0x8c, 0x82, 0x82, 0x82, 0x0c, 0x3d,
	0x17, 0xa5, 0x9b, 0x9b, 0x91, 0x0c, 0x3d, 0x2d,
	0xaa, 0xa0, 0xa0, 0x96, 0x0c, 0x3d, 0x3c, 0xaa,
	0xa0, 0xa0, 0x8c, 0x0c, 0x3d, 0x46, 0xaa, 0xa0,
	0x96, 0x8c, 0x0c, 0x3d, 0x4b, 0x8c, 0x82, 0x82,
	0x82, 0x0c, 0x3d, 0x55, 0x8c, 0x82, 0x82, 0x82,
	0x0c, 0x32, 0x17, 0xa5, 0x9b, 0x91, 0x87, 0x0c,
	0x32, 0x2d, 0xaa, 0xa0, 0x96, 0x8c, 0x0c, 0x32,
	0x3c, 0xaa, 0xa0, 0x96, 0x82, 0x0c, 0x32, 0x46,
	0xaa, 0xa0, 0x8c, 0x78, 0x0c, 0x32, 0x4b, 0x8c,
	0x82, 0x82, 0x78, 0x0c, 0x32, 0x55, 0x8c, 0x82,
	0x82, 0x6e, 0x0c, 0x28, 0x17, 0xa5, 0x9b, 0x87,
	0x73, 0x0c, 0x28, 0x2d, 0xaa, 0xa0, 0x8c, 0x78,
	0x0c, 0x28, 0x3c, 0xaa, 0x96, 0x82, 0x73, 0x0c,
	0x28, 0x46, 0xaa, 0x96, 0x78, 0x69, 0x0c, 0x28,
	0x4b, 0x8c, 0x82, 0x78, 0x69, 0x0c, 0x28, 0x55,
	0x8c, 0x82, 0x6e, 0x5a, 0x0c, 0x23, 0x17, 0xa5,
	0x91, 0x7d, 0x69, 0x0c, 0x23, 0x2d, 0xaa, 0x96,
	0x82, 0x6e, 0x0c, 0x23, 0x3c, 0xaa, 0x96, 0x78,
	0x69, 0x0c, 0x23, 0x46, 0xaa, 0x8c, 0x6e, 0x5a,
	0x0c, 0x23, 0x4b, 0x8c, 0x82, 0x6e, 0x5a, 0x0c,
	0x23, 0x55, 0x8c, 0x82, 0x64, 0x50, 0x0d, 0x64,
	0x17, 0xa5, 0x9b, 0x9b, 0x9b, 0x0d, 0x64, 0x2d,
	0xaa, 0xa0, 0xa0, 0xa0, 0x0d, 0x64, 0x3c, 0xaa,
	0xa0, 0xa0, 0xa0, 0x0d, 0x64, 0x46, 0xaa, 0xa0,
	0xa0, 0xa0, 0x0d, 0x64, 0x4b, 0x8c, 0x82, 0x82,
	0x82, 0x0d, 0x64, 0x55, 0x8c, 0x82, 0x82, 0x82,
	0x0d, 0x50, 0x17, 0xa5, 0x9b, 0x9b, 0x9b, 0x0d,
	0x50, 0x2d, 0xaa, 0xa0, 0xa0, 0xa0, 0x0d, 0x50,
	0x3c, 0xaa, 0xa0, 0xa0, 0x96, 0x0d, 0x50, 0x46,
	0xaa, 0xa0, 0xa0, 0x96, 0x0d, 0x50, 0x4b, 0x8c,
	0x82, 0x82, 0x82, 0x0d, 0x50, 0x55, 0x8c, 0x82,
	0x82, 0x82, 0x0d, 0x3c, 0x17, 0xa5, 0x9b, 0x9b,
	0x87, 0x0d, 0x3c, 0x2d, 0xaa, 0xa0, 0xa0, 0x8c,
	0x0d, 0x3c, 0x3c, 0xaa, 0xa0, 0x96, 0x82, 0x0d,
	0x3c, 0x46, 0xaa, 0xa0, 0x96, 0x78, 0x0d, 0x3c,
	0x4b, 0x8c, 0x82, 0x82, 0x78, 0x0d, 0x3c, 0x55,
	0x8c, 0x82, 0x82, 0x6e, 0x0d, 0x28, 0x17, 0xa5,
	0x91, 0x7d, 0x69, 0x0d, 0x28, 0x2d, 0xaa, 0x96,
	0x82, 0x6e, 0x0d, 0x28, 0x3c, 0xaa, 0x96, 0x78,
	0x64, 0x0d, 0x28, 0x46, 0xaa, 0x8c, 0x6e, 0x5a,
	0x0d, 0x28, 0x4b, 0x8c, 0x82, 0x6e, 0x5a, 0x0d,
	0x28, 0x55, 0x8c, 0x82, 0x64, 0x50,
};


static struct tegra_system_edp_entry __initdata tegra_system_edp_map[] = {

/* {SKU, power-limit (in 100mW), {freq limits (in 10Mhz)} } */

	{  1,  49, {130, 120, 120, 120} },
	{  1,  44, {130, 120, 120, 110} },
	{  1,  37, {130, 120, 110, 100} },
	{  1,  35, {130, 120, 110,  90} },
	{  1,  29, {130, 120, 100,  80} },
	{  1,  27, {130, 120,  90,  80} },
	{  1,  25, {130, 110,  80,  60} },
	{  1,  21, {130, 100,  80,  40} },

	{  4,  49, {130, 120, 120, 120} },
	{  4,  44, {130, 120, 120, 110} },
	{  4,  37, {130, 120, 110, 100} },
	{  4,  35, {130, 120, 110,  90} },
	{  4,  29, {130, 120, 100,  80} },
	{  4,  27, {130, 120,  90,  80} },
	{  4,  25, {130, 110,  80,  60} },
	{  4,  21, {130, 100,  80,  40} },
};

/*
 * "Safe entry" to be used when no match for speedo_id /
 * regulator_cur is found; must be the last one
 */
static struct tegra_edp_limits edp_default_limits[] = {
	{85, {1000000, 1000000, 1000000, 1000000} },
};

static struct tegra_system_edp_entry power_edp_default_limits[] = {
	{0, 20, {1000000, 1000000, 1000000, 1000000} },
};

/* Constants for EDP calculations */
static const int temperatures[] = { /* degree celcius (C) */
	23, 40, 50, 60, 70, 74, 78, 82, 86, 90, 94, 98, 102,
};

static const int power_cap_levels[] = { /* milliwatts (mW) */
	  700,  1700,  2700,  3700,  3800,  3900,  4500,  4600,
	 4700,  4800,  4900,  5200,  5300,  5400,  5500,  5800,
	 5900,  6200,  6400,  6500,  6800,  7200,  7500,  8200,
	 8500,  9200,  9500, 10200, 10500, 11200, 11500, 12200,
	12500, 13200, 13500, 14200, 14500, 15200, 15500, 16500,
	17500
};

struct tegra_edp_limits edp_fix_limits_speedo3[] = {
	{23, {1810500, 1810500, 1810500, 1810500} },
	{40, {1810500, 1810500, 1810500, 1810500} },
	{50, {1810500, 1810500, 1810500, 1810500} },
	{60, {1810500, 1810500, 1810500, 1810500} },
	{70, {1810500, 1810500, 1810500, 1810500} },
	{74, {1810500, 1810500, 1810500, 1810500} },
	{78, {1810500, 1810500, 1810500, 1810500} },
	{82, {1810500, 1810500, 1810500, 1810500} },
	{86, {1810500, 1810500, 1810500, 1810500} },
	{90, {1810500, 1810500, 1810500, 1810500} },
	{94, {1810500, 1810500, 1810500, 1810500} },
	{98, {1810500, 1810500, 1810500, 1810500} },
	{102, {1810500, 1810500, 1810500, 1810500} },
};

struct tegra_system_edp_entry power_edp_fix_limits_speedo3[] = {
	{3, 7, {396000, 204000, 102000, 102000} },
	{3, 17, {1122000, 612000, 396000, 306000} },
	{3, 27, {1428000, 1020000, 612000, 510000} },
	{3, 37, {1708500, 1224000, 1020000, 768000} },
	{3, 38, {1708500, 1326000, 1020000, 816000} },
	{3, 39, {1708500, 1326000, 1020000, 816000} },
	{3, 45, {1810500, 1428000, 1020000, 816000} },
	{3, 46, {1810500, 1428000, 1224000, 918000} },
	{3, 47, {1810500, 1428000, 1224000, 1020000} },
	{3, 48, {1810500, 1428000, 1224000, 1020000} },
	{3, 49, {1810500, 1428000, 1224000, 1020000} },
	{3, 52, {1810500, 1530000, 1224000, 1122000} },
	{3, 53, {1810500, 1530000, 1326000, 1122000} },
	{3, 54, {1810500, 1530000, 1326000, 1122000} },
	{3, 55, {1810500, 1530000, 1326000, 1122000} },
	{3, 58, {1810500, 1606500, 1326000, 1122000} },
	{3, 59, {1810500, 1606500, 1326000, 1122000} },
	{3, 62, {1810500, 1606500, 1428000, 1224000} },
	{3, 64, {1810500, 1606500, 1428000, 1224000} },
	{3, 65, {1810500, 1708500, 1428000, 1224000} },
	{3, 68, {1810500, 1708500, 1428000, 1224000} },
	{3, 72, {1810500, 1708500, 1530000, 1326000} },
	{3, 75, {1810500, 1810500, 1530000, 1326000} },
	{3, 82, {1810500, 1810500, 1606500, 1428000} },
	{3, 85, {1810500, 1810500, 1606500, 1428000} },
	{3, 92, {1810500, 1810500, 1708500, 1530000} },
	{3, 95, {1810500, 1810500, 1708500, 1530000} },
	{3, 102, {1810500, 1810500, 1708500, 1606500} },
	{3, 105, {1810500, 1810500, 1810500, 1606500} },
	{3, 112, {1810500, 1810500, 1810500, 1606500} },
	{3, 115, {1810500, 1810500, 1810500, 1606500} },
	{3, 122, {1810500, 1810500, 1810500, 1708500} },
	{3, 125, {1810500, 1810500, 1810500, 1708500} },
	{3, 132, {1810500, 1810500, 1810500, 1708500} },
	{3, 135, {1810500, 1810500, 1810500, 1810500} },
	{3, 142, {1810500, 1810500, 1810500, 1810500} },
	{3, 145, {1810500, 1810500, 1810500, 1810500} },
	{3, 152, {1810500, 1810500, 1810500, 1810500} },
	{3, 155, {1810500, 1810500, 1810500, 1810500} },
	{3, 165, {1810500, 1810500, 1810500, 1810500} },
	{3, 175, {1810500, 1810500, 1810500, 1810500} },
};

static struct tegra_edp_freq_voltage_table *freq_voltage_lut_saved;
static unsigned int freq_voltage_lut_size_saved;
static struct tegra_edp_freq_voltage_table *freq_voltage_lut;
static unsigned int freq_voltage_lut_size;

static inline s64 edp_pow(s64 val, int pwr)
{
	s64 retval = 1;

	while (val && pwr) {
		if (pwr & 1)
			retval *= val;
		pwr >>= 1;
		if (pwr)
			val *= val;
	}

	return retval;
}


#ifdef CONFIG_TEGRA_CPU_EDP_FIXED_LIMITS
static inline unsigned int edp_apply_fixed_limits(
				unsigned int in_freq_KHz,
				struct tegra_edp_cpu_leakage_params *params,
				unsigned int cur_effective,
				int temp_C, int n_cores_idx)
{
	unsigned int out_freq_KHz = in_freq_KHz;
	unsigned int max_cur, max_temp, max_freq;
	int i;

	/* Apply any additional fixed limits */
	for (i = 0; i < 8; i++) {
		max_cur = params->max_current_cap[i].max_cur;
		if (max_cur != 0 && cur_effective <= max_cur) {
			max_temp = params->max_current_cap[i].max_temp;
			if (max_temp != 0 && temp_C > max_temp) {
				max_freq = params->max_current_cap[i].
					max_freq[n_cores_idx];
				if (max_freq && max_freq < out_freq_KHz)
					out_freq_KHz = max_freq;
			}
		}
	}

	return out_freq_KHz;
}
#else
#define edp_apply_fixed_limits(freq, unused...)	(freq)
#endif

/*
 * Find the maximum frequency that results in dynamic and leakage current that
 * is less than the regulator current limit.
 * temp_C - valid or -EINVAL
 * power_mW - valid or -1 (infinite) or -EINVAL
 */
static unsigned int edp_calculate_maxf(
				struct tegra_edp_cpu_leakage_params *params,
				int temp_C, int power_mW,
				int iddq_mA,
				int n_cores_idx)
{
	unsigned int voltage_mV, freq_KHz = 0;
	unsigned int cur_effective = regulator_cur - edp_reg_override_mA;
	int f, i, j, k, r = 0;
	s64 leakage_mA, dyn_mA, leakage_calc_step;
	s64 leakage_mW, dyn_mW;

	for (f = freq_voltage_lut_size - 1; f >= 0; f--) {
		freq_KHz = freq_voltage_lut[f].freq / 1000;
		voltage_mV = freq_voltage_lut[f].voltage_mV;

		/* Constrain Volt-Temp. Eg. at Tj >= 70C, no VDD_CPU > 1.24V */
		if (temp_C > params->volt_temp_cap.temperature &&
		    voltage_mV > params->volt_temp_cap.voltage_limit_mV)
			continue;

		/* Calculate leakage current */
		leakage_mA = 0;
		for (i = 0; i <= 3; i++) {
			for (j = 0; j <= 3; j++) {
				for (k = 0; k <= 3; k++) {
					leakage_calc_step =
						params->leakage_consts_ijk
						[i][j][k] * edp_pow(iddq_mA, i);
					/* Convert (mA)^i to (A)^i */
					leakage_calc_step =
						div64_s64(leakage_calc_step,
							  edp_pow(1000, i));
					leakage_calc_step *=
						edp_pow(voltage_mV, j);
					/* Convert (mV)^j to (V)^j */
					leakage_calc_step =
						div64_s64(leakage_calc_step,
							  edp_pow(1000, j));
					leakage_calc_step *=
						edp_pow(temp_C, k);
					/* Convert (C)^k to (scaled_C)^k */
					leakage_calc_step =
						div64_s64(leakage_calc_step,
						edp_pow(params->temp_scaled,
							k));
					/* leakage_consts_ijk was scaled */
					leakage_calc_step =
						div64_s64(leakage_calc_step,
							  params->ijk_scaled);
					leakage_mA += leakage_calc_step;
				}
			}
		}

		/* if specified, set floor for leakage current */
		if (params->leakage_min && leakage_mA <= params->leakage_min)
			leakage_mA = params->leakage_min;

		/* leakage cannot be negative => leakage model has error */
		if (leakage_mA <= 0) {
			pr_err("VDD_CPU EDP failed: IDDQ too high (%d mA)\n",
			       iddq_mA);
			r = -EINVAL;
			goto end;
		}

		leakage_mA *= params->leakage_consts_n[n_cores_idx];

		/* leakage_const_n was scaled */
		leakage_mA = div64_s64(leakage_mA, params->consts_scaled);

		/* Calculate dynamic current */
		dyn_mA = voltage_mV * freq_KHz / 1000;
		/* Convert mV to V */
		dyn_mA = div64_s64(dyn_mA, 1000);
		dyn_mA *= params->dyn_consts_n[n_cores_idx];
		/* dyn_const_n was scaled */
		dyn_mA = div64_s64(dyn_mA, params->dyn_scaled);

		if (power_mW != -1) {
			leakage_mW = leakage_mA * voltage_mV;
			dyn_mW = dyn_mA * voltage_mV;
			if (div64_s64(leakage_mW + dyn_mW, 1000) <= power_mW)
				goto end;
		} else if ((leakage_mA + dyn_mA) <= cur_effective) {
			goto end;
		}
		freq_KHz = 0;
	}

 end:
	if (r != 0)
		return r;

	return edp_apply_fixed_limits(freq_KHz, params,
					cur_effective, temp_C, n_cores_idx);
}

static int edp_relate_freq_voltage(struct clk *clk_cpu_g,
			unsigned int cpu_speedo_idx,
			unsigned int freq_volt_lut_size,
			struct tegra_edp_freq_voltage_table *freq_volt_lut)
{
	unsigned int i, j, freq;
	int voltage_mV;

	for (i = 0, j = 0, freq = 0;
		 i < freq_volt_lut_size;
		 i++, freq += FREQ_STEP) {

		/* Predict voltages */
		voltage_mV = tegra_dvfs_predict_millivolts(clk_cpu_g, freq);
		if (voltage_mV < 0) {
			pr_err("%s: couldn't predict voltage: freq %u; err %d",
			       __func__, freq, voltage_mV);
			return -EINVAL;
		}

		/* Cache frequency / voltage / voltage constant relationship */
		freq_volt_lut[i].freq = freq;
		freq_volt_lut[i].voltage_mV = voltage_mV;
	}
	return 0;
}

unsigned int tegra_edp_find_maxf(int volt)
{
	unsigned int i;

	for (i = 0; i < freq_voltage_lut_size_saved; i++) {
		if (freq_voltage_lut_saved[i].voltage_mV > volt)
			break;
	}
	return freq_voltage_lut[i - 1].freq;
}


static int edp_find_speedo_idx(int cpu_speedo_id, unsigned int *cpu_speedo_idx)
{
	int i, array_size;
	struct tegra_edp_cpu_leakage_params *params;

	switch (tegra_chip_id) {
	case TEGRA_CHIPID_TEGRA11:
		params = tegra11x_get_leakage_params(0, &array_size);
		break;
	case TEGRA_CHIPID_TEGRA3:
	case TEGRA_CHIPID_TEGRA2:
	default:
		array_size = 0;
		break;
	}

	for (i = 0; i < array_size; i++)
		if (cpu_speedo_id == params[i].cpu_speedo_id) {
			*cpu_speedo_idx = i;
			return 0;
		}

	pr_err("%s: couldn't find cpu speedo id %d in freq/voltage LUT\n",
	       __func__, cpu_speedo_id);
	return -EINVAL;
}

static int init_cpu_edp_limits_calculated(void)
{
	unsigned int max_nr_cpus = num_possible_cpus();
	unsigned int temp_idx, n_cores_idx, pwr_idx;
	unsigned int cpu_g_minf, cpu_g_maxf;
	unsigned int iddq_mA;
	unsigned int cpu_speedo_idx;
	unsigned int cap, limit;
	struct tegra_edp_limits *edp_calculated_limits;
	struct tegra_system_edp_entry *power_edp_calc_limits;
	struct tegra_edp_cpu_leakage_params *params;
	int ret;
	struct clk *clk_cpu_g = tegra_get_clock_by_name("cpu_g");
	int cpu_speedo_id = tegra_cpu_speedo_id();

	if (cpu_speedo_id == 3) {
		edp_limits = edp_fix_limits_speedo3;
		edp_limits_size = ARRAY_SIZE(edp_fix_limits_speedo3);
		power_edp_limits = power_edp_fix_limits_speedo3;
		power_edp_limits_size = ARRAY_SIZE(power_edp_fix_limits_speedo3);
		return 0;
	}

	/* Determine all inputs to EDP formula */
	iddq_mA = tegra_get_cpu_iddq_value();
	ret = edp_find_speedo_idx(cpu_speedo_id, &cpu_speedo_idx);
	if (ret)
		return ret;

	switch (tegra_chip_id) {
	case TEGRA_CHIPID_TEGRA11:
		params = tegra11x_get_leakage_params(cpu_speedo_idx, NULL);
		break;
	case TEGRA_CHIPID_TEGRA3:
	case TEGRA_CHIPID_TEGRA2:
	default:
		return -EINVAL;
	}

	edp_calculated_limits = kmalloc(sizeof(struct tegra_edp_limits)
					* ARRAY_SIZE(temperatures), GFP_KERNEL);
	BUG_ON(!edp_calculated_limits);

	power_edp_calc_limits = kmalloc(sizeof(struct tegra_system_edp_entry)
				* ARRAY_SIZE(power_cap_levels), GFP_KERNEL);
	BUG_ON(!power_edp_calc_limits);

	cpu_g_minf = 0;
	cpu_g_maxf = clk_get_max_rate(clk_cpu_g);
	freq_voltage_lut_size = (cpu_g_maxf - cpu_g_minf) / FREQ_STEP + 1;
	freq_voltage_lut = kmalloc(sizeof(struct tegra_edp_freq_voltage_table)
				   * freq_voltage_lut_size, GFP_KERNEL);
	if (!freq_voltage_lut) {
		pr_err("%s: failed alloc mem for freq/voltage LUT\n", __func__);
		return -ENOMEM;
	}

	ret = edp_relate_freq_voltage(clk_cpu_g, cpu_speedo_idx,
				freq_voltage_lut_size, freq_voltage_lut);
	if (ret) {
		kfree(freq_voltage_lut);
		return ret;
	}

	if (freq_voltage_lut_size != freq_voltage_lut_size_saved) {
		/* release previous table if present */
		kfree(freq_voltage_lut_saved);
		/* create table to save */
		freq_voltage_lut_saved =
			kmalloc(sizeof(struct tegra_edp_freq_voltage_table) *
			freq_voltage_lut_size, GFP_KERNEL);
		if (!freq_voltage_lut_saved) {
			pr_err("%s: failed alloc mem for freq/voltage LUT\n",
				__func__);
			kfree(freq_voltage_lut);
			return -ENOMEM;
		}
		freq_voltage_lut_size_saved = freq_voltage_lut_size;
	}
	memcpy(freq_voltage_lut_saved,
		freq_voltage_lut,
		sizeof(struct tegra_edp_freq_voltage_table) *
			freq_voltage_lut_size);

	/* Calculate EDP table */
	for (n_cores_idx = 0; n_cores_idx < max_nr_cpus; n_cores_idx++) {
		for (temp_idx = 0;
		     temp_idx < ARRAY_SIZE(temperatures); temp_idx++) {
			edp_calculated_limits[temp_idx].temperature =
				temperatures[temp_idx];
			limit = edp_calculate_maxf(params,
						   temperatures[temp_idx],
						   -1,
						   iddq_mA,
						   n_cores_idx);
			if (limit == -EINVAL)
				return -EINVAL;
			/* apply safety cap if it is specified */
			if (n_cores_idx < 4) {
				cap = params->safety_cap[n_cores_idx];
				if (cap && cap < limit)
					limit = cap;
			}
			edp_calculated_limits[temp_idx].
				freq_limits[n_cores_idx] = limit;
		}

		for (pwr_idx = 0;
		     pwr_idx < ARRAY_SIZE(power_cap_levels); pwr_idx++) {
			power_edp_calc_limits[pwr_idx].power_limit_100mW =
				power_cap_levels[pwr_idx] / 100;
			limit = edp_calculate_maxf(params,
						   50,
						   power_cap_levels[pwr_idx],
						   iddq_mA,
						   n_cores_idx);
			if (limit == -EINVAL)
				return -EINVAL;
			power_edp_calc_limits[pwr_idx].
				freq_limits[n_cores_idx] = limit;
		}
	}

	/*
	 * If this is an EDP table update, need to overwrite old table.
	 * The old table's address must remain valid.
	 */
	if (edp_limits != edp_default_limits) {
		memcpy(edp_limits, edp_calculated_limits,
		       sizeof(struct tegra_edp_limits)
		       * ARRAY_SIZE(temperatures));
		kfree(edp_calculated_limits);
	}
	else {
		edp_limits = edp_calculated_limits;
		edp_limits_size = ARRAY_SIZE(temperatures);
	}

	if (power_edp_limits != power_edp_default_limits) {
		memcpy(power_edp_limits, power_edp_calc_limits,
		       sizeof(struct tegra_system_edp_entry)
		       * ARRAY_SIZE(power_cap_levels));
		kfree(power_edp_calc_limits);
	} else {
		power_edp_limits = power_edp_calc_limits;
		power_edp_limits_size = ARRAY_SIZE(power_cap_levels);
	}

	kfree(freq_voltage_lut);
	return 0;
}

static int __init init_cpu_edp_limits_lookup(void)
{
	int i, j;
	struct tegra_edp_limits *e;
	struct tegra_edp_vdd_cpu_entry *t;
	int tsize;
	int cpu_speedo_id = tegra_cpu_speedo_id();

	t = (struct tegra_edp_vdd_cpu_entry *)tegra_edp_vdd_cpu_map;
	tsize = sizeof(tegra_edp_vdd_cpu_map)
		/ sizeof(struct tegra_edp_vdd_cpu_entry);

	for (i = 0; i < tsize; i++) {
		if (t[i].speedo_id == cpu_speedo_id &&
		    t[i].regulator_100mA <= regulator_cur / 100)
			break;
	}

	/* No entry found in tegra_edp_vdd_cpu_map */
	if (i >= tsize)
		return -EINVAL;

	/* Find all rows for this entry */
	for (j = i + 1; j < tsize; j++) {
		if (t[i].speedo_id != t[j].speedo_id ||
		    t[i].regulator_100mA != t[j].regulator_100mA)
			break;
	}

	edp_limits_size = j - i;
	e = kmalloc(sizeof(struct tegra_edp_limits) * edp_limits_size,
		    GFP_KERNEL);
	BUG_ON(!e);

	for (j = 0; j < edp_limits_size; j++) {
		e[j].temperature = (int)t[i+j].temperature;
		e[j].freq_limits[0] = (unsigned int)t[i+j].freq_limits[0]*10000;
		e[j].freq_limits[1] = (unsigned int)t[i+j].freq_limits[1]*10000;
		e[j].freq_limits[2] = (unsigned int)t[i+j].freq_limits[2]*10000;
		e[j].freq_limits[3] = (unsigned int)t[i+j].freq_limits[3]*10000;
	}

	if (edp_limits != edp_default_limits)
		kfree(edp_limits);

	edp_limits = e;
	return 0;
}

void tegra_recalculate_cpu_edp_limits(void)
{
	if (tegra_chip_id != TEGRA11X)
		return;

	if (init_cpu_edp_limits_calculated() == 0)
		return;

	/* Revert to default EDP table on error */
	edp_limits = edp_default_limits;
	edp_limits_size = ARRAY_SIZE(edp_default_limits);

	power_edp_limits = power_edp_default_limits;
	power_edp_limits_size = ARRAY_SIZE(power_edp_default_limits);
}

/*
 * Specify regulator current in mA, e.g. 5000mA
 * Use 0 for default
 */
void __init tegra_init_cpu_edp_limits(unsigned int regulator_mA)
{
	if (!regulator_mA)
		goto end;
	regulator_cur = regulator_mA + OVERRIDE_DEFAULT;

	switch (tegra_chip_id) {
	case TEGRA30:
		if (init_cpu_edp_limits_lookup() == 0)
			return;
		break;
	case TEGRA11X:
		if (init_cpu_edp_limits_calculated() == 0)
			return;
		break;
	case TEGRA20:
	default:
		BUG();
		break;
	}

 end:
	edp_limits = edp_default_limits;
	edp_limits_size = ARRAY_SIZE(edp_default_limits);

	power_edp_limits = power_edp_default_limits;
	power_edp_limits_size = ARRAY_SIZE(power_edp_default_limits);
}

void __init tegra_init_system_edp_limits(unsigned int power_limit_mW)
{
	int cpu_speedo_id = tegra_cpu_speedo_id();
	int i;
	unsigned int *e;
	struct tegra_system_edp_entry *t = tegra_system_edp_map;
	int tsize = ARRAY_SIZE(tegra_system_edp_map);

	if (!power_limit_mW) {
		e = NULL;
		goto out;
	}

	for (i = 0; i < tsize; i++)
		if (t[i].speedo_id == cpu_speedo_id)
			break;

	if (i >= tsize) {
		e = NULL;
		goto out;
	}

	do {
		if (t[i].power_limit_100mW <= power_limit_mW / 100)
			break;
		i++;
	} while (i < tsize && t[i].speedo_id == cpu_speedo_id);

	if (i >= tsize || t[i].speedo_id != cpu_speedo_id)
		i--; /* No low enough entry in the table, use best possible */

	e = kmalloc(sizeof(unsigned int) * 4, GFP_KERNEL);
	BUG_ON(!e);

	e[0] = (unsigned int)t[i].freq_limits[0] * 10000;
	e[1] = (unsigned int)t[i].freq_limits[1] * 10000;
	e[2] = (unsigned int)t[i].freq_limits[2] * 10000;
	e[3] = (unsigned int)t[i].freq_limits[3] * 10000;

out:
	kfree(system_edp_limits);

	system_edp_limits = e;
}


void tegra_get_cpu_edp_limits(const struct tegra_edp_limits **limits, int *size)
{
	*limits = edp_limits;
	*size = edp_limits_size;
}

void tegra_get_system_edp_limits(const unsigned int **limits)
{
	*limits = system_edp_limits;
}

void tegra_platform_edp_init(struct thermal_trip_info *trips,
				int *num_trips, int margin)
{
	const struct tegra_edp_limits *cpu_edp_limits;
	struct thermal_trip_info *trip_state;
	int i, cpu_edp_limits_size;

	if (!trips || !num_trips)
		return;

	/* edp capping */
	tegra_get_cpu_edp_limits(&cpu_edp_limits, &cpu_edp_limits_size);

	if (cpu_edp_limits_size > MAX_THROT_TABLE_SIZE)
		BUG();

	for (i = 0; i < cpu_edp_limits_size-1; i++) {
		trip_state = &trips[*num_trips];

		trip_state->cdev_type = "cpu_edp";
		trip_state->trip_temp =
			(cpu_edp_limits[i].temperature * 1000) - margin;
		trip_state->trip_type = THERMAL_TRIP_ACTIVE;
		trip_state->upper = trip_state->lower = i + 1;

		(*num_trips)++;

		if (*num_trips >= THERMAL_MAX_TRIPS)
			BUG();
	}
}

struct tegra_system_edp_entry *tegra_get_system_edp_entries(int *size)
{
	*size = power_edp_limits_size;
	return power_edp_limits;
}

#ifdef CONFIG_DEBUG_FS

static int edp_limit_debugfs_show(struct seq_file *s, void *data)
{
	seq_printf(s, "%u\n", tegra_get_edp_limit(NULL));
	return 0;
}

static inline void edp_show_4core_edp_table(struct seq_file *s, int th_idx)
{
	int i;

	seq_printf(s, "%6s %10s %10s %10s %10s\n",
		   " Temp.", "1-core", "2-cores", "3-cores", "4-cores");
	for (i = 0; i < edp_limits_size; i++) {
		seq_printf(s, "%c%3dC: %10u %10u %10u %10u\n",
			   i == th_idx ? '>' : ' ',
			   edp_limits[i].temperature,
			   edp_limits[i].freq_limits[0],
			   edp_limits[i].freq_limits[1],
			   edp_limits[i].freq_limits[2],
			   edp_limits[i].freq_limits[3]);
	}
}

static inline void edp_show_2core_edp_table(struct seq_file *s, int th_idx)
{
	int i;

	seq_printf(s, "%6s %10s %10s\n",
		   " Temp.", "1-core", "2-cores");
	for (i = 0; i < edp_limits_size; i++) {
		seq_printf(s, "%c%3dC: %10u %10u\n",
			   i == th_idx ? '>' : ' ',
			   edp_limits[i].temperature,
			   edp_limits[i].freq_limits[0],
			   edp_limits[i].freq_limits[1]);
	}
}

static inline void edp_show_4core_power_table(struct seq_file *s)
{
	int i;

	seq_printf(s, "%6s %10s %10s %10s %10s\n",
		   " Power", "1-core", "2-cores", "3-cores", "4-cores");
	for (i = 0; i < power_edp_limits_size; i++) {
		seq_printf(s, "%5dmW: %10u %10u %10u %10u\n",
			   power_edp_limits[i].power_limit_100mW * 100,
			   power_edp_limits[i].freq_limits[0],
			   power_edp_limits[i].freq_limits[1],
			   power_edp_limits[i].freq_limits[2],
			   power_edp_limits[i].freq_limits[3]);
	}
}

static inline void edp_show_2core_power_table(struct seq_file *s)
{
	int i;

	seq_printf(s, "%6s %10s %10s\n",
		   " Power", "1-core", "2-cores");
	for (i = 0; i < power_edp_limits_size; i++) {
		seq_printf(s, "%5dmW: %10u %10u\n",
			   power_edp_limits[i].power_limit_100mW * 100,
			   power_edp_limits[i].freq_limits[0],
			   power_edp_limits[i].freq_limits[1]);
	}
}

static inline void edp_show_4core_system_table(struct seq_file *s)
{
	seq_printf(s, "%10u %10u %10u %10u\n",
		   system_edp_limits[0],
		   system_edp_limits[1],
		   system_edp_limits[2],
		   system_edp_limits[3]);
}

static inline void edp_show_2core_system_table(struct seq_file *s)
{
	seq_printf(s, "%10u %10u\n",
		   system_edp_limits[0],
		   system_edp_limits[1]);
}

static int edp_debugfs_show(struct seq_file *s, void *data)
{
	unsigned int max_nr_cpus = num_possible_cpus();
	int th_idx;

	if (max_nr_cpus != 2 && max_nr_cpus != 4) {
		seq_printf(s, "Unsupported number of CPUs\n");
		return 0;
	}

	tegra_get_edp_limit(&th_idx);
	seq_printf(s, "-- VDD_CPU %sEDP table (%umA = %umA - %umA) --\n",
		   edp_limits == edp_default_limits ? "**default** " : "",
		   regulator_cur - edp_reg_override_mA,
		   regulator_cur, edp_reg_override_mA);
	if (max_nr_cpus == 2)
		edp_show_2core_edp_table(s, th_idx);
	else if (max_nr_cpus == 4)
		edp_show_4core_edp_table(s, th_idx);

	seq_printf(s, "-- VDD_CPU %sPower EDP table --\n",
		   power_edp_limits == power_edp_default_limits ?
		   "**default** " : "");
	if (max_nr_cpus == 2)
		edp_show_2core_power_table(s);
	else if (max_nr_cpus == 4)
		edp_show_4core_power_table(s);

	if (system_edp_limits) {
		seq_printf(s, "\n-- System EDP table --\n");
		if (max_nr_cpus == 2)
			edp_show_2core_system_table(s);
		else if (max_nr_cpus == 4)
			edp_show_4core_system_table(s);
	}
	return 0;
}

static int edp_reg_override_show(struct seq_file *s, void *data)
{
	seq_printf(s, "Limit override: %u mA. Effective limit: %u mA\n",
		   edp_reg_override_mA, regulator_cur - edp_reg_override_mA);
	return 0;
}

static int edp_reg_override_write(struct file *file,
	const char __user *userbuf, size_t count, loff_t *ppos)
{
	char buf[32], *end;
	unsigned int edp_reg_override_mA_temp;
	unsigned int edp_reg_override_mA_prev = edp_reg_override_mA;

	if (sizeof(buf) <= count)
		goto override_err;

	if (copy_from_user(buf, userbuf, count))
		goto override_err;

	/* terminate buffer and trim - white spaces may be appended
	 *  at the end when invoked from shell command line */
	buf[count]='\0';
	strim(buf);

	edp_reg_override_mA_temp = simple_strtoul(buf, &end, 10);
	if (*end != '\0')
		goto override_err;

	if (edp_reg_override_mA_temp >= regulator_cur)
		goto override_err;

	if (edp_reg_override_mA == edp_reg_override_mA_temp)
		return count;

	edp_reg_override_mA = edp_reg_override_mA_temp;
	if (init_cpu_edp_limits_calculated()) {
		/* Revert to previous override value if new value fails */
		edp_reg_override_mA = edp_reg_override_mA_prev;
		goto override_err;
	}

	if (tegra_cpu_set_speed_cap(NULL)) {
		pr_err("FAILED: Set CPU freq cap with new VDD_CPU EDP table\n");
		goto override_out;
	}

	pr_info("Reinitialized VDD_CPU EDP table with regulator current limit"
			" %u mA\n", regulator_cur - edp_reg_override_mA);

	return count;

override_err:
	pr_err("FAILED: Reinitialize VDD_CPU EDP table with override \"%s\"",
	       buf);
override_out:
	return -EINVAL;
}

static int edp_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, edp_debugfs_show, inode->i_private);
}

static int edp_limit_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, edp_limit_debugfs_show, inode->i_private);
}

static int edp_reg_override_open(struct inode *inode, struct file *file)
{
	return single_open(file, edp_reg_override_show, inode->i_private);
}

static const struct file_operations edp_debugfs_fops = {
	.open		= edp_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations edp_limit_debugfs_fops = {
	.open		= edp_limit_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations edp_reg_override_debugfs_fops = {
	.open		= edp_reg_override_open,
	.read		= seq_read,
	.write		= edp_reg_override_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#ifdef CONFIG_EDP_FRAMEWORK
static __init struct dentry *tegra_edp_debugfs_dir(void)
{
	return edp_debugfs_dir;
}
#else
static __init struct dentry *tegra_edp_debugfs_dir(void)
{
	return debugfs_create_dir("edp", NULL);
}
#endif

static int __init tegra_edp_debugfs_init(void)
{
	struct dentry *d_edp;
	struct dentry *d_edp_limit;
	struct dentry *d_edp_reg_override;
	struct dentry *edp_dir;
	struct dentry *vdd_cpu_dir;

	edp_dir = tegra_edp_debugfs_dir();

	if (!edp_dir)
		goto edp_dir_err;

	vdd_cpu_dir = debugfs_create_dir("vdd_cpu", edp_dir);

	if (!vdd_cpu_dir)
		goto vdd_cpu_dir_err;

	d_edp = debugfs_create_file("edp", S_IRUGO, vdd_cpu_dir, NULL,
				&edp_debugfs_fops);

	if (!d_edp)
		goto edp_err;

	d_edp_limit = debugfs_create_file("edp_limit", S_IRUGO, vdd_cpu_dir,
				NULL, &edp_limit_debugfs_fops);

	if (!d_edp_limit)
		goto edp_limit_err;

	d_edp_reg_override = debugfs_create_file("edp_reg_override",
				S_IRUGO | S_IWUSR, vdd_cpu_dir, NULL,
				&edp_reg_override_debugfs_fops);

	if (!d_edp_reg_override)
		goto edp_reg_override_err;

	if (tegra_core_edp_debugfs_init(edp_dir))
		goto edp_reg_override_err;

	return 0;

edp_reg_override_err:
	debugfs_remove(d_edp_limit);
edp_limit_err:
	debugfs_remove(d_edp);
edp_err:
	debugfs_remove(vdd_cpu_dir);
vdd_cpu_dir_err:
	debugfs_remove(edp_dir);
edp_dir_err:
	return -ENOMEM;
}

late_initcall(tegra_edp_debugfs_init);
#endif /* CONFIG_DEBUG_FS */
