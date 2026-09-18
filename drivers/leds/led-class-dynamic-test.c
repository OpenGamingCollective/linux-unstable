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
};

struct dl_test_priv {
	struct led_classdev_dynamic ldev;
	struct device *dev;
};

static int dl_test_set_effect(struct led_classdev_dynamic *ldev, unsigned int index)
{
	return 0;
}

static int dl_test_set_palette(struct led_classdev_dynamic *ldev,
			       const struct dl_rgb *palette,
			       unsigned int num_entries)
{
	return 0;
}

static const struct led_dynamic_ops dl_test_ops = {
	.set_effect = dl_test_set_effect,
	.set_palette = dl_test_set_palette,
};

static int dl_test_init(struct kunit *test)
{
	struct dl_test_priv *priv;
	struct device *dev;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev = kunit_device_register(test, "dl_test");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	priv->dev = get_device(dev);
	priv->ldev.cdev.name = "dl-test";
	priv->ldev.ops = &dl_test_ops;
	priv->ldev.zone_type = "keyboard";
	priv->ldev.effects = dl_test_effects;
	priv->ldev.num_effects = ARRAY_SIZE(dl_test_effects);
	priv->ldev.max_palette_entries = 2;
	priv->ldev.speed_min = 0;
	priv->ldev.speed_max = 2;

	ret = devm_led_classdev_dynamic_register(priv->dev, &priv->ldev);
	if (ret) {
		put_device(priv->dev);
		return ret;
	}

	test->priv = priv;
	return 0;
}

static void dl_test_exit(struct kunit *test)
{
	struct dl_test_priv *priv = test->priv;

	if (priv && priv->dev)
		put_device(priv->dev);
}

static bool dl_test_attr_visible(struct kunit *test, const char *attr)
{
	struct dl_test_priv *priv = test->priv;
	struct kernfs_node *kn;

	kn = kernfs_find_and_get(priv->ldev.cdev.dev->kobj.sd, attr);
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
static struct device_attribute *dl_test_attr(struct kunit *test, const char *name)
{
	struct dl_test_priv *priv = test->priv;
	struct kernfs_node *kn;
	struct attribute *attr;

	kn = kernfs_find_and_get(priv->ldev.cdev.dev->kobj.sd, name);
	KUNIT_ASSERT_NOT_NULL(test, kn);

	attr = kn->priv;
	kernfs_put(kn);
	KUNIT_ASSERT_NOT_NULL(test, attr);

	return container_of(attr, struct device_attribute, attr);
}

static int dl_test_write(struct kunit *test, const char *name, const char *text)
{
	struct dl_test_priv *priv = test->priv;
	struct device_attribute *attr = dl_test_attr(test, name);
	ssize_t n;

	KUNIT_ASSERT_NOT_NULL(test, attr->store);

	n = attr->store(priv->ldev.cdev.dev, attr, text, strlen(text));
	if (n < 0)
		return n;

	return 0;
}

static void dl_test_read(struct kunit *test, const char *name, char *buf)
{
	struct dl_test_priv *priv = test->priv;
	struct device_attribute *attr = dl_test_attr(test, name);
	ssize_t n;

	KUNIT_ASSERT_NOT_NULL(test, attr->show);

	n = attr->show(priv->ldev.cdev.dev, attr, buf);
	KUNIT_ASSERT_GE(test, n, 0);
	buf[n] = '\0';
}

static void dl_test_unknown_effect_rejected(struct kunit *test)
{
	int ret;

	KUNIT_EXPECT_TRUE(test, dl_test_attr_visible(test, "effect"));
	KUNIT_EXPECT_TRUE(test, dl_test_attr_visible(test, "effect_index"));
	KUNIT_EXPECT_FALSE(test, dl_test_attr_visible(test, "speed"));
	KUNIT_EXPECT_FALSE(test, dl_test_attr_visible(test, "enabled"));
	KUNIT_EXPECT_FALSE(test, dl_test_attr_visible(test, "direct_buffer"));

	ret = dl_test_write(test, "effect", "no-such-effect");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void dl_test_effect_and_palette(struct kunit *test)
{
	char *buf;
	int ret;

	/* show() callbacks expect a PAGE_SIZE buffer. */
	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	ret = dl_test_write(test, "effect", "breathe");
	KUNIT_ASSERT_EQ(test, ret, 0);
	dl_test_read(test, "effect", buf);
	KUNIT_EXPECT_STREQ(test, buf, "breathe\n");

	ret = dl_test_write(test, "effects_palette", "#ff0000 #00ff00");
	KUNIT_ASSERT_EQ(test, ret, 0);
	dl_test_read(test, "effects_palette", buf);
	KUNIT_EXPECT_STREQ(test, buf, "#ff0000 #00ff00\n");

	ret = dl_test_write(test, "effects_palette", "#ff0000 #00ff00 #0000ff");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = dl_test_write(test, "effects_palette", "ff0000");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static struct kunit_case dl_test_cases[] = {
	KUNIT_CASE(dl_test_unknown_effect_rejected),
	KUNIT_CASE(dl_test_effect_and_palette),
	{ }
};

static struct kunit_suite dl_test_suite = {
	.name = "led_class_dynamic",
	.init = dl_test_init,
	.exit = dl_test_exit,
	.test_cases = dl_test_cases,
};

kunit_test_suite(dl_test_suite);

MODULE_AUTHOR("Marco Scardovi <scardracs@disroot.org>");
MODULE_AUTHOR("Denis Benato <denis.benato@linux.dev>");
MODULE_DESCRIPTION("KUnit tests for the Dynamic Lighting LED class");
MODULE_LICENSE("GPL");
