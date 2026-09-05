// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LED Dynamic Lighting Class Interface
 *
 * Copyright (C) 2026 Open Gaming Collective
 * Author: Marco Scardovi <scardracs@disroot.org>
 * Author: Denis Benato <denis.benato@linux.dev>
 */

#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/hex.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/led-dynamic-lighting.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

static const char * const dl_zone_type_names[] = {
	[DL_ZONE_TYPE_GENERIC]		= "generic",
	[DL_ZONE_TYPE_KEYBOARD_PER_KEY]	= "keyboard_per_key",
	[DL_ZONE_TYPE_MATRIX_2D]	= "matrix_2d",
	[DL_ZONE_TYPE_SEGMENT_STRIP]	= "segment_strip",
	[DL_ZONE_TYPE_LOGO]		= "logo",
	[DL_ZONE_TYPE_LIGHTBAR]		= "lightbar",
};

static const char * const dl_effect_names[] = {
	[DL_EFFECT_OFF]			= "off",
	[DL_EFFECT_STATIC]		= "static",
	[DL_EFFECT_BREATHING]		= "breathing",
	[DL_EFFECT_STROBE]		= "strobe",
	[DL_EFFECT_SPECTRUM_CYCLE]	= "spectrum_cycle",
	[DL_EFFECT_RAINBOW]		= "rainbow",
	[DL_EFFECT_DIRECT]		= "direct",
	[DL_EFFECT_CUSTOM]		= "custom",
};

static const char * const dl_direction_names[] = {
	[DL_DIRECTION_LEFT]			= "left",
	[DL_DIRECTION_RIGHT]			= "right",
	[DL_DIRECTION_UP]			= "up",
	[DL_DIRECTION_DOWN]			= "down",
	[DL_DIRECTION_CLOCKWISE]		= "clockwise",
	[DL_DIRECTION_COUNTER_CLOCKWISE]	= "counter_clockwise",
};

static const char * const dl_power_state_names[] = {
	"boot",
	"awake",
	"sleep",
	"shutdown",
};

static ssize_t zone_type_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	guard(mutex)(&ldev->lock);

	if (ldev->zone_type >= ARRAY_SIZE(dl_zone_type_names) ||
	    !dl_zone_type_names[ldev->zone_type])
		return sysfs_emit(buf, "unknown\n");

	return sysfs_emit(buf, "%s\n", dl_zone_type_names[ldev->zone_type]);
}
static DEVICE_ATTR_RO(zone_type);

static ssize_t led_count_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	guard(mutex)(&ldev->lock);

	return sysfs_emit(buf, "%u\n", ldev->led_count);
}
static DEVICE_ATTR_RO(led_count);

static ssize_t matrix_dimensions_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	guard(mutex)(&ldev->lock);

	return sysfs_emit(buf, "%u %u\n", ldev->matrix_width, ldev->matrix_height);
}
static DEVICE_ATTR_RO(matrix_dimensions);

static ssize_t effects_supported_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	int len = 0;
	int i;

	guard(mutex)(&ldev->lock);

	for (i = 0; i < ARRAY_SIZE(dl_effect_names); i++) {
		if (dl_effect_names[i] && (ldev->supported_effects & BIT(i)))
			len += sysfs_emit_at(buf, len, "%s ", dl_effect_names[i]);
	}

	if (len > 0)
		buf[len - 1] = '\n';
	else
		len = sysfs_emit(buf, "\n");

	return len;
}
static DEVICE_ATTR_RO(effects_supported);

static ssize_t effect_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	guard(mutex)(&ldev->lock);

	if (ldev->current_effect >= ARRAY_SIZE(dl_effect_names) ||
	    !dl_effect_names[ldev->current_effect])
		return sysfs_emit(buf, "unknown\n");

	return sysfs_emit(buf, "%s\n", dl_effect_names[ldev->current_effect]);
}

static ssize_t effect_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	int match, ret;

	if (!ldev->ops->set_effect)
		return -EOPNOTSUPP;

	match = sysfs_match_string(dl_effect_names, buf);
	if (match < 0 || !(ldev->supported_effects & BIT(match)))
		return -EINVAL;

	guard(mutex)(&cdev->led_access);
	led_trigger_remove(cdev);
	guard(mutex)(&ldev->lock);

	ret = ldev->ops->set_effect(ldev, match);
	if (ret < 0)
		return ret;

	ldev->current_effect = match;
	return count;
}
static DEVICE_ATTR_RW(effect);

static ssize_t effect_speed_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	guard(mutex)(&ldev->lock);

	return sysfs_emit(buf, "%u\n", ldev->speed);
}

