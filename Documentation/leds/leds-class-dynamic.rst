.. SPDX-License-Identifier: GPL-2.0

======================================
Dynamic Lighting LED class under Linux
======================================

Author: Marco Scardovi <scardracs@disroot.org>

Description
===========
The Dynamic Lighting LED class provides a standardized sysfs interface for
complex, addressable illumination hardware such as per-key RGB keyboard
matrices, 2D LED matrix displays, addressable segment strips, and chassis
lightbars.

The class can either wrap a new LED class device or attach onto an LED that
the vendor driver already registered (including ``LED_MULTI_COLOR``). Color
stays on the standard ``brightness`` / ``multi_intensity`` nodes. Dynamic
Lighting adds optional effect, speed, enable, palette, and binary frame
attributes without requiring hidraw or a rewrite of the existing LED
registration.

Directory Layout Example
========================
The following examples use ``<led>`` as a placeholder for a Dynamic Lighting
LED class device name. Optional attributes are omitted when the driver does
not implement the matching callback.

.. code-block:: console

    # ls -l /sys/class/leds/<led>/
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 brightness
    -r--r--r-- 1 root root 4096 Sep  4 17:00 max_brightness
    -r--r--r-- 1 root root 4096 Sep  4 17:00 zone_type
    -r--r--r-- 1 root root 4096 Sep  4 17:00 led_count
    -r--r--r-- 1 root root 4096 Sep  4 17:00 effect_index
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 effect
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 enabled
    -r--r--r-- 1 root root 4096 Sep  4 17:00 enabled_index
    -r--r--r-- 1 root root 4096 Sep  4 17:00 speed_range
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 speed
    -r--r--r-- 1 root root 4096 Sep  4 17:00 direction_index
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 direction
    -r--r--r-- 1 root root 4096 Sep  4 17:00 max_palette_entries
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 effects_palette
    -r--r--r-- 1 root root 4096 Sep  4 17:00 power_states_index
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 power_states
    --w------- 1 root root  504 Sep  4 17:00 direct_buffer

Attaching to an existing LED
============================
Drivers that already expose a LED can publish Dynamic Lighting as extra
attributes on that same directory. Keep the existing color path, point
``host`` at the registered LED, and supply only the ops the hardware
already implements:

.. code-block:: c

    static const struct led_dynamic_ops my_ops = {
        .set_effect  = my_set_effect,
        .set_speed   = my_set_speed,
        .set_enabled = my_set_enabled,
    };

    ldev->host = existing_led_cdev;
    ldev->ops = &my_ops;
    ldev->effects = my_effects;
    ldev->num_effects = ARRAY_SIZE(my_effects);
    led_classdev_dynamic_register(dev, ldev);

``led_dynamic_fill_effects()`` can compact an existing capability bitmask
into a name table. Effect names are defined by the driver; userspace must
read ``effect_index``.

Sysfs Attributes
================

``zone_type`` (read-only)
    Driver-defined string describing the physical topology of the zone.
    Example values include ``generic``, ``keyboard``, ``keyboard_per_key``,
    ``matrix_2d``, ``segment_strip``, ``logo``, ``lightbar``, or ``global``.

``led_count`` (read-only)
    Total number of individually addressable LEDs in this zone.

``matrix_dimensions`` (read-only)
    Width and height for 2D matrix layouts formatted as ``<width> <height>``.
    Only visible when the driver publishes non-zero matrix dimensions.

``effect_index`` (read-only)
    Space-separated list of animation effect names supported by this LED.
    Names are defined by the driver.

``effect`` (read/write)
    Currently selected animation effect. Writing a name from ``effect_index``
    switches the mode. Any active trigger is automatically detached upon
    effect change.

``enabled`` (read/write)
    Lighting engine on/off (``true`` / ``false``). Independent from
    ``effect``. Only visible when the driver implements ``set_enabled``.

``enabled_index`` (read-only)
    Values accepted by ``enabled`` (``false true``). Only visible when the
    driver implements ``set_enabled``.

``speed_range`` (read-only)
    Minimum and maximum effect animation speed accepted by ``speed``,
    formatted as ``<min>-<max>``. Only visible when the hardware supports
    adjustable speed.

