// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for the Dynamic Lighting LED class.
 *
 * Copyright (C) 2026 Open Gaming Collective
 * Author: Marco Scardovi <scardracs@disroot.org>
 * Author: Denis Benato <denis.benato@linux.dev>
 */

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/device.h>
#include <linux/kernfs.h>
#include <linux/led-dynamic-lighting.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

static const char * const dl_test_effects[] = {
	"static",
	"breathe",
	"direct",
};

static const char * const dl_test_pool[] = {
	"static",
	"breathe",
	"wave",
	"pulse",
};

struct dl_test_priv {
	struct led_classdev_dynamic ldev;
	struct device *dev;
	int fail_op;
	unsigned int last_effect;
	unsigned int last_speed;
	enum dl_direction last_direction;
	bool last_enabled;
	u32 last_power_states;
	size_t last_direct_len;
};

static int dl_test_set_effect(struct led_classdev_dynamic *ldev, unsigned int index)
{
	struct dl_test_priv *priv = ldev->driver_data;

	if (priv && priv->fail_op)
		return -EIO;

	if (priv)
		priv->last_effect = index;
	return 0;
}

static int dl_test_set_palette(struct led_classdev_dynamic *ldev,
			       const struct dl_rgb *palette,
			       unsigned int num_entries)
{
	struct dl_test_priv *priv = ldev->driver_data;

	if (priv && priv->fail_op)
		return -EIO;
	return 0;
}

static int dl_test_set_speed(struct led_classdev_dynamic *ldev, unsigned int speed)
{
	struct dl_test_priv *priv = ldev->driver_data;

	if (priv && priv->fail_op)
		return -EIO;
	if (priv)
		priv->last_speed = speed;
	return 0;
}

static int dl_test_set_direction(struct led_classdev_dynamic *ldev,
				 enum dl_direction direction)
{
	struct dl_test_priv *priv = ldev->driver_data;

	if (priv && priv->fail_op)
		return -EIO;
	if (priv)
		priv->last_direction = direction;
	return 0;
}

static int dl_test_set_enabled(struct led_classdev_dynamic *ldev, bool enabled)
{
	struct dl_test_priv *priv = ldev->driver_data;

	if (priv && priv->fail_op)
		return -EIO;
	if (priv)
		priv->last_enabled = enabled;
	return 0;
}

static int dl_test_set_power_states(struct led_classdev_dynamic *ldev,
				    u32 active_states)
{
	struct dl_test_priv *priv = ldev->driver_data;

	if (priv && priv->fail_op)
		return -EIO;
	if (priv)
		priv->last_power_states = active_states;
	return 0;
}

static int dl_test_direct_write(struct led_classdev_dynamic *ldev,
				const u8 *buffer, size_t size)
{
	struct dl_test_priv *priv = ldev->driver_data;

	if (priv && priv->fail_op)
		return -EIO;
	if (priv)
		priv->last_direct_len = size;
	return 0;
}

static const struct led_dynamic_ops dl_test_ops = {
	.set_effect = dl_test_set_effect,
	.set_palette = dl_test_set_palette,
};

static const struct led_dynamic_ops dl_test_full_ops = {
	.set_effect = dl_test_set_effect,
	.set_palette = dl_test_set_palette,
	.set_speed = dl_test_set_speed,
	.set_direction = dl_test_set_direction,
	.set_enabled = dl_test_set_enabled,
	.set_power_states = dl_test_set_power_states,
	.direct_write = dl_test_direct_write,
};

static bool dl_test_attr_visible(struct dl_test_priv *priv, const char *attr)
{
	struct kernfs_node *kn;
	struct led_classdev *cdev = led_dynamic_cdev(&priv->ldev);

	kn = kernfs_find_and_get(cdev->dev->kobj.sd, attr);
	if (!kn)
		return false;

	kernfs_put(kn);
	return true;
}

/*
 * Resolve the device_attribute behind a sysfs file through kernfs, so the
 * show/store callbacks can be exercised without a mounted sysfs. sysfs
 * stores the struct attribute pointer in kernfs_node->priv.
 */
