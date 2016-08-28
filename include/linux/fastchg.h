/*
 * Author: Chad Froebel <chadfroebel@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_FASTCHG_H
#define _LINUX_FASTCHG_H

#include <linux/notifier.h>

#define FAST_CHARGE_DISABLED 0	/* default */
#define FAST_CHARGE_FORCE_AC 1

#ifdef CONFIG_FORCE_FAST_CHARGE
extern int force_fast_charge;
extern int force_fast_charge_register_notifier(struct notifier_block *nb);
extern int force_fast_charge_unregister_notifier(struct notifier_block *nb);
#else
enum { force_fast_charge = FAST_CHARGE_DISABLED };
static inline int force_fast_charge_register_notifier(struct notifier_block *nb) { return 0; }
static inline int force_fast_charge_unregister_notifier(struct notifier_block *nb) { return 0; }
#endif

#endif
