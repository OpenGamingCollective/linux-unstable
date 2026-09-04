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

The Dynamic Lighting class wraps the standard Linux LED class framework,
providing a unified sysfs ABI for hardware effects, stacked palette
configuration, power state persistence, and high-throughput binary frame
streaming without requiring raw character device access from userspace.

Directory Layout Example
========================
.. code-block:: console

    # ls -l /sys/class/leds/aura:keyboard/
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 brightness
    -r--r--r-- 1 root root 4096 Sep  4 17:00 max_brightness
    -r--r--r-- 1 root root 4096 Sep  4 17:00 zone_type
    -r--r--r-- 1 root root 4096 Sep  4 17:00 led_count
    -r--r--r-- 1 root root 4096 Sep  4 17:00 effects_supported
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 effect
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 effect_speed
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 effect_direction
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 effects_palette
    -r--r--r-- 1 root root 4096 Sep  4 17:00 power_states_supported
    -rw-r--r-- 1 root root 4096 Sep  4 17:00 power_states
    --w------- 1 root root  504 Sep  4 17:00 direct_buffer

Sysfs Attributes
================

``zone_type`` (read-only)
    Describes the physical topology of the zone. Outputs one of:
    ``generic``, ``keyboard_per_key``, ``matrix_2d``, ``segment_strip``,
    ``logo``, or ``lightbar``.

``led_count`` (read-only)
    Total number of individually addressable LEDs in this zone.

``matrix_dimensions`` (read-only)
    Width and height for 2D matrix zones formatted as ``<width> <height>``.
    Only visible when ``zone_type`` is ``matrix_2d``.

``effects_supported`` (read-only)
    Space-separated list of animation effects supported by the hardware.
    Names include: ``off``, ``static``, ``breathing``, ``strobe``,
    ``spectrum_cycle``, ``rainbow``, ``direct``, and ``custom``.

``effect`` (read/write)
    Currently active hardware animation effect. Writing a supported effect name
    switches the mode. Any active trigger is automatically detached upon effect
    change to eliminate lock conflicts.

``effect_speed`` (read/write)
    Current effect animation speed (0 to ``max_speed``). Only visible when the
    hardware supports adjustable speed.

``effect_direction`` (read/write)
    Animation propagation direction: ``left``, ``right``, ``up``, ``down``,
    ``clockwise``, or ``counter_clockwise``. Only visible when directional
    effects are supported.

``effects_palette`` (read/write)
    Space-separated list of 24-bit RGB hex colors (e.g. ``#ff0000 #00ff00``).
    Up to ``max_palette_entries`` colors can be defined.

``power_states_supported`` (read-only)
    List of platform power states supported for illumination persistence
    (``boot``, ``awake``, ``sleep``, ``shutdown``).

``power_states`` (read/write)
    Currently active persistence states. Writing a space-separated list of
    state names idempotently updates the active state bitmask.

``direct_buffer`` (write-only, binary)
    Raw binary sink for streaming per-key RGB frames. Each LED requires 3 bytes
    in sequence (R, G, B). The buffer write size must equal ``led_count * 3``
    bytes. Enables efficient high-rate streaming for visualizers and canvas sinks.

``frame`` (write-only, binary)
    Raw binary sink for monochrome display chunks (e.g. 2D pixel matrices) or
    segmented lighting bars.

Locking Hierarchy & Invariants
==============================
To prevent kernel deadlocks between LED triggers, sysfs handlers, and bus
transfers, the subsystem enforces the following lock order:

1. Acquire outer semaphore: ``mutex_lock(&cdev->led_access)``.
2. If the operation replaces trigger-driven output, disengage/remove the active
   LED trigger via ``led_trigger_remove(cdev)``.
3. Acquire internal mutex: ``mutex_lock(&ldev->lock)``.
4. Validate inputs, update state, and dispatch driver callbacks.
5. Release internal mutex: ``mutex_unlock(&ldev->lock)``.
6. Release outer semaphore: ``mutex_unlock(&cdev->led_access)``.

Examples
========

Setting breathing effect at medium speed:
-----------------------------------------
.. code-block:: console

    # echo "breathing" > /sys/class/leds/aura:keyboard/effect
    # echo 1 > /sys/class/leds/aura:keyboard/effect_speed

Configuring a custom 3-color palette:
-------------------------------------
.. code-block:: console

    # echo "#ff0000 #00ff00 #0000ff" > /sys/class/leds/aura:keyboard/effects_palette

Enabling illumination during boot and awake states:
---------------------------------------------------
.. code-block:: console

    # echo "boot awake" > /sys/class/leds/aura:keyboard/power_states

Streaming a direct per-key frame (168 LEDs = 504 bytes):
--------------------------------------------------------
.. code-block:: console

    # dd if=/dev/urandom of=/sys/class/leds/aura:keyboard/direct_buffer bs=504 count=1
