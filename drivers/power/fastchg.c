/*
 * Author: Chad Froebel <chadfroebel@gmail.com>
 *
 * Simple port to Nexus 4 : motley <motley.slate@gmail.com>
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

/*
 * Possible values for "force_fast_charge" are :
 *
 *   0 - disabled (default)
 *   1 - substitute AC to USB unconditional
*/

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fastchg.h>

static struct raw_notifier_head notifier_header;
int force_fast_charge = FAST_CHARGE_DISABLED;

/**
 * force_fast_charge_register_notifier() - Register a notifee to get notified
 *			      when force_fast_charge is changed
 * @nb:		a notifier block to be registered.
 */
int force_fast_charge_register_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&notifier_header, nb);
}
EXPORT_SYMBOL_GPL(force_fast_charge_register_notifier);

/**
 * force_fast_charge_unregister_notifier() - Unregister a notifee
 * @nb:		a registered notifier block to be unregistered.
 */
int force_fast_charge_unregister_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&notifier_header, nb);
}
EXPORT_SYMBOL_GPL(force_fast_charge_unregister_notifier);

/* sysfs interface for "force_fast_charge" */
static ssize_t force_fast_charge_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", force_fast_charge);
}

static ssize_t force_fast_charge_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	int new_force_fast_charge;

	sscanf(buf, "%du", &new_force_fast_charge);

	if (new_force_fast_charge >= FAST_CHARGE_DISABLED && new_force_fast_charge <= FAST_CHARGE_FORCE_AC) {
		/* update only if valid value provided */
		if (force_fast_charge != new_force_fast_charge) {
			force_fast_charge = new_force_fast_charge;
			raw_notifier_call_chain(&notifier_header, force_fast_charge, NULL);
		}
	}

	return count;
}

static struct kobj_attribute force_fast_charge_attribute =
	__ATTR(force_fast_charge, 0666, force_fast_charge_show, force_fast_charge_store);

	static struct attribute *force_fast_charge_attrs[] = {
	&force_fast_charge_attribute.attr,
	NULL,
};

static struct attribute_group force_fast_charge_attr_group = {
	.attrs = force_fast_charge_attrs,
};

/* Initialize fast charge sysfs folder */
static struct kobject *force_fast_charge_kobj;

int force_fast_charge_init(void)
{
	int force_fast_charge_retval;

	force_fast_charge = FAST_CHARGE_DISABLED; /* Forced fast charge disabled by default */

	force_fast_charge_kobj = kobject_create_and_add("fast_charge", kernel_kobj);
	if (!force_fast_charge_kobj) {
			return -ENOMEM;
	}

	force_fast_charge_retval = sysfs_create_group(force_fast_charge_kobj, &force_fast_charge_attr_group);

	if (force_fast_charge_retval)
		kobject_put(force_fast_charge_kobj);

	if (force_fast_charge_retval)
		kobject_put(force_fast_charge_kobj);

	RAW_INIT_NOTIFIER_HEAD(&notifier_header);
	return (force_fast_charge_retval);
}

void force_fast_charge_exit(void)
{
	kobject_put(force_fast_charge_kobj);
}

module_init(force_fast_charge_init);
module_exit(force_fast_charge_exit);