``speed`` (read/write)
    Current effect animation speed (within ``speed_range``). Only visible when
    the hardware supports adjustable speed.

``direction_index`` (read-only)
    Space-separated list of directions accepted by ``direction``. Only
    visible when directional effects are supported.

``direction`` (read/write)
    Animation propagation direction: ``left``, ``right``, ``up``, ``down``,
    ``clockwise``, or ``counter_clockwise``. Only visible when directional
    effects are supported.

``max_palette_entries`` (read-only)
    Maximum number of palette entries accepted by ``effects_palette``. Only
    visible when programmable palettes are supported.

``effects_palette`` (read/write)
    Space-separated list of 24-bit RGB hex colors (e.g. ``#ff0000 #00ff00``).
    Up to ``max_palette_entries`` colors can be defined. Single-LED color
    continues to use ``brightness`` / ``multi_intensity``.

``power_states_index`` (read-only)
    List of platform power states supported for illumination persistence
    (``boot``, ``awake``, ``sleep``, ``shutdown``).

``power_states`` (read/write)
    Currently active persistence states. Writing a space-separated list of
    state names replaces the active state bitmask.

``direct_buffer`` (write-only, binary)
    Raw binary sink for streaming per-key RGB frames. Each LED requires 3 bytes
    in sequence (R, G, B). The complete write must total ``led_count * 3``
    bytes; kernfs may deliver that payload in ``PAGE_SIZE`` chunks starting at
    offset 0. Enables efficient high-rate streaming for visualizers and canvas
    sinks.

``frame`` (write-only, binary)
    Raw binary sink for monochrome display chunks (e.g. 2D pixel matrices) or
    segmented lighting bars. A write smaller than ``PAGE_SIZE`` at offset 0 is
    forwarded as one frame. A larger write may arrive in ``PAGE_SIZE`` chunks
    and is forwarded only when those chunks fill the advertised size
    contiguously from offset 0 (at most 65536 bytes). Any other offset is
    rejected.

Locking Hierarchy & Invariants
==============================
To prevent kernel deadlocks between LED triggers, sysfs handlers, and bus
transfers, the subsystem enforces the following lock order:

1. Acquire outer mutex: ``mutex_lock(&cdev->led_access)``.
2. If the operation replaces trigger-driven output, disengage/remove the active
   LED trigger via ``led_trigger_remove(cdev)``.
3. Acquire internal mutex: ``mutex_lock(&ldev->lock)``.
4. Validate inputs, update state, and dispatch driver callbacks.
5. Release internal mutex: ``mutex_unlock(&ldev->lock)``.
6. Release outer mutex: ``mutex_unlock(&cdev->led_access)``.

Driver callbacks must not persist class-owned fields (``current_effect``,
``speed``, ``enabled``, ``palette``, ``active_power_states``) on failure; the core
writes those fields only after a successful callback. ``brightness_set_blocking``
is not called with ``ldev->lock`` held and must take it if it mutates the
same state.

When Dynamic Lighting is attached to an existing LED, ``cdev`` in the lock
order is that host LED (``led_dynamic_cdev()``), not the unused embedded
``ldev->cdev``.

``frame`` writes larger than the driver-advertised ``max_frame_size`` (capped at
65536 bytes) are rejected. Empty writes are rejected.

Examples
========

Selecting an effect and speed advertised by the device:
-------------------------------------------------------
.. code-block:: console

    # cat /sys/class/leds/<led>/effect_index
    # echo <effect> > /sys/class/leds/<led>/effect
    # echo 1 > /sys/class/leds/<led>/speed

Turning lighting off without changing the selected effect:
----------------------------------------------------------
.. code-block:: console

    # echo false > /sys/class/leds/<led>/enabled

Configuring a custom 3-color palette:
-------------------------------------
.. code-block:: console

    # echo "#ff0000 #00ff00 #0000ff" > /sys/class/leds/<led>/effects_palette

Enabling illumination during boot and awake states:
---------------------------------------------------
.. code-block:: console

    # echo "boot awake" > /sys/class/leds/<led>/power_states

Streaming a direct RGB frame (for a 168-LED device, 504 bytes):
----------------------------------------------------------------
.. code-block:: console

    # dd if=/dev/urandom of=/sys/class/leds/<led>/direct_buffer bs=504 count=1