static ssize_t effect_speed_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	unsigned int speed;
	int ret;

	if (!ldev->ops->set_speed || !ldev->max_speed)
		return -EOPNOTSUPP;

	ret = kstrtouint(buf, 10, &speed);
	if (ret)
		return ret;

	if (speed > ldev->max_speed)
		return -EINVAL;

	guard(mutex)(&cdev->led_access);
	guard(mutex)(&ldev->lock);

	ret = ldev->ops->set_speed(ldev, speed);
	if (ret < 0)
		return ret;

	ldev->speed = speed;
	return count;
}
static DEVICE_ATTR_RW(effect_speed);

static ssize_t effect_direction_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	guard(mutex)(&ldev->lock);

	if (ldev->direction >= ARRAY_SIZE(dl_direction_names) ||
	    !dl_direction_names[ldev->direction])
		return sysfs_emit(buf, "unknown\n");

	return sysfs_emit(buf, "%s\n", dl_direction_names[ldev->direction]);
}

static ssize_t effect_direction_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	int match, ret;

	if (!ldev->ops->set_direction || !ldev->supported_directions)
		return -EOPNOTSUPP;

	match = sysfs_match_string(dl_direction_names, buf);
	if (match < 0 || !(ldev->supported_directions & BIT(match)))
		return -EINVAL;

	guard(mutex)(&cdev->led_access);
	guard(mutex)(&ldev->lock);

	ret = ldev->ops->set_direction(ldev, match);
	if (ret < 0)
		return ret;

	ldev->direction = match;
	return count;
}
static DEVICE_ATTR_RW(effect_direction);

static ssize_t effects_palette_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	int len = 0;
	unsigned int i;

	guard(mutex)(&ldev->lock);

	for (i = 0; i < ldev->num_palette_entries; i++) {
		len += sysfs_emit_at(buf, len, "#%02x%02x%02x%c",
				     ldev->palette[i].r,
				     ldev->palette[i].g,
				     ldev->palette[i].b,
				     (i == ldev->num_palette_entries - 1) ? '\n' : ' ');
	}

	if (!len)
		len = sysfs_emit(buf, "\n");

	return len;
}

static ssize_t effects_palette_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	const char *cur = buf;
	unsigned int num_parsed = 0;
	int ret;

	if (!ldev->ops->set_palette || !ldev->max_palette_entries)
		return -EOPNOTSUPP;

	struct dl_rgb *temp_palette __free(kfree) = kmalloc_array(ldev->max_palette_entries,
								  sizeof(*temp_palette),
								  GFP_KERNEL);
	if (!temp_palette)
		return -ENOMEM;

	while (*cur) {
		cur = skip_spaces(cur);
		if (!*cur)
			break;

		if (num_parsed >= ldev->max_palette_entries)
			return -EINVAL;

		if (*cur != '#')
			return -EINVAL;
		cur++;

		if (hex2bin((u8 *)&temp_palette[num_parsed], cur, 3) < 0)
			return -EINVAL;
		cur += 6;
		num_parsed++;
	}

	if (!num_parsed)
		return -EINVAL;

	guard(mutex)(&cdev->led_access);
	led_trigger_remove(cdev);
	guard(mutex)(&ldev->lock);

	ret = ldev->ops->set_palette(ldev, temp_palette, num_parsed);
	if (ret < 0)
		return ret;

	memcpy(ldev->palette, temp_palette, num_parsed * sizeof(*temp_palette));
	ldev->num_palette_entries = num_parsed;

	return count;
}
static DEVICE_ATTR_RW(effects_palette);

static ssize_t power_states_supported_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	int len = 0;
	int i;

	guard(mutex)(&ldev->lock);

	for (i = 0; i < ARRAY_SIZE(dl_power_state_names); i++) {
		if (ldev->supported_power_states & BIT(i))
			len += sysfs_emit_at(buf, len, "%s ", dl_power_state_names[i]);
	}

	if (len > 0)
		buf[len - 1] = '\n';
	else
		len = sysfs_emit(buf, "\n");

	return len;
}
static DEVICE_ATTR_RO(power_states_supported);

static ssize_t power_states_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	int len = 0;
	int i;

	guard(mutex)(&ldev->lock);

	for (i = 0; i < ARRAY_SIZE(dl_power_state_names); i++) {
		if (ldev->active_power_states & BIT(i))
			len += sysfs_emit_at(buf, len, "%s ", dl_power_state_names[i]);
	}

	if (len > 0)
		buf[len - 1] = '\n';
	else
		len = sysfs_emit(buf, "\n");

	return len;
}