static struct device_attribute *dl_test_attr(struct kunit *test,
					     struct dl_test_priv *priv,
					     const char *name)
{
	struct kernfs_node *kn;
	struct attribute *attr;
	struct led_classdev *cdev = led_dynamic_cdev(&priv->ldev);

	kn = kernfs_find_and_get(cdev->dev->kobj.sd, name);
	KUNIT_ASSERT_NOT_NULL(test, kn);

	attr = kn->priv;
	kernfs_put(kn);
	KUNIT_ASSERT_NOT_NULL(test, attr);

	return container_of(attr, struct device_attribute, attr);
}

static int dl_test_write(struct kunit *test, struct dl_test_priv *priv,
			 const char *name, const char *text)
{
	struct device_attribute *attr = dl_test_attr(test, priv, name);
	struct led_classdev *cdev = led_dynamic_cdev(&priv->ldev);
	ssize_t n;

	KUNIT_ASSERT_NOT_NULL(test, attr->store);

	n = attr->store(cdev->dev, attr, text, strlen(text));
	if (n < 0)
		return n;

	return 0;
}

static void dl_test_read(struct kunit *test, struct dl_test_priv *priv,
			 const char *name, char *buf)
{
	struct device_attribute *attr = dl_test_attr(test, priv, name);
	struct led_classdev *cdev = led_dynamic_cdev(&priv->ldev);
	ssize_t n;

	KUNIT_ASSERT_NOT_NULL(test, attr->show);

	n = attr->show(cdev->dev, attr, buf);
	KUNIT_ASSERT_GE(test, n, 0);
	buf[n] = '\0';
}

static void dl_test_unregister(struct dl_test_priv *priv)
{
	if (!priv)
		return;

	led_classdev_dynamic_unregister(&priv->ldev);
	if (priv->dev) {
		put_device(priv->dev);
		priv->dev = NULL;
	}
}

static int dl_test_register_basic(struct kunit *test, struct dl_test_priv *priv)
{
	struct device *dev;
	int ret;

	dev = kunit_device_register(test, "dl_test");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	priv->dev = get_device(dev);
	priv->ldev.cdev.name = "dl-test";
	priv->ldev.ops = &dl_test_ops;
	priv->ldev.zone_type = "keyboard";
	priv->ldev.effects = dl_test_effects;
	priv->ldev.num_effects = 2; /* static, breathe */
	priv->ldev.max_palette_entries = 2;
	priv->ldev.speed_min = 0;
	priv->ldev.speed_max = 2;
	priv->ldev.driver_data = priv;

	ret = led_classdev_dynamic_register(priv->dev, &priv->ldev);
	if (ret) {
		put_device(priv->dev);
		priv->dev = NULL;
		return ret;
	}

	return 0;
}

static int dl_test_register_full(struct kunit *test, struct dl_test_priv *priv,
				 unsigned int led_count,
				 unsigned int sample_size)
{
	struct device *dev;
	int ret;

	dev = kunit_device_register(test, "dl_full");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	priv->dev = get_device(dev);
	priv->ldev.cdev.name = "dl-full";
	priv->ldev.ops = &dl_test_full_ops;
	priv->ldev.zone_type = "generic";
	priv->ldev.led_count = led_count;
	priv->ldev.direct_sample_size = sample_size;
	priv->ldev.effects = dl_test_effects;
	priv->ldev.num_effects = ARRAY_SIZE(dl_test_effects);
	priv->ldev.max_palette_entries = 2;
	priv->ldev.speed_min = 1;
	priv->ldev.speed_max = 3;
	priv->ldev.speed = 2;
	priv->ldev.supported_directions = BIT(DL_DIRECTION_LEFT) |
					  BIT(DL_DIRECTION_RIGHT);
	priv->ldev.direction = DL_DIRECTION_LEFT;
	priv->ldev.enabled = true;
	priv->ldev.supported_power_states = DL_POWER_STATE_ALL;
	priv->ldev.active_power_states = DL_POWER_STATE_AWAKE;
	priv->ldev.driver_data = priv;

	ret = led_classdev_dynamic_register(priv->dev, &priv->ldev);
	if (ret) {
		put_device(priv->dev);
		priv->dev = NULL;
		return ret;
	}

	return 0;
}

