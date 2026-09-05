/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LED Dynamic Lighting Class Interface
 *
 * Copyright (C) 2026 Open Gaming Collective
 * Author: Marco Scardovi <scardracs@disroot.org>
 * Author: Denis Benato <denis.benato@linux.dev>
 */

#ifndef _LINUX_LED_DYNAMIC_LIGHTING_H
#define _LINUX_LED_DYNAMIC_LIGHTING_H

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>

/**
 * enum dl_zone_type - Physical lighting zone topology
 * @DL_ZONE_TYPE_GENERIC: Single or clustered zone without specific topology
 * @DL_ZONE_TYPE_KEYBOARD_PER_KEY: Per-key addressable keyboard matrix
 * @DL_ZONE_TYPE_MATRIX_2D: 2D rectangular grid of individually addressable LEDs
 * @DL_ZONE_TYPE_SEGMENT_STRIP: 1D addressable strip or multi-segment bar
 * @DL_ZONE_TYPE_LOGO: Device badge or accent logo
 * @DL_ZONE_TYPE_LIGHTBAR: Ambient chassis lightbar or glow strip
 * @DL_ZONE_TYPE_MAX: Number of zone types
 */
enum dl_zone_type {
	DL_ZONE_TYPE_GENERIC = 0,
	DL_ZONE_TYPE_KEYBOARD_PER_KEY,
	DL_ZONE_TYPE_MATRIX_2D,
	DL_ZONE_TYPE_SEGMENT_STRIP,
	DL_ZONE_TYPE_LOGO,
	DL_ZONE_TYPE_LIGHTBAR,
	DL_ZONE_TYPE_MAX,
};

/**
 * enum dl_effect_mode - Hardware or driver-synthesized animation effect
 * @DL_EFFECT_OFF: Lighting disabled
 * @DL_EFFECT_STATIC: Fixed color across the zone
 * @DL_EFFECT_BREATHING: Pulsing brightness modulation
 * @DL_EFFECT_STROBE: Rapid intermittent flash
 * @DL_EFFECT_SPECTRUM_CYCLE: Continuous smooth chromatic transition
 * @DL_EFFECT_RAINBOW: Animated multi-color spectral wave
 * @DL_EFFECT_DIRECT: Direct binary frame streaming bypass
 * @DL_EFFECT_CUSTOM: Vendor-specific custom animation profile
 * @DL_EFFECT_MAX: Number of effect modes
 */
enum dl_effect_mode {
	DL_EFFECT_OFF = 0,
	DL_EFFECT_STATIC,
	DL_EFFECT_BREATHING,
	DL_EFFECT_STROBE,
	DL_EFFECT_SPECTRUM_CYCLE,
	DL_EFFECT_RAINBOW,
	DL_EFFECT_DIRECT,
	DL_EFFECT_CUSTOM,
	DL_EFFECT_MAX,
};

/**
 * enum dl_direction - Effect animation propagation direction
 * @DL_DIRECTION_LEFT: Animation moves toward the left
 * @DL_DIRECTION_RIGHT: Animation moves toward the right
 * @DL_DIRECTION_UP: Animation moves upward
 * @DL_DIRECTION_DOWN: Animation moves downward
 * @DL_DIRECTION_CLOCKWISE: Radial animation moving clockwise
 * @DL_DIRECTION_COUNTER_CLOCKWISE: Radial animation moving counter-clockwise
 * @DL_DIRECTION_MAX: Number of directions
 */
enum dl_direction {
	DL_DIRECTION_LEFT = 0,
	DL_DIRECTION_RIGHT,
	DL_DIRECTION_UP,
	DL_DIRECTION_DOWN,
	DL_DIRECTION_CLOCKWISE,
	DL_DIRECTION_COUNTER_CLOCKWISE,
	DL_DIRECTION_MAX,
};

/* Power state bitmask flags */
#define DL_POWER_STATE_BOOT		BIT(0)
#define DL_POWER_STATE_AWAKE		BIT(1)
#define DL_POWER_STATE_SLEEP		BIT(2)
#define DL_POWER_STATE_SHUTDOWN		BIT(3)
#define DL_POWER_STATE_ALL		(DL_POWER_STATE_BOOT | \
					 DL_POWER_STATE_AWAKE | \
					 DL_POWER_STATE_SLEEP | \
					 DL_POWER_STATE_SHUTDOWN)

/**
 * struct dl_rgb - 24-bit RGB color representation
 * @r: Red component (0-255)
 * @g: Green component (0-255)
 * @b: Blue component (0-255)
 */
struct dl_rgb {
	u8 r;
	u8 g;
	u8 b;
};

struct led_classdev_dynamic;

/**
 * struct led_dynamic_ops - Hardware driver callback vector
 * @set_effect: Configure active hardware animation effect
 * @set_speed: Configure effect speed (0 to max_speed)
 * @set_direction: Configure effect propagation direction
 * @set_palette: Apply multi-color stacked palette
 * @direct_write: Stream packed RGB buffer (size must equal led_count * 3)
 * @frame_write: Stream raw grayscale/segment frame buffer
 * @set_power_states: Update active power state persistence bitmask
 */