static ssize_t power_states_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	char state_name[16];
	const char *cur = buf;
	u32 target_states = 0;
	int ret, match;

	if (!ldev->ops->set_power_states || !ldev->supported_power_states)
		return -EOPNOTSUPP;

	while (*cur) {
		cur = skip_spaces(cur);
		if (!*cur)
			break;

		if (sscanf(cur, "%15s", state_name) != 1)
			return -EINVAL;
		cur += strlen(state_name);

		match = sysfs_match_string(dl_power_state_names, state_name);
		if (match < 0 || !(ldev->supported_power_states & BIT(match)))
			return -EINVAL;

		target_states |= BIT(match);
	}

	guard(mutex)(&cdev->led_access);
	guard(mutex)(&ldev->lock);

	ret = ldev->ops->set_power_states(ldev, target_states);
	if (ret < 0)
		return ret;

	ldev->active_power_states = target_states;
	return count;
}
static DEVICE_ATTR_RW(power_states);

static ssize_t direct_buffer_write(struct file *filp, struct kobject *kobj,
				   const struct bin_attribute *bin_attr,
				   char *buf, loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	size_t expected_size;
	int ret;

	if (!ldev->ops->direct_write)
		return -EOPNOTSUPP;

	expected_size = ldev->led_count * 3;
	if (off != 0 || count != expected_size)
		return -EINVAL;

	guard(mutex)(&cdev->led_access);
	led_trigger_remove(cdev);
	guard(mutex)(&ldev->lock);

	ret = ldev->ops->direct_write(ldev, buf, count);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t frame_write(struct file *filp, struct kobject *kobj,
			   const struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);
	int ret;

	if (!ldev->ops->frame_write)
		return -EOPNOTSUPP;

	if (!count)
		return -EINVAL;

	guard(mutex)(&cdev->led_access);
	led_trigger_remove(cdev);
	guard(mutex)(&ldev->lock);

	ret = ldev->ops->frame_write(ldev, buf, count);
	if (ret < 0)
		return ret;

	return count;
}

static umode_t dl_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	if (attr == &dev_attr_matrix_dimensions.attr) {
		if (ldev->zone_type != DL_ZONE_TYPE_MATRIX_2D)
			return 0;
	}

	if (attr == &dev_attr_power_states_supported.attr ||
	    attr == &dev_attr_power_states.attr) {
		if (!ldev->supported_power_states || !ldev->ops->set_power_states)
			return 0;
	}

	if (attr == &dev_attr_effect_speed.attr) {
		if (!ldev->max_speed || !ldev->ops->set_speed)
			return 0;
	}

	if (attr == &dev_attr_effect_direction.attr) {
		if (!ldev->supported_directions || !ldev->ops->set_direction)
			return 0;
	}

	if (attr == &dev_attr_effects_palette.attr) {
		if (!ldev->max_palette_entries || !ldev->ops->set_palette)
			return 0;
	}

	if (attr == &dev_attr_effect.attr || attr == &dev_attr_effects_supported.attr) {
		if (!ldev->supported_effects || !ldev->ops->set_effect)
			return 0;
	}

	return attr->mode;
}

static umode_t dl_bin_attr_is_visible(struct kobject *kobj,
				      const struct bin_attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	if (attr == &ldev->bin_attr_direct) {
		if (!ldev->ops->direct_write || !ldev->led_count)
			return 0;
	}

	if (attr == &ldev->bin_attr_frame) {
		if (!ldev->ops->frame_write)
			return 0;
	}

	return attr->attr.mode;
}

static struct attribute *led_dynamic_attrs[] = {
	&dev_attr_zone_type.attr,
	&dev_attr_led_count.attr,
	&dev_attr_matrix_dimensions.attr,
	&dev_attr_effects_supported.attr,
	&dev_attr_effect.attr,
	&dev_attr_effect_speed.attr,
	&dev_attr_effect_direction.attr,
	&dev_attr_effects_palette.attr,
	&dev_attr_power_states_supported.attr,
	&dev_attr_power_states.attr,
	NULL,
};

int led_classdev_dynamic_register_ext(struct device *parent,
				      struct led_classdev_dynamic *ldev,
				      struct led_init_data *init_data)
{
	struct led_classdev *cdev;
	unsigned int num_driver_groups = 0;
	int ret;

	if (!ldev || !ldev->ops)
		return -EINVAL;

	mutex_init(&ldev->lock);
	cdev = &ldev->cdev;
	cdev->flags |= LED_DYNAMIC_LIGHTING;

	sysfs_bin_attr_init(&ldev->bin_attr_direct);
	ldev->bin_attr_direct.attr.name = "direct_buffer";
	ldev->bin_attr_direct.attr.mode = 0200;
	ldev->bin_attr_direct.write = direct_buffer_write;
	ldev->bin_attr_direct.size = ldev->led_count * 3;