static void dl_test_unknown_effect_rejected(struct kunit *test)
{
	struct dl_test_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	KUNIT_ASSERT_EQ(test, dl_test_register_basic(test, priv), 0);

	KUNIT_EXPECT_TRUE(test, dl_test_attr_visible(priv, "effect"));
	KUNIT_EXPECT_TRUE(test, dl_test_attr_visible(priv, "effect_index"));
	KUNIT_EXPECT_FALSE(test, dl_test_attr_visible(priv, "speed"));
	KUNIT_EXPECT_FALSE(test, dl_test_attr_visible(priv, "enabled"));
	KUNIT_EXPECT_FALSE(test, dl_test_attr_visible(priv, "direct_buffer"));

	ret = dl_test_write(test, priv, "effect", "no-such-effect");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	dl_test_unregister(priv);
}

static void dl_test_effect_and_palette(struct kunit *test)
{
	struct dl_test_priv *priv;
	char *buf;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	KUNIT_ASSERT_EQ(test, dl_test_register_basic(test, priv), 0);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	ret = dl_test_write(test, priv, "effect", "breathe");
	KUNIT_ASSERT_EQ(test, ret, 0);
	dl_test_read(test, priv, "effect", buf);
	KUNIT_EXPECT_STREQ(test, buf, "breathe\n");

	ret = dl_test_write(test, priv, "effects_palette", "#ff0000 #00ff00");
	KUNIT_ASSERT_EQ(test, ret, 0);
	dl_test_read(test, priv, "effects_palette", buf);
	KUNIT_EXPECT_STREQ(test, buf, "#ff0000 #00ff00\n");

	ret = dl_test_write(test, priv, "effects_palette",
			    "#ff0000 #00ff00 #0000ff");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = dl_test_write(test, priv, "effects_palette", "ff0000");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	dl_test_unregister(priv);
}

static void dl_test_fill_effects_overflow(struct kunit *test)
{
	const char *dst[2];
	int n;

	n = led_dynamic_fill_effects(dst, ARRAY_SIZE(dst), dl_test_pool,
				     ARRAY_SIZE(dl_test_pool),
				     BIT(0) | BIT(1) | BIT(2));
	KUNIT_EXPECT_EQ(test, n, -EINVAL);

	n = led_dynamic_fill_effects(dst, ARRAY_SIZE(dst), dl_test_pool,
				     ARRAY_SIZE(dl_test_pool),
				     BIT(0) | BIT(1));
	KUNIT_ASSERT_EQ(test, n, 2);
	KUNIT_EXPECT_STREQ(test, dst[0], "static");
	KUNIT_EXPECT_STREQ(test, dst[1], "breathe");
}

static void dl_test_zone_type_unknown(struct kunit *test)
{
	struct dl_test_priv *priv;
	char *buf;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	priv->ldev.cdev.name = "dl-zone";
	priv->ldev.ops = &dl_test_ops;
	priv->ldev.effects = dl_test_effects;
	priv->ldev.num_effects = 2;
	priv->ldev.driver_data = priv;

	{
		struct device *dev = kunit_device_register(test, "dl_zone");

		KUNIT_ASSERT_FALSE(test, IS_ERR(dev));
		priv->dev = get_device(dev);
	}

	ret = led_classdev_dynamic_register(priv->dev, &priv->ldev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);
	dl_test_read(test, priv, "zone_type", buf);
	KUNIT_EXPECT_STREQ(test, buf, "unknown\n");

	dl_test_unregister(priv);
}

static void dl_test_reregister_rejected(struct kunit *test)
{
	struct dl_test_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	KUNIT_ASSERT_EQ(test, dl_test_register_basic(test, priv), 0);

	ret = led_classdev_dynamic_register(priv->dev, &priv->ldev);
	KUNIT_EXPECT_EQ(test, ret, -EBUSY);

	dl_test_unregister(priv);
}

static void dl_test_host_rejects_parent(struct kunit *test)
{
	struct dl_test_priv *priv;
	struct led_classdev host = { };
	struct device *dev;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	dev = kunit_device_register(test, "dl_host_parent");
	KUNIT_ASSERT_FALSE(test, IS_ERR(dev));

	host.name = "dl-host-led";
	ret = led_classdev_register(dev, &host);
	KUNIT_ASSERT_EQ(test, ret, 0);

	priv->ldev.host = &host;
	priv->ldev.ops = &dl_test_ops;
	priv->ldev.effects = dl_test_effects;
	priv->ldev.num_effects = 2;

	ret = led_classdev_dynamic_register(dev, &priv->ldev);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	led_classdev_unregister(&host);
}