struct led_dynamic_ops {
	int (*set_effect)(struct led_classdev_dynamic *ldev,
			  enum dl_effect_mode mode);
	int (*set_speed)(struct led_classdev_dynamic *ldev,
			 unsigned int speed);
	int (*set_direction)(struct led_classdev_dynamic *ldev,
			     enum dl_direction direction);
	int (*set_palette)(struct led_classdev_dynamic *ldev,
			   const struct dl_rgb *palette,
			   unsigned int num_entries);
	int (*direct_write)(struct led_classdev_dynamic *ldev,
			    const u8 *buffer, size_t size);
	int (*frame_write)(struct led_classdev_dynamic *ldev,
			   const u8 *buffer, size_t size);
	int (*set_power_states)(struct led_classdev_dynamic *ldev,
				u32 active_states);
};

/**
 * struct led_classdev_dynamic - Dynamic Lighting LED class device
 * @cdev: Embedded standard LED classdev
 * @ops: Hardware callback dispatch table
 * @lock: Internal mutex protecting ldev state and serialization
 * @zone_type: Physical topology of the lighting zone
 * @led_count: Total individual LEDs in this zone
 * @matrix_width: Grid width for 2D matrix zones
 * @matrix_height: Grid height for 2D matrix zones
 * @supported_effects: Bitmask of supported enum dl_effect_mode values
 * @current_effect: Currently active animation effect
 * @speed: Current effect animation speed
 * @max_speed: Maximum supported speed level (0 if speed not adjustable)
 * @direction: Current effect animation direction
 * @supported_directions: Bitmask of supported enum dl_direction values
 * @palette: Allocated array of stacked palette color entries
 * @num_palette_entries: Current number of valid palette entries
 * @max_palette_entries: Maximum allowable palette entries
 * @palette_allocated: True if @palette was allocated by the Dynamic Lighting core
 * @supported_power_states: Bitmask of DL_POWER_STATE_* supported by hardware
 * @active_power_states: Bitmask of currently active DL_POWER_STATE_* states
 * @driver_data: Private driver reference pointer
 * @bin_attr_direct: Per-instance direct RGB binary attribute
 * @bin_attr_frame: Per-instance frame sink binary attribute
 * @bin_attrs: Array of binary attribute pointers for group
 * @group: Per-instance sysfs attribute group
 * @groups: Inline sysfs attribute groups pointer array for cdev
 * @driver_groups: Original driver-provided sysfs groups saved during registration
 * @merged_groups: Optional dynamically allocated merge of driver and Dynamic Lighting groups
 */
struct led_classdev_dynamic {
	struct led_classdev cdev;
	const struct led_dynamic_ops *ops;
	struct mutex lock; /* Protects ldev state serialization */

	enum dl_zone_type zone_type;
	unsigned int led_count;
	unsigned int matrix_width;
	unsigned int matrix_height;

	unsigned int supported_effects;
	enum dl_effect_mode current_effect;

	unsigned int speed;
	unsigned int max_speed;

	enum dl_direction direction;
	unsigned int supported_directions;

	struct dl_rgb *palette;
	unsigned int num_palette_entries;
	unsigned int max_palette_entries;
	bool palette_allocated;

	u32 supported_power_states;
	u32 active_power_states;

	void *driver_data;

	struct bin_attribute bin_attr_direct __aligned(__alignof__(const struct bin_attribute));
	struct bin_attribute bin_attr_frame __aligned(__alignof__(const struct bin_attribute));
	const struct bin_attribute *bin_attrs[3];
	struct attribute_group group;
	const struct attribute_group *groups[2];
	const struct attribute_group **driver_groups;
	const struct attribute_group **merged_groups;
};

static inline struct led_classdev_dynamic *lcdev_to_dldev(struct led_classdev *lcdev)
{
	return container_of(lcdev, struct led_classdev_dynamic, cdev);
}

static inline bool is_dynamic_lighting_led(struct led_classdev *lcdev)
{
	return !!(lcdev->flags & LED_DYNAMIC_LIGHTING);
}

#if IS_ENABLED(CONFIG_LEDS_CLASS_DYNAMIC)

int led_classdev_dynamic_register_ext(struct device *parent,
				      struct led_classdev_dynamic *ldev,
				      struct led_init_data *init_data);
void led_classdev_dynamic_unregister(struct led_classdev_dynamic *ldev);
int devm_led_classdev_dynamic_register_ext(struct device *parent,
					   struct led_classdev_dynamic *ldev,
					   struct led_init_data *init_data);
void devm_led_classdev_dynamic_unregister(struct device *parent,
					  struct led_classdev_dynamic *ldev);

#else

static inline int led_classdev_dynamic_register_ext(struct device *parent,
						    struct led_classdev_dynamic *ldev,
						    struct led_init_data *init_data)
{
	return -EOPNOTSUPP;
}

static inline void led_classdev_dynamic_unregister(struct led_classdev_dynamic *ldev) {}

static inline int devm_led_classdev_dynamic_register_ext(struct device *parent,
							 struct led_classdev_dynamic *ldev,
							 struct led_init_data *init_data)
{
	return -EOPNOTSUPP;
}

static inline void devm_led_classdev_dynamic_unregister(struct device *parent,
							struct led_classdev_dynamic *ldev) {}

#endif /* IS_ENABLED(CONFIG_LEDS_CLASS_DYNAMIC) */

static inline int devm_led_classdev_dynamic_register(struct device *parent,
						     struct led_classdev_dynamic *ldev)
{
	return devm_led_classdev_dynamic_register_ext(parent, ldev, NULL);
}

static inline int led_classdev_dynamic_register(struct device *parent,
						struct led_classdev_dynamic *ldev)
{
	return led_classdev_dynamic_register_ext(parent, ldev, NULL);
}

#endif /* _LINUX_LED_DYNAMIC_LIGHTING_H */