	sysfs_bin_attr_init(&ldev->bin_attr_frame);
	ldev->bin_attr_frame.attr.name = "frame";
	ldev->bin_attr_frame.attr.mode = 0200;
	ldev->bin_attr_frame.write = frame_write;
	ldev->bin_attr_frame.size = 0;

	ldev->bin_attrs[0] = &ldev->bin_attr_direct;
	ldev->bin_attrs[1] = &ldev->bin_attr_frame;
	ldev->bin_attrs[2] = NULL;

	ldev->group.attrs = led_dynamic_attrs;
	ldev->group.bin_attrs = ldev->bin_attrs;
	ldev->group.is_visible = dl_attr_is_visible;
	ldev->group.is_bin_visible = dl_bin_attr_is_visible;

	ldev->groups[0] = &ldev->group;
	ldev->groups[1] = NULL;
	ldev->driver_groups = cdev->groups;
	ldev->merged_groups = NULL;
	ldev->palette_allocated = false;

	while (cdev->groups && cdev->groups[num_driver_groups])
		num_driver_groups++;

	if (num_driver_groups) {
		unsigned int i;

		ldev->merged_groups = kcalloc(num_driver_groups + 2,
					      sizeof(*ldev->merged_groups),
					      GFP_KERNEL);
		if (!ldev->merged_groups) {
			mutex_destroy(&ldev->lock);
			return -ENOMEM;
		}

		for (i = 0; i < num_driver_groups; i++)
			ldev->merged_groups[i] = cdev->groups[i];
		ldev->merged_groups[num_driver_groups] = &ldev->group;
		ldev->merged_groups[num_driver_groups + 1] = NULL;
		cdev->groups = ldev->merged_groups;
	} else {
		cdev->groups = ldev->groups;
	}

	if (ldev->max_palette_entries > 0 && !ldev->palette) {
		ldev->palette = kcalloc(ldev->max_palette_entries,
				       sizeof(struct dl_rgb), GFP_KERNEL);
		if (!ldev->palette) {
			kfree(ldev->merged_groups);
			ldev->merged_groups = NULL;
			cdev->groups = ldev->driver_groups;
			mutex_destroy(&ldev->lock);
			return -ENOMEM;
		}
		ldev->palette_allocated = true;
	}

	ret = led_classdev_register_ext(parent, cdev, init_data);
	if (ret) {
		if (ldev->palette_allocated) {
			kfree(ldev->palette);
			ldev->palette = NULL;
			ldev->palette_allocated = false;
		}
		kfree(ldev->merged_groups);
		ldev->merged_groups = NULL;
		cdev->groups = ldev->driver_groups;
		mutex_destroy(&ldev->lock);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(led_classdev_dynamic_register_ext);

void led_classdev_dynamic_unregister(struct led_classdev_dynamic *ldev)
{
	if (!ldev)
		return;

	led_classdev_unregister(&ldev->cdev);
	ldev->cdev.groups = ldev->driver_groups;
	if (ldev->palette_allocated) {
		kfree(ldev->palette);
		ldev->palette = NULL;
		ldev->palette_allocated = false;
	}
	kfree(ldev->merged_groups);
	ldev->merged_groups = NULL;
	mutex_destroy(&ldev->lock);
}
EXPORT_SYMBOL_GPL(led_classdev_dynamic_unregister);

static void devm_led_classdev_dynamic_release(struct device *dev, void *res)
{
	led_classdev_dynamic_unregister(*(struct led_classdev_dynamic **)res);
}

int devm_led_classdev_dynamic_register_ext(struct device *parent,
					   struct led_classdev_dynamic *ldev,
					   struct led_init_data *init_data)
{
	struct led_classdev_dynamic **dr;
	int ret;

	dr = devres_alloc(devm_led_classdev_dynamic_release,
			  sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = led_classdev_dynamic_register_ext(parent, ldev, init_data);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	*dr = ldev;
	devres_add(parent, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_led_classdev_dynamic_register_ext);

static int devm_led_classdev_dynamic_match(struct device *dev,
					   void *res, void *data)
{
	struct led_classdev_dynamic **p = res;

	if (WARN_ON(!p || !*p))
		return 0;

	return *p == data;
}

void devm_led_classdev_dynamic_unregister(struct device *dev,
					  struct led_classdev_dynamic *ldev)
{
	WARN_ON(devres_release(dev,
			       devm_led_classdev_dynamic_release,
			       devm_led_classdev_dynamic_match, ldev));
}
EXPORT_SYMBOL_GPL(devm_led_classdev_dynamic_unregister);

MODULE_AUTHOR("Marco Scardovi <scardracs@disroot.org>");
MODULE_AUTHOR("Denis Benato <denis.benato@linux.dev>");
MODULE_DESCRIPTION("LED Dynamic Lighting Class Interface");
MODULE_LICENSE("GPL");