static void dl_test_host_attach(struct kunit *test)
{
	struct dl_test_priv *priv;
	struct led_classdev host = { };
	struct device *dev;
	char *buf;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	dev = kunit_device_register(test, "dl_host");
	KUNIT_ASSERT_FALSE(test, IS_ERR(dev));

	host.name = "dl-host-led";
	ret = led_classdev_register(dev, &host);
	KUNIT_ASSERT_EQ(test, ret, 0);

	priv->dev = get_device(dev);
	priv->ldev.host = &host;
	priv->ldev.ops = &dl_test_ops;
	priv->ldev.zone_type = "generic";
	priv->ldev.effects = dl_test_effects;
	priv->ldev.num_effects = 2;
	priv->ldev.max_palette_entries = 2;
	priv->ldev.driver_data = priv;

	ret = led_classdev_dynamic_register(NULL, &priv->ldev);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, priv->ldev.attached);
	KUNIT_EXPECT_PTR_EQ(test, host.led_dynamic, &priv->ldev);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	ret = dl_test_write(test, priv, "effect", "breathe");
	KUNIT_ASSERT_EQ(test, ret, 0);
	dl_test_read(test, priv, "effect", buf);
	KUNIT_EXPECT_STREQ(test, buf, "breathe\n");

	led_classdev_dynamic_unregister(&priv->ldev);
	KUNIT_EXPECT_NULL(test, host.led_dynamic);
	led_classdev_unregister(&host);
	put_device(priv->dev);
}

static void dl_test_enabled_and_power_states(struct kunit *test)
{
	struct dl_test_priv *priv;
	char *buf;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	KUNIT_ASSERT_EQ(test, dl_test_register_full(test, priv, 2, 0), 0);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	KUNIT_EXPECT_TRUE(test, dl_test_attr_visible(priv, "enabled"));
	KUNIT_EXPECT_TRUE(test, dl_test_attr_visible(priv, "power_states"));

	ret = dl_test_write(test, priv, "enabled", "false");
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, priv->ldev.enabled);
	dl_test_read(test, priv, "enabled", buf);
	KUNIT_EXPECT_STREQ(test, buf, "false\n");

	ret = dl_test_write(test, priv, "power_states", "boot awake");
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->ldev.active_power_states,
			DL_POWER_STATE_BOOT | DL_POWER_STATE_AWAKE);

	ret = dl_test_write(test, priv, "power_states", "\n");
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->ldev.active_power_states, 0);

	ret = dl_test_write(test, priv, "power_states", "not-a-state");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	dl_test_unregister(priv);
}

static void dl_test_speed_direction_bounds(struct kunit *test)
{
	struct dl_test_priv *priv;
	char *buf;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	KUNIT_ASSERT_EQ(test, dl_test_register_full(test, priv, 2, 0), 0);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	ret = dl_test_write(test, priv, "speed", "0");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = dl_test_write(test, priv, "speed", "4");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = dl_test_write(test, priv, "speed", "3");
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->ldev.speed, 3);

	ret = dl_test_write(test, priv, "direction", "up");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = dl_test_write(test, priv, "direction", "right");
	KUNIT_ASSERT_EQ(test, ret, 0);
	dl_test_read(test, priv, "direction", buf);
	KUNIT_EXPECT_STREQ(test, buf, "right\n");

	dl_test_unregister(priv);
}

static void dl_test_set_ops_failure_preserves_state(struct kunit *test)
{
	struct dl_test_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	KUNIT_ASSERT_EQ(test, dl_test_register_full(test, priv, 2, 0), 0);

	ret = dl_test_write(test, priv, "effect", "breathe");
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->ldev.current_effect, 1);

	priv->fail_op = 1;
	ret = dl_test_write(test, priv, "effect", "static");
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, priv->ldev.current_effect, 1);

	ret = dl_test_write(test, priv, "speed", "1");
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, priv->ldev.speed, 2);

	ret = dl_test_write(test, priv, "enabled", "false");
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_TRUE(test, priv->ldev.enabled);

	ret = dl_test_write(test, priv, "power_states", "boot");
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, priv->ldev.active_power_states, DL_POWER_STATE_AWAKE);

	dl_test_unregister(priv);
}

static void dl_test_direct_buffer_size_and_effect(struct kunit *test)
{
	struct dl_test_priv *priv;
	struct bin_attribute *bin;
	struct kernfs_node *kn;
	struct led_classdev *cdev;
	u8 frame[6];
	u8 mono[4] = { 1, 2, 3, 4 };
	char *buf;
	ssize_t n;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	KUNIT_ASSERT_EQ(test, dl_test_register_full(test, priv, 2, 0), 0);

	cdev = led_dynamic_cdev(&priv->ldev);
	kn = kernfs_find_and_get(cdev->dev->kobj.sd, "direct_buffer");
	KUNIT_ASSERT_NOT_NULL(test, kn);
	bin = kn->priv;
	kernfs_put(kn);
	KUNIT_ASSERT_NOT_NULL(test, bin);
	KUNIT_ASSERT_NOT_NULL(test, bin->write);

	memset(frame, 0x11, sizeof(frame));
	n = bin->write(NULL, &cdev->dev->kobj, bin, frame, 0, 3);
	KUNIT_EXPECT_EQ(test, n, -EINVAL);

	n = bin->write(NULL, &cdev->dev->kobj, bin, frame, 0, sizeof(frame));
	KUNIT_ASSERT_EQ(test, n, sizeof(frame));
	KUNIT_EXPECT_EQ(test, priv->last_direct_len, sizeof(frame));

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);
	dl_test_read(test, priv, "effect", buf);
	KUNIT_EXPECT_STREQ(test, buf, "direct\n");

	priv->fail_op = 1;
	priv->ldev.current_effect = 0;
	n = bin->write(NULL, &cdev->dev->kobj, bin, frame, 0, sizeof(frame));
	KUNIT_EXPECT_EQ(test, n, -EIO);
	KUNIT_EXPECT_EQ(test, priv->ldev.current_effect, 0);

	dl_test_unregister(priv);

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	KUNIT_ASSERT_EQ(test, dl_test_register_full(test, priv, 4, 1), 0);

	cdev = led_dynamic_cdev(&priv->ldev);
	kn = kernfs_find_and_get(cdev->dev->kobj.sd, "direct_buffer");
	KUNIT_ASSERT_NOT_NULL(test, kn);
	bin = kn->priv;
	kernfs_put(kn);

	n = bin->write(NULL, &cdev->dev->kobj, bin, mono, 0, 3);
	KUNIT_EXPECT_EQ(test, n, -EINVAL);
	n = bin->write(NULL, &cdev->dev->kobj, bin, mono, 0, 4);
	KUNIT_ASSERT_EQ(test, n, 4);
	KUNIT_EXPECT_EQ(test, priv->last_direct_len, 4);

	dl_test_unregister(priv);
}

static struct kunit_case dl_test_cases[] = {
	KUNIT_CASE(dl_test_unknown_effect_rejected),
	KUNIT_CASE(dl_test_effect_and_palette),
	KUNIT_CASE(dl_test_fill_effects_overflow),
	KUNIT_CASE(dl_test_zone_type_unknown),
	KUNIT_CASE(dl_test_reregister_rejected),
	KUNIT_CASE(dl_test_host_rejects_parent),
	KUNIT_CASE(dl_test_host_attach),
	KUNIT_CASE(dl_test_enabled_and_power_states),
	KUNIT_CASE(dl_test_speed_direction_bounds),
	KUNIT_CASE(dl_test_set_ops_failure_preserves_state),
	KUNIT_CASE(dl_test_direct_buffer_size_and_effect),
	{ }
};

static struct kunit_suite dl_test_suite = {
	.name = "led_class_dynamic",
	.test_cases = dl_test_cases,
};

kunit_test_suite(dl_test_suite);

MODULE_AUTHOR("Marco Scardovi <scardracs@disroot.org>");
MODULE_AUTHOR("Denis Benato <denis.benato@linux.dev>");
MODULE_DESCRIPTION("KUnit tests for the Dynamic Lighting LED class");
MODULE_LICENSE("GPL");
