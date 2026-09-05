// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Asus notebook built-in keyboard.
 *  Fixes small logical maximum to match usage maximum.
 *
 *  Currently supported devices are:
 *    EeeBook X205TA
 *    VivoBook E200HA
 *
 *  Copyright (c) 2016 Yusuke Fujimaki <usk.fujimaki@gmail.com>
 *
 *  This module based on hid-ortek by
 *  Copyright (c) 2010 Johnathon Harris <jmharris@gmail.com>
 *  Copyright (c) 2011 Jiri Kosina
 *
 *  This module has been updated to add support for Asus i2c touchpad.
 *
 *  Copyright (c) 2016 Brendan McGrath <redmcg@redmandi.dyndns.org>
 *  Copyright (c) 2016 Victor Vlasenko <victor.vlasenko@sysgears.com>
 *  Copyright (c) 2016 Frederik Wenigwieser <frederik.wenigwieser@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/dmi.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/platform_data/x86/asus-wmi.h>
#include <linux/input/mt.h>
#include <linux/usb.h> /* For to_usb_interface for T100 touchpad intf check */
#include <linux/power_supply.h>
#include <linux/leds.h>
#include <linux/led-dynamic-lighting.h>

#include "hid-ids.h"

MODULE_AUTHOR("Yusuke Fujimaki <usk.fujimaki@gmail.com>");
MODULE_AUTHOR("Brendan McGrath <redmcg@redmandi.dyndns.org>");
MODULE_AUTHOR("Victor Vlasenko <victor.vlasenko@sysgears.com>");
MODULE_AUTHOR("Frederik Wenigwieser <frederik.wenigwieser@gmail.com>");
MODULE_AUTHOR("Marco Scardovi <scardracs@disroot.org>");
MODULE_AUTHOR("Denis Benato <denis.benato@linux.dev>");
MODULE_DESCRIPTION("Asus HID Keyboard and TouchPad");

#define T100_TPAD_INTF 2
#define MEDION_E1239T_TPAD_INTF 1

#define E1239T_TP_TOGGLE_REPORT_ID 0x05
#define T100CHI_MOUSE_REPORT_ID 0x06
#define FEATURE_REPORT_ID 0x0d
#define INPUT_REPORT_ID 0x5d
#define FEATURE_KBD_REPORT_ID 0x5a
#define FEATURE_KBD_REPORT_SIZE 64
#define FEATURE_KBD_LED_REPORT_ID1 0x5d
#define FEATURE_KBD_LED_REPORT_ID2 0x5e

#define AURA_FEATURE_REPORT_SIZE	64

#define AURA_CMD_PROBE			0x05
#define AURA_CMD_SET_EFFECT		0xb3
#define AURA_CMD_COMMIT			0xb4
#define AURA_CMD_SET			0xb5
#define AURA_CMD_DIRECT			0xbc
#define AURA_CMD_ZONE_INIT		0xc0

#define AURA_ZONE_ALL			0x00
#define AURA_ZONE_KEY1			0x01
#define AURA_ZONE_KEY2			0x02
#define AURA_ZONE_KEY3			0x03
#define AURA_ZONE_KEY4			0x04
#define AURA_ZONE_BAR_LEFT		0x06
#define AURA_ZONE_BAR_RIGHT		0x07
#define AURA_ZONE_KEYBOARD_CHANNEL	0x01
#define AURA_ZONE_LIGHTBAR_CHANNEL	0x04

#define AURA_MODE_STATIC		0x00
#define AURA_MODE_BREATHING		0x01
#define AURA_MODE_SPECTRUM_CYCLE	0x02
#define AURA_MODE_RAINBOW		0x03
#define AURA_MODE_STROBING		0x0c

#define AURA_SPEED_SLOW			0xe1
#define AURA_SPEED_MED			0xeb
#define AURA_SPEED_FAST			0xf5

#define ROG_STRIX_DIRECT_LEDS		168
#define ROG_STRIX_DIRECT_BUF_SIZE	(ROG_STRIX_DIRECT_LEDS * 3)
#define ROG_STRIX_LEDS_PER_PKT		16
#define ROG_STRIX_LIGHTBAR_LEDS		12
#define ROG_STRIX_LIGHTBAR_BUF_SIZE	(ROG_STRIX_LIGHTBAR_LEDS * 3)
#define ROG_STRIX_4ZONE_LIGHTBAR_LEDS	6
#define ROG_STRIX_4ZONE_LIGHTBAR_BUF_SIZE \
					(ROG_STRIX_4ZONE_LIGHTBAR_LEDS * 3)

#define ROG_ALLY_REPORT_SIZE 64
#define ROG_ALLY_X_MIN_MCU 313
#define ROG_ALLY_MIN_MCU 319

/* Spurious HID codes sent by QUIRK_ROG_NKEY_KEYBOARD devices */
#define ASUS_SPURIOUS_CODE_0XEA 0xea
#define ASUS_SPURIOUS_CODE_0XEC 0xec
#define ASUS_SPURIOUS_CODE_0X02 0x02
#define ASUS_SPURIOUS_CODE_0X8A 0x8a
#define ASUS_SPURIOUS_CODE_0X9E 0x9e

/* Special key codes */
#define ASUS_FAN_CTRL_KEY_CODE 0xae

#define SUPPORT_KBD_BACKLIGHT BIT(0)

#define MAX_TOUCH_MAJOR 8
#define MAX_PRESSURE 128

#define BTN_LEFT_MASK 0x01
#define CONTACT_TOOL_TYPE_MASK 0x80
#define CONTACT_X_MSB_MASK 0xf0
#define CONTACT_Y_MSB_MASK 0x0f
#define CONTACT_TOUCH_MAJOR_MASK 0x07
#define CONTACT_PRESSURE_MASK 0x7f

#define	BATTERY_REPORT_ID	(0x03)
#define	BATTERY_REPORT_SIZE	(1 + 8)
#define	BATTERY_LEVEL_MAX	((u8)255)
#define	BATTERY_STAT_DISCONNECT	(0)
#define	BATTERY_STAT_CHARGING	(1)
#define	BATTERY_STAT_FULL	(2)

#define QUIRK_FIX_NOTEBOOK_REPORT	BIT(0)
#define QUIRK_NO_INIT_REPORTS		BIT(1)
#define QUIRK_SKIP_INPUT_MAPPING	BIT(2)
#define QUIRK_IS_MULTITOUCH		BIT(3)
#define QUIRK_NO_CONSUMER_USAGES	BIT(4)
#define QUIRK_USE_KBD_BACKLIGHT		BIT(5)
#define QUIRK_T100_KEYBOARD		BIT(6)
#define QUIRK_T100CHI			BIT(7)
#define QUIRK_G752_KEYBOARD		BIT(8)
#define QUIRK_T90CHI			BIT(9)
#define QUIRK_MEDION_E1239T		BIT(10)
#define QUIRK_ROG_NKEY_KEYBOARD		BIT(11)
#define QUIRK_ROG_CLAYMORE_II_KEYBOARD	BIT(12)
#define QUIRK_ROG_ALLY_XPAD		BIT(13)
#define QUIRK_HID_FN_LOCK		BIT(14)
#define QUIRK_FILTER_CAMERA_COMPANION	BIT(15)

#define I2C_KEYBOARD_QUIRKS			(QUIRK_FIX_NOTEBOOK_REPORT | \
						 QUIRK_NO_INIT_REPORTS | \
						 QUIRK_NO_CONSUMER_USAGES)
#define I2C_TOUCHPAD_QUIRKS			(QUIRK_NO_INIT_REPORTS | \
						 QUIRK_SKIP_INPUT_MAPPING | \
						 QUIRK_IS_MULTITOUCH)

#define TRKID_SGN       ((TRKID_MAX + 1) >> 1)

enum asus_work_action_type {
	FN_LOCK_SYNC,
	BRIGHTNESS_SET,
	WMI_FAN,
};

struct hid_raw_event_data {
	u8 report_data[FEATURE_KBD_REPORT_SIZE];
	size_t report_size;
};

struct asus_work_action {
	struct list_head node;
	enum asus_work_action_type type;
	union {
		/* Data for BRIGHTNESS_SET */
		unsigned int brightness;

		/* Data for FN_LOCK_SYNC */
		bool fn_lock;

		/* Data for WMI_FAN */
		struct hid_raw_event_data fan_hid_data;
	} data;
};

struct asus_worker {
	struct hid_device *hdev;
	struct work_struct work;
	struct list_head actions;
	spinlock_t lock;
	bool removed;
};

struct asus_touchpad_info {
	int max_x;
	int max_y;
	int res_x;
	int res_y;
	int contact_size;
	int max_contacts;
	int report_size;
};

struct asus_drvdata {
	unsigned long quirks;
	struct hid_device *hdev;
	struct input_dev *input;
	struct input_dev *tp_kbd_input;
	struct asus_worker *worker;
	unsigned int kbd_backlight_brightness;
	const struct asus_touchpad_info *tp;
	struct power_supply *battery;
	struct power_supply_desc battery_desc;
	int battery_capacity;
	int battery_stat;
	bool battery_in_query;
	unsigned long battery_next_query;
	struct asus_hid_listener listener;
	bool fn_lock;
#if IS_REACHABLE(CONFIG_LEDS_CLASS_DYNAMIC)
	struct mutex aura_lock; /* Serializes Aura HID reports and buffers */
	u8 *aura_buf;
	struct led_classdev_dynamic dldev_kbd;
	struct led_classdev_dynamic dldev_lightbar;
	bool has_dldev_kbd;
	bool has_dldev_lightbar;
	bool is_strix_4zone;
	bool kbd_direct_mode;
	bool lightbar_direct_mode;
	u8 kbd_direct_buf[12];
	u8 lb_direct_buf[ROG_STRIX_LIGHTBAR_BUF_SIZE];
	u8 kbd_r, kbd_g, kbd_b;
	u8 lb_r, lb_g, lb_b;
#endif
};

static int asus_report_battery(struct asus_drvdata *, u8 *, int);

static const struct asus_touchpad_info asus_i2c_tp = {
	.max_x = 2794,
	.max_y = 1758,
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 28 /* 2 byte header + 5 * 5 + 1 byte footer */,
};

static const struct asus_touchpad_info asus_t100ta_tp = {
	.max_x = 2240,
	.max_y = 1120,
	.res_x = 30, /* units/mm */
	.res_y = 27, /* units/mm */
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 28 /* 2 byte header + 5 * 5 + 1 byte footer */,
};

static const struct asus_touchpad_info asus_t100ha_tp = {
	.max_x = 2640,
	.max_y = 1320,
	.res_x = 30, /* units/mm */
	.res_y = 29, /* units/mm */
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 28 /* 2 byte header + 5 * 5 + 1 byte footer */,
};

static const struct asus_touchpad_info asus_t200ta_tp = {
	.max_x = 3120,
	.max_y = 1716,
	.res_x = 30, /* units/mm */
	.res_y = 28, /* units/mm */
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 28 /* 2 byte header + 5 * 5 + 1 byte footer */,
};

static const struct asus_touchpad_info asus_t100chi_tp = {
	.max_x = 2640,
	.max_y = 1320,
	.res_x = 31, /* units/mm */
	.res_y = 29, /* units/mm */
	.contact_size = 3,
	.max_contacts = 4,
	.report_size = 15 /* 2 byte header + 3 * 4 + 1 byte footer */,
};

static const struct asus_touchpad_info medion_e1239t_tp = {
	.max_x = 2640,
	.max_y = 1380,
	.res_x = 29, /* units/mm */
	.res_y = 28, /* units/mm */
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 32 /* 2 byte header + 5 * 5 + 5 byte footer */,
};

static const u8 asus_report_id_init[] = {
	FEATURE_KBD_REPORT_ID,
	FEATURE_KBD_LED_REPORT_ID1,
	FEATURE_KBD_LED_REPORT_ID2
};

/*
 * Send events to asus-wmi driver for handling special keys
 */
static int asus_wmi_send_event(struct asus_drvdata *drvdata, u8 code)
{
	int err;
	u32 retval;

	err = asus_wmi_evaluate_method(ASUS_WMI_METHODID_DEVS,
				       ASUS_WMI_METHODID_NOTIF, code, &retval);
	if (err) {
		pr_warn("Failed to notify asus-wmi: %d\n", err);
		return err;
	}

	if (retval != 0) {
		pr_warn("Failed to notify asus-wmi (retval): 0x%x\n", retval);
		return -EIO;
	}

	return 0;
}

static void asus_report_contact_down(struct asus_drvdata *drvdat,
		int toolType, u8 *data)
{
	struct input_dev *input = drvdat->input;
	int touch_major, pressure, x, y;

	x = (data[0] & CONTACT_X_MSB_MASK) << 4 | data[1];
	y = drvdat->tp->max_y - ((data[0] & CONTACT_Y_MSB_MASK) << 8 | data[2]);

	input_report_abs(input, ABS_MT_POSITION_X, x);
	input_report_abs(input, ABS_MT_POSITION_Y, y);

	if (drvdat->tp->contact_size < 5)
		return;

	if (toolType == MT_TOOL_PALM) {
		touch_major = MAX_TOUCH_MAJOR;
		pressure = MAX_PRESSURE;
	} else {
		touch_major = (data[3] >> 4) & CONTACT_TOUCH_MAJOR_MASK;
		pressure = data[4] & CONTACT_PRESSURE_MASK;
	}

	input_report_abs(input, ABS_MT_TOUCH_MAJOR, touch_major);
	input_report_abs(input, ABS_MT_PRESSURE, pressure);
}

/* Required for Synaptics Palm Detection */
static void asus_report_tool_width(struct asus_drvdata *drvdat)
{
	struct input_mt *mt = drvdat->input->mt;
	struct input_mt_slot *oldest;
	int oldid, i;

	if (drvdat->tp->contact_size < 5)
		return;

	oldest = NULL;
	oldid = mt->trkid;

	for (i = 0; i < mt->num_slots; ++i) {
		struct input_mt_slot *ps = &mt->slots[i];
		int id = input_mt_get_value(ps, ABS_MT_TRACKING_ID);

		if (id < 0)
			continue;
		if ((id - oldid) & TRKID_SGN) {
			oldest = ps;
			oldid = id;
		}
	}

	if (oldest) {
		input_report_abs(drvdat->input, ABS_TOOL_WIDTH,
			input_mt_get_value(oldest, ABS_MT_TOUCH_MAJOR));
	}
}

static int asus_report_input(struct asus_drvdata *drvdat, u8 *data, int size)
{
	int i, toolType = MT_TOOL_FINGER;
	u8 *contactData = data + 2;

	if (size != drvdat->tp->report_size)
		return 0;

	for (i = 0; i < drvdat->tp->max_contacts; i++) {
		bool down = !!(data[1] & BIT(i+3));

		if (drvdat->tp->contact_size >= 5)
			toolType = contactData[3] & CONTACT_TOOL_TYPE_MASK ?
						MT_TOOL_PALM : MT_TOOL_FINGER;

		input_mt_slot(drvdat->input, i);
		input_mt_report_slot_state(drvdat->input, toolType, down);

		if (down) {
			asus_report_contact_down(drvdat, toolType, contactData);
			contactData += drvdat->tp->contact_size;
		}
	}

	input_report_key(drvdat->input, BTN_LEFT, data[1] & BTN_LEFT_MASK);
	asus_report_tool_width(drvdat);

	input_mt_sync_frame(drvdat->input);
	input_sync(drvdat->input);

	return 1;
}

static int asus_e1239t_event(struct asus_drvdata *drvdat, u8 *data, int size)
{
	if (size != 3)
		return 0;

	/* Handle broken mute key which only sends press events */
	if (!drvdat->tp &&
	    data[0] == 0x02 && data[1] == 0xe2 && data[2] == 0x00) {
		input_report_key(drvdat->input, KEY_MUTE, 1);
		input_sync(drvdat->input);
		input_report_key(drvdat->input, KEY_MUTE, 0);
		input_sync(drvdat->input);
		return 1;
	}

	/* Handle custom touchpad toggle key which only sends press events */
	if (drvdat->tp_kbd_input &&
	    data[0] == 0x05 && data[1] == 0x02 && data[2] == 0x28) {
		input_report_key(drvdat->tp_kbd_input, KEY_F21, 1);
		input_sync(drvdat->tp_kbd_input);
		input_report_key(drvdat->tp_kbd_input, KEY_F21, 0);
		input_sync(drvdat->tp_kbd_input);
		return 1;
	}

	return 0;
}

/*
 * Used in atomic contexts to schedule work involving sleeps operations or
 * asus-wmi interactions.
 *
 * Caller is responsible to store relevant data in the structure to carry out
 * the required action.
 *
 * This function must be called while the spin lock protecting the workqueue
 * is already being held.
 */
static void asus_worker_schedule(struct asus_worker *worker, struct asus_work_action *action)
{
	if (worker->removed) {
		kfree(action);
		return;
	}

	list_add_tail(&action->node, &worker->actions);
	schedule_work(&worker->work);
}

static int asus_kbd_fn_lock_set(struct asus_drvdata *drvdata, bool enabled)
{
	struct asus_work_action *action;
	unsigned long flags;

	action = kzalloc(sizeof(struct asus_work_action), GFP_ATOMIC);
	if (!action)
		return -ENOMEM;

	drvdata->fn_lock = enabled;
	action->type = FN_LOCK_SYNC;
	action->data.fn_lock = drvdata->fn_lock;
	INIT_LIST_HEAD(&action->node);

	spin_lock_irqsave(&drvdata->worker->lock, flags);
	asus_worker_schedule(drvdata->worker, action);
	spin_unlock_irqrestore(&drvdata->worker->lock, flags);

	return 0;
}

static int asus_kbd_wmi_fan_send(struct asus_drvdata *drvdata, u8 *report_data,
				 size_t report_size)
{
	struct asus_work_action *action;
	unsigned long flags;

	if (report_size > FEATURE_KBD_REPORT_SIZE) {
		hid_err(drvdata->hdev, "Invalid report size for fan event: %zu\n", report_size);
		return -EINVAL;
	}

	action = kzalloc(sizeof(struct asus_work_action), GFP_NOWAIT);
	if (!action)
		return -ENOMEM;

	action->type = WMI_FAN;
	action->data.fan_hid_data.report_size = report_size;
	memcpy(action->data.fan_hid_data.report_data, report_data, report_size);
	INIT_LIST_HEAD(&action->node);

	spin_lock_irqsave(&drvdata->worker->lock, flags);
	asus_worker_schedule(drvdata->worker, action);
	spin_unlock_irqrestore(&drvdata->worker->lock, flags);

	return 0;
}

static int asus_event(struct hid_device *hdev, struct hid_field *field,
		      struct hid_usage *usage, __s32 value)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	int ret;

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_ASUSVENDOR &&
	    (usage->hid & HID_USAGE) != 0x00 &&
	    (usage->hid & HID_USAGE) != 0xff && !usage->type) {
		hid_warn(hdev, "Unmapped Asus vendor usagepage code 0x%02x\n",
			 usage->hid & HID_USAGE);
	}

	if (usage->type == EV_KEY && value) {
		switch (usage->code) {
		case KEY_KBDILLUMUP:
			return !asus_hid_event(ASUS_EV_BRTUP);
		case KEY_KBDILLUMDOWN:
			return !asus_hid_event(ASUS_EV_BRTDOWN);
		case KEY_KBDILLUMTOGGLE:
			return !asus_hid_event(ASUS_EV_BRTTOGGLE);
		case KEY_FN_ESC:
			if (drvdata->quirks & QUIRK_HID_FN_LOCK) {
				ret = asus_kbd_fn_lock_set(drvdata, !drvdata->fn_lock);
				if (ret) {
					hid_err(hdev, "Error while toggling FN lock: %d\n", ret);
					return ret;
				}
			}
			break;
		}
	}

	return 0;
}

static int asus_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	int ret;

	if (size < 2) {
		hid_dbg(hdev, "Unexpected keyboard report size %d\n", size);
		return 0;
	}

	if (drvdata->battery && data[0] == BATTERY_REPORT_ID)
		return asus_report_battery(drvdata, data, size);

	if (drvdata->tp && data[0] == INPUT_REPORT_ID)
		return asus_report_input(drvdata, data, size);

	if (drvdata->quirks & QUIRK_MEDION_E1239T)
		return asus_e1239t_event(drvdata, data, size);

	/*
	 * Skip these report ID, the device emits a continuous stream associated
	 * with the AURA mode it is in which looks like an 'echo'.
	 */
	if (report->id == FEATURE_KBD_LED_REPORT_ID1 || report->id == FEATURE_KBD_LED_REPORT_ID2)
		return -1;
	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD) {
		if (report->id == FEATURE_KBD_REPORT_ID) {
			/*
			 * Fn+F5 fan control key - try to send WMI event to toggle fan mode.
			 * If successful, block the event from reaching userspace.
			 * If asus-wmi is unavailable or the call fails, let the event
			 * pass to userspace so it can implement its own fan control.
			 */
			if (data[1] == ASUS_FAN_CTRL_KEY_CODE) {
				ret = asus_kbd_wmi_fan_send(drvdata, data, size);

				/* if execution deferred successfully block event */
				if (ret == 0)
					return -1;

				return ret;
			}

			/*
			 * ASUS ROG laptops send these codes during normal operation
			 * with no discernable reason. Filter them out to avoid
			 * unmapped warning messages.
			 */
			if (data[1] == ASUS_SPURIOUS_CODE_0XEA ||
			    data[1] == ASUS_SPURIOUS_CODE_0XEC ||
			    data[1] == ASUS_SPURIOUS_CODE_0X02 ||
			    data[1] == ASUS_SPURIOUS_CODE_0X8A ||
			    data[1] == ASUS_SPURIOUS_CODE_0X9E) {
				return -1;
			}
		}

		/*
		 * G713 and G733 send these codes on some keypresses, depending on
		 * the key pressed it can trigger a shutdown event if not caught.
		 */
		if (data[0] == 0x02 && data[1] == 0x30)
			return -1;
	}

	if (drvdata->quirks & QUIRK_ROG_CLAYMORE_II_KEYBOARD) {
		/*
		 * CLAYMORE II keyboard sends this packet when it goes to sleep
		 * this causes the whole system to go into suspend.
		 */
		if (size == 2 && data[0] == 0x02 && data[1] == 0x00)
			return -1;
	}

	/*
	 * The camera-toggle key reports its vendor usage (0x85) together with a
	 * companion state byte in the same array report, e.g. "5a 85 01" and
	 * "5a 85 10" for the two toggle positions. The 0x10 companion aliases the
	 * brightness-down vendor usage and would spuriously dim the panel, so drop
	 * the companion slots and leave only the camera usage for input mapping.
	 */
	if (drvdata->quirks & QUIRK_FILTER_CAMERA_COMPANION &&
	    report->id == FEATURE_KBD_REPORT_ID && size >= 3 && data[1] == 0x85)
		memset(&data[2], 0, size - 2);

	return 0;
}

static int asus_kbd_set_report(struct hid_device *hdev, const u8 *buf, size_t buf_size)
{
	unsigned char report_type = HID_FEATURE_REPORT;
	u8 *dmabuf __free(kfree) = kmemdup(buf, buf_size, GFP_KERNEL);
	int ret;

	if (!dmabuf)
		return -ENOMEM;

	if (buf[0] == FEATURE_KBD_LED_REPORT_ID1 || buf[0] == FEATURE_KBD_LED_REPORT_ID2) {
		ret = hid_hw_output_report(hdev, dmabuf, buf_size);
		if (ret >= 0)
			return 0;

		report_type = HID_OUTPUT_REPORT;
	}

	/*
	 * The report ID should be set from the incoming buffer due to LED and key
	 * interfaces having different pages
	 */
	ret = hid_hw_raw_request(hdev, buf[0], dmabuf, buf_size, report_type,
				 HID_REQ_SET_REPORT);
	if (ret < 0)
		return ret;

	return 0;
}

static int asus_kbd_init(struct hid_device *hdev, u8 report_id)
{
	/*
	 * The handshake is first sent as a set_report, then retrieved
	 * from a get_report. They should be equal.
	 */
	const u8 buf[] = { report_id, 0x41, 0x53, 0x55, 0x53, 0x20, 0x54,
		     0x65, 0x63, 0x68, 0x2e, 0x49, 0x6e, 0x63, 0x2e, 0x00 };
	int ret;

	ret = asus_kbd_set_report(hdev, buf, sizeof(buf));
	if (ret < 0) {
		hid_err(hdev, "Asus handshake %02x failed to send: %d\n",
			report_id, ret);
		return ret;
	}

	u8 *readbuf __free(kfree) = kzalloc(FEATURE_KBD_REPORT_SIZE, GFP_KERNEL);
	if (!readbuf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, report_id, readbuf,
				 FEATURE_KBD_REPORT_SIZE, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret < 0) {
		hid_warn(hdev, "Asus handshake %02x failed to receive ack: %d\n",
			 report_id, ret);
	} else if (memcmp(readbuf, buf, sizeof(buf)) != 0) {
		hid_warn(hdev, "Asus handshake %02x returned invalid response: %*ph\n",
			 report_id, FEATURE_KBD_REPORT_SIZE, readbuf);
	}

	/*
	 * Do not return error if handshake is wrong until this is
	 * verified to work for all devices.
	 */
	return 0;
}

static int asus_kbd_get_functions(struct hid_device *hdev,
				  unsigned char *kbd_func,
				  u8 report_id)
{
	const u8 buf[] = { report_id, 0x05, 0x20, 0x31, 0x00, 0x08 };
	u8 *readbuf;
	int ret;

	ret = asus_kbd_set_report(hdev, buf, sizeof(buf));
	if (ret < 0) {
		hid_err(hdev, "Asus failed to send configuration command: %d\n", ret);
		return ret;
	}

	readbuf = kzalloc(FEATURE_KBD_REPORT_SIZE, GFP_KERNEL);
	if (!readbuf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, report_id, readbuf,
				 FEATURE_KBD_REPORT_SIZE, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret < 0) {
		hid_err(hdev, "Asus failed to request functions: %d\n", ret);
		kfree(readbuf);
		return ret;
	}

	*kbd_func = readbuf[6];

	kfree(readbuf);
	return ret;
}

static int asus_kbd_disable_oobe(struct hid_device *hdev)
{
	const u8 init[][6] = {
		{ FEATURE_KBD_REPORT_ID, 0x05, 0x20, 0x31, 0x00, 0x08 },
		{ FEATURE_KBD_REPORT_ID, 0xBA, 0xC5, 0xC4 },
		{ FEATURE_KBD_REPORT_ID, 0xD0, 0x8F, 0x01 },
		{ FEATURE_KBD_REPORT_ID, 0xD0, 0x85, 0xFF }
	};
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(init); i++) {
		ret = asus_kbd_set_report(hdev, init[i], sizeof(init[i]));
		if (ret < 0)
			return ret;
	}

	hid_info(hdev, "Disabled OOBE for keyboard\n");
	return 0;
}

static void asus_kbd_set_fn_lock(struct hid_device *hdev, bool enabled)
{
	const u8 buf[FEATURE_KBD_REPORT_SIZE] = { FEATURE_KBD_REPORT_ID, 0xd0, 0x4e, !!enabled };
	int ret;

	ret = asus_kbd_set_report(hdev, buf, sizeof(buf));
	if (ret < 0)
		hid_err(hdev, "Asus failed to set fn lock: %d\n", ret);
}

static void asus_kbd_set_brightness(struct hid_device *hdev, u8 brightness)
{
	const u8 buf[FEATURE_KBD_REPORT_SIZE] = {
		FEATURE_KBD_REPORT_ID, 0xba, 0xc5, 0xc4, brightness
	};
	int ret;

	ret = asus_kbd_set_report(hdev, buf, sizeof(buf));
	if (ret < 0)
		hid_err(hdev, "Asus failed to set keyboard backlight: %d\n", ret);
}

static void asus_kbd_wmi_fan(struct hid_device *hdev, struct hid_raw_event_data *data)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	int ret;

	ret = asus_wmi_send_event(drvdata, ASUS_FAN_CTRL_KEY_CODE);

	/*
	 * Warn if asus-wmi failed (but not if it's unavailable).
	 * Let the event reach userspace in all failure cases.
	 */
	switch (ret) {
	case -ENODEV:
		break;
	case 0:
		return;
	default:
		hid_warn(hdev, "Failed to notify asus-wmi: %d\n", ret);
		break;
	}

	/*
	 * Fallback: pass the raw event to the HID core; to avoid
	 * racing against the hid_report_raw_event() that generated
	 * this event use the same locking mechanism and wait for
	 * that function to terminate and signal the deferred execution
	 * before raising the stored event.
	 */
	down(&hdev->driver_input_lock);
	hid_report_raw_event(hdev, HID_INPUT_REPORT,
			     data->report_data, data->report_size,
			     data->report_size, 1);
	up(&hdev->driver_input_lock);
}

static void asus_kbd_backlight_set(struct asus_hid_listener *listener, int brightness)
{
	struct asus_drvdata *drvdata = container_of(listener, struct asus_drvdata, listener);
	struct asus_worker *worker = drvdata->worker;
	struct asus_work_action *action;
	unsigned long flags;

	drvdata->kbd_backlight_brightness = brightness;

	action = kzalloc(sizeof(struct asus_work_action), GFP_NOWAIT);
	if (!action)
		return;

	action->type = BRIGHTNESS_SET;
	action->data.brightness = brightness;
	INIT_LIST_HEAD(&action->node);

	spin_lock_irqsave(&worker->lock, flags);
	asus_worker_schedule(worker, action);
	spin_unlock_irqrestore(&worker->lock, flags);
}

static void asus_work(struct work_struct *work)
{
	struct asus_worker *worker = container_of(work, struct asus_worker, work);
	struct asus_work_action *action = NULL;
	unsigned long flags;

	/* Save the action to be performed and clear the flag */
	spin_lock_irqsave(&worker->lock, flags);
	if (!list_empty(&worker->actions)) {
		action = list_first_entry(&worker->actions,
					  struct asus_work_action, node);
		list_del(&action->node);
	}
	spin_unlock_irqrestore(&worker->lock, flags);

	if (!action)
		return;

	switch (action->type) {
	case BRIGHTNESS_SET:
		asus_kbd_set_brightness(worker->hdev, action->data.brightness);
		break;
	case FN_LOCK_SYNC:
		asus_kbd_set_fn_lock(worker->hdev, action->data.fn_lock);
		break;
	case WMI_FAN:
		asus_kbd_wmi_fan(worker->hdev, &action->data.fan_hid_data);
		break;
	default:
		hid_err(worker->hdev, "Invalid action type: %d\n", action->type);
		break;
	}

	kfree(action);

	/* Re-schedule if there are more pending actions */
	spin_lock_irqsave(&worker->lock, flags);
	if (!list_empty(&worker->actions))
		schedule_work(&worker->work);
	spin_unlock_irqrestore(&worker->lock, flags);
}

static int asus_worker_create(struct hid_device *hdev, struct asus_drvdata *drvdata)
{
	drvdata->worker = devm_kzalloc(&hdev->dev, sizeof(struct asus_worker), GFP_KERNEL);
	if (!drvdata->worker)
		return -ENOMEM;

	drvdata->worker->removed = false;
	drvdata->worker->hdev = hdev;
	INIT_LIST_HEAD(&drvdata->worker->actions);

	INIT_WORK(&drvdata->worker->work, asus_work);
	spin_lock_init(&drvdata->worker->lock);

	return 0;
}

static void asus_worker_stop(struct asus_worker *worker)
{
	struct asus_work_action *action, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&worker->lock, flags);
	worker->removed = true;
	list_for_each_entry_safe(action, tmp, &worker->actions, node) {
		list_del(&action->node);
		kfree(action);
	}
	spin_unlock_irqrestore(&worker->lock, flags);

	cancel_work_sync(&worker->work);
}

/*
 * We don't care about any other part of the string except the version section.
 * Example strings: FGA80100.RC72LA.312_T01, FGA80100.RC71LS.318_T01
 * The bytes "5a 05 03 31 00 1a 13" and possibly more come before the version
 * string, and there may be additional bytes after the version string such as
 * "75 00 74 00 65 00" or a postfix such as "_T01"
 */
static int mcu_parse_version_string(const u8 *response, size_t response_size)
{
	const u8 *end = response + response_size;
	const u8 *p = response;
	int dots, err, version;
	char buf[4];

	dots = 0;
	while (p < end && dots < 2) {
		if (*p++ == '.')
			dots++;
	}

	if (dots != 2 || end - p < 3)
		return -EINVAL;

	memcpy(buf, p, 3);
	buf[3] = '\0';

	err = kstrtoint(buf, 10, &version);
	if (err || version < 0)
		return -EINVAL;

	return version;
}

static int mcu_request_version(struct hid_device *hdev)
{
	u8 *response __free(kfree) = kzalloc(ROG_ALLY_REPORT_SIZE, GFP_KERNEL);
	const u8 request[] = { 0x5a, 0x05, 0x03, 0x31, 0x00, 0x20 };
	int ret;

	if (!response)
		return -ENOMEM;

	ret = asus_kbd_set_report(hdev, request, sizeof(request));
	if (ret < 0)
		return ret;

	ret = hid_hw_raw_request(hdev, FEATURE_REPORT_ID, response,
				ROG_ALLY_REPORT_SIZE, HID_FEATURE_REPORT,
				HID_REQ_GET_REPORT);
	if (ret < 0)
		return ret;

	ret = mcu_parse_version_string(response, ROG_ALLY_REPORT_SIZE);
	if (ret < 0) {
		pr_err("Failed to parse MCU version: %d\n", ret);
		print_hex_dump(KERN_ERR, "MCU: ", DUMP_PREFIX_NONE,
			      16, 1, response, ROG_ALLY_REPORT_SIZE, false);
	}

	return ret;
}

static void validate_mcu_fw_version(struct hid_device *hdev, int idProduct)
{
	int min_version, version;

	version = mcu_request_version(hdev);
	if (version < 0)
		return;

	switch (idProduct) {
	case USB_DEVICE_ID_ASUSTEK_ROG_NKEY_ALLY:
		min_version = ROG_ALLY_MIN_MCU;
		break;
	case USB_DEVICE_ID_ASUSTEK_ROG_NKEY_ALLY_X:
		min_version = ROG_ALLY_X_MIN_MCU;
		break;
	default:
		min_version = 0;
	}

	if (version < min_version) {
		hid_warn(hdev,
			"The MCU firmware version must be %d or greater to avoid issues with suspend.\n",
			min_version);
	} else {
		set_ally_mcu_hack(ASUS_WMI_ALLY_MCU_HACK_DISABLED);
		set_ally_mcu_powersave(true);
	}
}

static bool asus_has_report_id(struct hid_device *hdev, u16 report_id)
{
	struct hid_report *report;
	int t;

	for (t = HID_INPUT_REPORT; t <= HID_FEATURE_REPORT; t++) {
		list_for_each_entry(report, &hdev->report_enum[t].report_list, list) {
			if (report->id == report_id)
				return true;
		}
	}

	return false;
}

static int asus_kbd_register_leds(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	struct usb_interface *intf;
	struct usb_device *udev;
	unsigned char kbd_func;
	int ret;

	/* Get keyboard functions */
	ret = asus_kbd_get_functions(hdev, &kbd_func, FEATURE_KBD_REPORT_ID);
	if (ret < 0)
		return ret;

	/* Check for backlight support */
	if (!(kbd_func & SUPPORT_KBD_BACKLIGHT))
		return -ENODEV;

	if (dmi_match(DMI_PRODUCT_FAMILY, "ProArt P16")) {
		ret = asus_kbd_disable_oobe(hdev);
		if (ret < 0)
			return ret;
	}

	if ((drvdata->quirks & QUIRK_ROG_ALLY_XPAD) && hid_is_usb(hdev)) {
		intf = to_usb_interface(hdev->dev.parent);
		udev = interface_to_usbdev(intf);
		validate_mcu_fw_version(hdev,
			le16_to_cpu(udev->descriptor.idProduct));
	}

	drvdata->listener.brightness_set = asus_kbd_backlight_set;
	ret = asus_hid_register_listener(&drvdata->listener);
	if (ret < 0) {
		hid_err(hdev, "Unable to register kbd brightness listener: %d\n", ret);
		drvdata->listener.brightness_set = NULL;
	}

	return ret;
}

#if IS_REACHABLE(CONFIG_LEDS_CLASS_DYNAMIC)

static int asus_aura_set_feature_unlocked(struct asus_drvdata *drvdata,
					  const u8 *buf, size_t buf_size)
{
	int ret;

	if (buf_size > AURA_FEATURE_REPORT_SIZE)
		return -EINVAL;

	memcpy(drvdata->aura_buf, buf, buf_size);
	if (buf_size < AURA_FEATURE_REPORT_SIZE)
		memset(drvdata->aura_buf + buf_size, 0,
		       AURA_FEATURE_REPORT_SIZE - buf_size);

	/*
	 * Try Output Report first matching Armoury Crate / asus_kbd_set_report.
	 * If the device lacks an interrupt OUT endpoint, fall back to
	 * hid_hw_raw_request() with HID_OUTPUT_REPORT, and finally to
	 * HID_FEATURE_REPORT.
	 */
	ret = hid_hw_output_report(drvdata->hdev, drvdata->aura_buf,
				   AURA_FEATURE_REPORT_SIZE);
	if (ret >= 0)
		return 0;

	ret = hid_hw_raw_request(drvdata->hdev, drvdata->aura_buf[0],
				 drvdata->aura_buf, AURA_FEATURE_REPORT_SIZE,
				 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret >= 0)
		return 0;

	ret = hid_hw_raw_request(drvdata->hdev, drvdata->aura_buf[0],
				 drvdata->aura_buf, AURA_FEATURE_REPORT_SIZE,
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		return ret;

	return 0;
}

static int asus_aura_set_feature(struct asus_drvdata *drvdata,
				 const u8 *buf, size_t buf_size)
{
	guard(mutex)(&drvdata->aura_lock);

	return asus_aura_set_feature_unlocked(drvdata, buf, buf_size);
}

static int asus_aura_get_feature(struct asus_drvdata *drvdata,
				 u8 *buf, size_t buf_size)
{
	int ret;

	if (buf_size > AURA_FEATURE_REPORT_SIZE)
		return -EINVAL;

	guard(mutex)(&drvdata->aura_lock);

	memset(drvdata->aura_buf, 0, AURA_FEATURE_REPORT_SIZE);
	drvdata->aura_buf[0] = buf[0];

	ret = hid_hw_raw_request(drvdata->hdev, buf[0], drvdata->aura_buf,
				 AURA_FEATURE_REPORT_SIZE,
				 HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		return ret;

	memcpy(buf, drvdata->aura_buf, buf_size);
	return 0;
}

static int asus_aura_commit(struct asus_drvdata *drvdata)
{
	u8 buf_apply[AURA_FEATURE_REPORT_SIZE] = {
		FEATURE_KBD_LED_REPORT_ID1,
		AURA_CMD_COMMIT,
	};
	u8 buf_set[AURA_FEATURE_REPORT_SIZE] = {
		FEATURE_KBD_LED_REPORT_ID1,
		AURA_CMD_SET,
	};
	int ret;

	/*
	 * ASUS Aura firmware requires sending the APPLY commit command (0x5d 0xb4)
	 * followed by the staging SET command (0x5d 0xb5) to activate and persist
	 * register changes, matching Armoury Crate and asusctl.
	 */
	ret = asus_aura_set_feature(drvdata, buf_apply, sizeof(buf_apply));
	if (ret < 0)
		return ret;

	return asus_aura_set_feature(drvdata, buf_set, sizeof(buf_set));
}

static int asus_aura_wake_all_zones(struct asus_drvdata *drvdata)
{
	u8 buf_pwr[AURA_FEATURE_REPORT_SIZE] = {
		FEATURE_KBD_LED_REPORT_ID1,
		0xbd,
		0x01,
		0xff,
		0x1f,
		0xff,
		0xff,
		0xff,
	};
	unsigned int z;
	int ret;

	/* Unmute power gating across all 5 hardware zones */
	ret = asus_aura_set_feature(drvdata, buf_pwr, sizeof(buf_pwr));
	if (ret < 0)
		return ret;

	/* Wake up all 5 hardware zones: 0..3 (keyboard 4 zones), 4 (chassis lightbar) */
	for (z = 0; z < 5; z++) {
		u8 buf_init[AURA_FEATURE_REPORT_SIZE] = {
			FEATURE_KBD_LED_REPORT_ID1,
			AURA_CMD_ZONE_INIT,
			(u8)z,
			0x01,
			0x01,
		};

		ret = asus_aura_set_feature(drvdata, buf_init, sizeof(buf_init));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int asus_aura_write_zone_effect(struct asus_drvdata *drvdata, u8 zone,
				       u8 aura_mode, u8 r, u8 g, u8 b,
				       u8 speed, u8 direction,
				       u8 r2, u8 g2, u8 b2,
				       bool commit)
{
	u8 buf[AURA_FEATURE_REPORT_SIZE] = { 0 };
	int ret;

	buf[0] = FEATURE_KBD_LED_REPORT_ID1;
	buf[1] = AURA_CMD_SET_EFFECT;
	buf[2] = zone;
	buf[3] = aura_mode;
	buf[4] = r;
	buf[5] = g;
	buf[6] = b;
	buf[7] = speed;
	buf[8] = direction;
	buf[9] = 0x00;
	buf[10] = r2;
	buf[11] = g2;
	buf[12] = b2;

	ret = asus_aura_set_feature(drvdata, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	if (commit)
		return asus_aura_commit(drvdata);

	return 0;
}

static int asus_aura_strix_set_direct(struct led_classdev_dynamic *ldev,
				      const u8 *buffer, size_t size)
{
	struct asus_drvdata *drvdata = ldev->driver_data;
	u8 buf[AURA_FEATURE_REPORT_SIZE];
	unsigned int i;
	int ret;

	if (size != ldev->led_count * 3)
		return -EINVAL;

	guard(mutex)(&drvdata->aura_lock);

	/*
	 * Ensure the controller is in direct mode with the enable handshake.
	 */
	if (!drvdata->kbd_direct_mode && !drvdata->lightbar_direct_mode) {
		memset(buf, 0, sizeof(buf));
		buf[0] = FEATURE_KBD_LED_REPORT_ID1;
		buf[1] = AURA_CMD_DIRECT;
		buf[2] = 0x01;
		ret = asus_aura_set_feature_unlocked(drvdata, buf, sizeof(buf));
		if (ret < 0)
			return ret;
	}
	drvdata->kbd_direct_mode = true;

	ldev->current_effect = DL_EFFECT_DIRECT;

	if (drvdata->is_strix_4zone) {
		unsigned int leds = min_t(size_t, size / 3, 4);

		memcpy(drvdata->kbd_direct_buf, buffer, leds * 3);
		if (leds > 0) {
			drvdata->kbd_r = buffer[0];
			drvdata->kbd_g = buffer[1];
			drvdata->kbd_b = buffer[2];
		}

		memset(buf, 0, sizeof(buf));
		buf[0] = FEATURE_KBD_LED_REPORT_ID1;
		buf[1] = AURA_CMD_DIRECT;
		buf[2] = 0x01;
		buf[3] = 0x01;
		buf[4] = AURA_ZONE_KEYBOARD_CHANNEL;

		memcpy(&buf[9], drvdata->kbd_direct_buf, sizeof(drvdata->kbd_direct_buf));
		memcpy(&buf[27], drvdata->lb_direct_buf,
		       ROG_STRIX_4ZONE_LIGHTBAR_BUF_SIZE);

		return asus_aura_set_feature_unlocked(drvdata, buf, sizeof(buf));
	}

	/*
	 * Stream ROG Strix per-key matrix in 16-LED chunks using
	 * Aura HID Feature Reports with opcode 0xbc:
	 * [0]    = Report ID (0x5d)
	 * [1]    = Direct frame command (0xbc)
	 * [2..5] = Routing header (0x00, 0x01, 0x01, 0x01)
	 * [6]    = Start LED index (0, 16, 32, ..., 160)
	 * [7]    = Number of LEDs in chunk (16 for chunks 0..9, 8 for chunk 10)
	 * [8]    = Reserved / 0x00
	 * [9..]  = RGB payload (3 bytes per LED)
	 *
	 * Total LEDs: 168 (11 packets). Strictly terminate at packet 10;
	 * sending a 12th packet triggers a firmware defect that shuts off the
	 * rear and front lightbars. No 0xb4 commit command is issued for raw
	 * direct frames to avoid stepping hardware animation registers.
	 */
	for (i = 0; i < ROG_STRIX_DIRECT_LEDS; i += ROG_STRIX_LEDS_PER_PKT) {
		unsigned int leds = min_t(unsigned int, ROG_STRIX_DIRECT_LEDS - i,
					  ROG_STRIX_LEDS_PER_PKT);
		size_t payload_len = leds * 3;

		memset(buf, 0, sizeof(buf));
		buf[0] = FEATURE_KBD_LED_REPORT_ID1;
		buf[1] = AURA_CMD_DIRECT;
		buf[2] = 0x00;
		buf[3] = 0x01;
		buf[4] = AURA_ZONE_KEYBOARD_CHANNEL;
		buf[5] = 0x01;
		buf[6] = (u8)i;
		buf[7] = (u8)leds;
		buf[8] = 0x00;
		memcpy(&buf[9], buffer + (i * 3), payload_len);

		ret = asus_aura_set_feature_unlocked(drvdata, buf, sizeof(buf));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int asus_aura_lightbar_write_packet(struct asus_drvdata *drvdata,
					   const u8 *buffer, size_t size)
{
	u8 buf[AURA_FEATURE_REPORT_SIZE];
	int ret;

	guard(mutex)(&drvdata->aura_lock);

	/*
	 * Ensure the controller is in direct mode with the enable handshake.
	 */
	if (!drvdata->kbd_direct_mode && !drvdata->lightbar_direct_mode) {
		memset(buf, 0, sizeof(buf));
		buf[0] = FEATURE_KBD_LED_REPORT_ID1;
		buf[1] = AURA_CMD_DIRECT;
		buf[2] = 0x01;
		ret = asus_aura_set_feature_unlocked(drvdata, buf, sizeof(buf));
		if (ret < 0)
			return ret;
	}
	drvdata->lightbar_direct_mode = true;

	memset(buf, 0, sizeof(buf));
	buf[0] = FEATURE_KBD_LED_REPORT_ID1;
	buf[1] = AURA_CMD_DIRECT;
	buf[2] = drvdata->is_strix_4zone ? 0x01 : 0x00;
	buf[3] = 0x01;
	buf[4] = AURA_ZONE_LIGHTBAR_CHANNEL;

	if (drvdata->is_strix_4zone) {
		unsigned int leds = min_t(size_t, size / 3,
					  ROG_STRIX_4ZONE_LIGHTBAR_LEDS);

		memcpy(drvdata->lb_direct_buf, buffer, leds * 3);
		memcpy(&buf[9], drvdata->kbd_direct_buf, sizeof(drvdata->kbd_direct_buf));
		memcpy(&buf[27], drvdata->lb_direct_buf,
		       ROG_STRIX_4ZONE_LIGHTBAR_BUF_SIZE);
	} else {
		size_t payload_len = min_t(size_t, size, ROG_STRIX_LIGHTBAR_BUF_SIZE);

		/*
		 * Per-Key models use buf[2] = 0x00 (chunked packet), so the MCU
		 * expects valid chunk headers in bytes 5-7. Without them it reads
		 * "0 LEDs to update" and silently drops the packet.
		 */
		buf[5] = 0x01;
		buf[6] = 0x00;             /* start LED index */
		buf[7] = (u8)(payload_len / 3); /* number of LEDs in this packet */

		memcpy(drvdata->lb_direct_buf, buffer, payload_len);
		memcpy(&buf[9], drvdata->lb_direct_buf, payload_len);
	}

	if (size >= 3) {
		drvdata->lb_r = buffer[0];
		drvdata->lb_g = buffer[1];
		drvdata->lb_b = buffer[2];
	}

	return asus_aura_set_feature_unlocked(drvdata, buf, sizeof(buf));
}

static int asus_aura_lightbar_set_direct(struct led_classdev_dynamic *ldev,
					 const u8 *buffer, size_t size)
{
	struct asus_drvdata *drvdata = ldev->driver_data;

	if (size != ldev->led_count * 3)
		return -EINVAL;

	ldev->current_effect = DL_EFFECT_DIRECT;
	return asus_aura_lightbar_write_packet(drvdata, buffer, size);
}

static int asus_aura_apply_effect(struct led_classdev_dynamic *ldev,
				  enum dl_effect_mode mode,
				  enum led_brightness brightness)
{
	struct asus_drvdata *drvdata = ldev->driver_data;
	u8 aura_mode;
	u8 speed;
	u8 direction = 0;
	u8 r = 0, g = 0, b = 0;
	u8 r2 = 0, g2 = 0, b2 = 0;
	u8 buf[AURA_FEATURE_REPORT_SIZE];
	int ret;

	if (mode != DL_EFFECT_OFF && ldev->num_palette_entries > 0 && brightness > LED_OFF) {
		r = (u8)(((unsigned int)ldev->palette[0].r * brightness) / 255);
		g = (u8)(((unsigned int)ldev->palette[0].g * brightness) / 255);
		b = (u8)(((unsigned int)ldev->palette[0].b * brightness) / 255);
		if (ldev->num_palette_entries > 1) {
			r2 = (u8)(((unsigned int)ldev->palette[1].r * brightness) / 255);
			g2 = (u8)(((unsigned int)ldev->palette[1].g * brightness) / 255);
			b2 = (u8)(((unsigned int)ldev->palette[1].b * brightness) / 255);
		}
	}

	if (ldev->zone_type == DL_ZONE_TYPE_LIGHTBAR) {
		drvdata->lb_r = r;
		drvdata->lb_g = g;
		drvdata->lb_b = b;
	} else {
		drvdata->kbd_r = r;
		drvdata->kbd_g = g;
		drvdata->kbd_b = b;
		if (drvdata->is_strix_4zone) {
			unsigned int z;

			for (z = 0; z < 4; z++) {
				drvdata->kbd_direct_buf[z * 3 + 0] = r;
				drvdata->kbd_direct_buf[z * 3 + 1] = g;
				drvdata->kbd_direct_buf[z * 3 + 2] = b;
			}
		}
	}

	if (mode == DL_EFFECT_DIRECT)
		return 0;

	switch (ldev->speed) {
	case 0:
		speed = AURA_SPEED_SLOW;
		break;
	case 2:
		speed = AURA_SPEED_FAST;
		break;
	case 1:
	default:
		speed = AURA_SPEED_MED;
		break;
	}

	if (brightness == LED_OFF || mode == DL_EFFECT_OFF) {
		aura_mode = AURA_MODE_STATIC;
		r = 0;
		g = 0;
		b = 0;
		r2 = 0;
		g2 = 0;
		b2 = 0;
	} else {
		switch (mode) {
		case DL_EFFECT_STATIC:
			aura_mode = AURA_MODE_STATIC;
			break;
		case DL_EFFECT_BREATHING:
			aura_mode = AURA_MODE_BREATHING;
			break;
		case DL_EFFECT_STROBE:
			aura_mode = AURA_MODE_STROBING;
			break;
		case DL_EFFECT_SPECTRUM_CYCLE:
			aura_mode = AURA_MODE_SPECTRUM_CYCLE;
			break;
		case DL_EFFECT_RAINBOW:
			aura_mode = AURA_MODE_RAINBOW;
			break;
		default:
			return -EINVAL;
		}
	}

	if (ldev->direction == DL_DIRECTION_LEFT)
		direction = 1;
	else if (ldev->direction == DL_DIRECTION_RIGHT)
		direction = 0;
	else if (ldev->direction == DL_DIRECTION_UP)
		direction = 2;
	else if (ldev->direction == DL_DIRECTION_DOWN)
		direction = 3;

	/*
	 * If channel was in direct streaming mode and a hardware animation
	 * effect (non-static) is requested, disable direct mode on that
	 * channel. If no channels remain in direct mode, signal the MCU
	 * direct mode disable handshake so the hardware animation engine
	 * resumes control.
	 */
	if (ldev->zone_type == DL_ZONE_TYPE_LIGHTBAR) {
		if (drvdata->lightbar_direct_mode &&
		    mode != DL_EFFECT_STATIC && mode != DL_EFFECT_OFF &&
		    brightness > LED_OFF) {
			drvdata->lightbar_direct_mode = false;
			if (!drvdata->kbd_direct_mode) {
				memset(buf, 0, sizeof(buf));
				buf[0] = FEATURE_KBD_LED_REPORT_ID1;
				buf[1] = AURA_CMD_DIRECT;
				buf[2] = 0x00;
				ret = asus_aura_set_feature(drvdata, buf, sizeof(buf));
				if (ret < 0)
					return ret;
			}
		}
	} else {
		if (drvdata->kbd_direct_mode &&
		    mode != DL_EFFECT_STATIC && mode != DL_EFFECT_OFF &&
		    brightness > LED_OFF) {
			drvdata->kbd_direct_mode = false;
			if (!drvdata->lightbar_direct_mode) {
				memset(buf, 0, sizeof(buf));
				buf[0] = FEATURE_KBD_LED_REPORT_ID1;
				buf[1] = AURA_CMD_DIRECT;
				buf[2] = 0x00;
				ret = asus_aura_set_feature(drvdata, buf, sizeof(buf));
				if (ret < 0)
					return ret;
			}
		}
	}

	if (ldev->zone_type == DL_ZONE_TYPE_LIGHTBAR) {
		/*
		 * Static color / off: drive physical lightbar LEDs via the direct
		 * packet on channel 0x04.
		 */
		if (!drvdata->is_strix_4zone &&
		    (mode == DL_EFFECT_STATIC || mode == DL_EFFECT_OFF)) {
			unsigned int num_leds = drvdata->is_strix_4zone ?
				ROG_STRIX_4ZONE_LIGHTBAR_LEDS :
				ROG_STRIX_LIGHTBAR_LEDS;
			u8 lb_buf[ROG_STRIX_LIGHTBAR_BUF_SIZE];
			unsigned int i;

			for (i = 0; i < num_leds; i++) {
				lb_buf[i * 3 + 0] = r;
				lb_buf[i * 3 + 1] = g;
				lb_buf[i * 3 + 2] = b;
			}

			return asus_aura_lightbar_write_packet(drvdata, lb_buf,
							       num_leds * 3);
		}

		/*
		 * Animated hardware effects (breathing, rainbow, etc.):
		 * Program zone 6 (BAR_LEFT) and zone 7 (BAR_RIGHT) via 0xb3.
		 * Updates apply immediately on the lightbar without commit
		 * (0xb4/0xb5), and must not send trailing direct packets (0xbc).
		 */
		ret = asus_aura_write_zone_effect(drvdata, AURA_ZONE_BAR_LEFT,
						  aura_mode, r, g, b,
						  speed, direction, r2, g2, b2, false);
		if (ret < 0)
			return ret;

		ret = asus_aura_write_zone_effect(drvdata, AURA_ZONE_BAR_RIGHT,
						  aura_mode, r, g, b,
						  speed, direction, r2, g2, b2, false);
		if (ret < 0)
			return ret;

		return 0;
	}

	if (drvdata->has_dldev_lightbar) {
		unsigned int z;

		for (z = 1; z <= 4; z++) {
			ret = asus_aura_write_zone_effect(drvdata, (u8)z, aura_mode,
							  r, g, b, speed, direction,
							  r2, g2, b2, false);
			if (ret < 0)
				return ret;
		}

		ret = asus_aura_commit(drvdata);
		if (ret < 0)
			return ret;

		return 0;
	}

	return asus_aura_write_zone_effect(drvdata, AURA_ZONE_ALL, aura_mode, r, g, b,
					   speed, direction, r2, g2, b2, true);
}

static int asus_aura_set_effect(struct led_classdev_dynamic *ldev,
				enum dl_effect_mode mode)
{
	return asus_aura_apply_effect(ldev, mode, ldev->cdev.brightness);
}

static int asus_aura_set_speed(struct led_classdev_dynamic *ldev,
			       unsigned int speed)
{
	unsigned int old_speed = ldev->speed;
	int ret;

	ldev->speed = speed;
	ret = asus_aura_apply_effect(ldev, ldev->current_effect, ldev->cdev.brightness);
	if (ret < 0) {
		ldev->speed = old_speed;
		return ret;
	}

	return 0;
}

static int asus_aura_set_direction(struct led_classdev_dynamic *ldev,
				   enum dl_direction direction)
{
	enum dl_direction old_dir = ldev->direction;
	int ret;

	ldev->direction = direction;
	ret = asus_aura_apply_effect(ldev, ldev->current_effect, ldev->cdev.brightness);
	if (ret < 0) {
		ldev->direction = old_dir;
		return ret;
	}

	return 0;
}

static int asus_aura_set_palette(struct led_classdev_dynamic *ldev,
				 const struct dl_rgb *palette,
				 unsigned int num_entries)
{
	memcpy(ldev->palette, palette, num_entries * sizeof(*palette));
	ldev->num_palette_entries = num_entries;

	return asus_aura_apply_effect(ldev, ldev->current_effect, ldev->cdev.brightness);
}

static int asus_aura_brightness_set_blocking(struct led_classdev *cdev,
					     enum led_brightness brightness)
{
	struct led_classdev_dynamic *ldev = lcdev_to_dldev(cdev);

	if (brightness == LED_OFF)
		return asus_aura_apply_effect(ldev, DL_EFFECT_OFF, LED_OFF);

	return asus_aura_apply_effect(ldev, ldev->current_effect ? : DL_EFFECT_STATIC,
				      brightness);
}

static const struct led_dynamic_ops asus_aura_kbd_ops = {
	.set_effect	= asus_aura_set_effect,
	.set_speed	= asus_aura_set_speed,
	.set_direction	= asus_aura_set_direction,
	.set_palette	= asus_aura_set_palette,
	.direct_write	= asus_aura_strix_set_direct,
};

static const struct led_dynamic_ops asus_aura_kbd_4zone_ops = {
	.set_effect	= asus_aura_set_effect,
	.set_speed	= asus_aura_set_speed,
	.set_direction	= asus_aura_set_direction,
	.set_palette	= asus_aura_set_palette,
};

static const struct led_dynamic_ops asus_aura_lightbar_ops = {
	.set_effect	= asus_aura_set_effect,
	.set_speed	= asus_aura_set_speed,
	.set_direction	= asus_aura_set_direction,
	.set_palette	= asus_aura_set_palette,
	.direct_write	= asus_aura_lightbar_set_direct,
};

static const struct led_dynamic_ops asus_aura_lightbar_4zone_ops = {
	.set_effect	= asus_aura_set_effect,
	.set_speed	= asus_aura_set_speed,
	.set_direction	= asus_aura_set_direction,
	.set_palette	= asus_aura_set_palette,
};

static int asus_aura_discover(struct asus_drvdata *drvdata, bool *has_lightbar,
			      bool *is_strix_4zone)
{
	u8 buf[AURA_FEATURE_REPORT_SIZE] = {
		FEATURE_KBD_LED_REPORT_ID1,
		AURA_CMD_PROBE,
		0x20,
		0x31,
		0x00,
		0x20,
	};
	int ret;

	*has_lightbar = false;
	*is_strix_4zone = false;

	/*
	 * Query hardware configuration via Report 0x5D opcode 0x05:
	 * Returns byte 9 as layout (0x02 = 4-zone, 0x03 = per-key) and
	 * byte 13 as feature flags (bit 1 = lightbar present).
	 */
	ret = asus_aura_set_feature(drvdata, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	memset(buf, 0, sizeof(buf));
	buf[0] = FEATURE_KBD_LED_REPORT_ID1;
	ret = asus_aura_get_feature(drvdata, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	if (buf[1] != AURA_CMD_PROBE || buf[2] != 0x20 || buf[3] != 0x31)
		return -ENODEV;

	*is_strix_4zone = (buf[9] == 0x02);
	*has_lightbar = !!(buf[13] & 0x02);

	return 0;
}

static int asus_init_dynamic_lighting(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	struct usb_interface *intf;
	struct usb_device *udev;
	unsigned int i;
	u16 idProduct = 0;
	bool is_strix_direct = false;
	bool has_lightbar = false;
	int ret;

	if (!hid_is_usb(hdev))
		return 0;

	intf = to_usb_interface(hdev->dev.parent);
	udev = interface_to_usbdev(intf);
	idProduct = le16_to_cpu(udev->descriptor.idProduct);

	if (idProduct == USB_DEVICE_ID_ASUSTEK_ROG_NKEY_KEYBOARD2)
		is_strix_direct = true;

	ret = asus_aura_discover(drvdata, &has_lightbar, &drvdata->is_strix_4zone);
	if (ret == -ENODEV)
		return 0;
	if (ret < 0) {
		hid_warn(hdev, "Aura device discovery failed: %d\n", ret);
		return 0;
	}

	drvdata->kbd_r = 255;
	drvdata->kbd_g = 0;
	drvdata->kbd_b = 0;
	drvdata->lb_r = 255;
	drvdata->lb_g = 0;
	drvdata->lb_b = 0;

	for (i = 0; i < 4; i++) {
		drvdata->kbd_direct_buf[i * 3 + 0] = 255;
		drvdata->kbd_direct_buf[i * 3 + 1] = 0;
		drvdata->kbd_direct_buf[i * 3 + 2] = 0;
	}
	for (i = 0; i < (drvdata->is_strix_4zone ? ROG_STRIX_4ZONE_LIGHTBAR_LEDS :
						   ROG_STRIX_LIGHTBAR_LEDS); i++) {
		drvdata->lb_direct_buf[i * 3 + 0] = 255;
		drvdata->lb_direct_buf[i * 3 + 1] = 0;
		drvdata->lb_direct_buf[i * 3 + 2] = 0;
	}

	ret = asus_aura_wake_all_zones(drvdata);
	if (ret < 0)
		hid_warn(hdev, "Failed to wake Aura hardware zones: %d\n", ret);

	/* Initialize and register keyboard dynamic lighting zone */
	drvdata->dldev_kbd.cdev.name = "aura:keyboard";
	drvdata->dldev_kbd.cdev.max_brightness = 255;
	drvdata->dldev_kbd.cdev.brightness = 255;
	drvdata->dldev_kbd.cdev.brightness_set_blocking =
		asus_aura_brightness_set_blocking;
	drvdata->dldev_kbd.ops = drvdata->is_strix_4zone ?
		&asus_aura_kbd_4zone_ops : &asus_aura_kbd_ops;
	drvdata->dldev_kbd.driver_data = drvdata;
	drvdata->dldev_kbd.speed = 1;
	drvdata->dldev_kbd.max_speed = 2;
	drvdata->dldev_kbd.direction = DL_DIRECTION_RIGHT;
	drvdata->dldev_kbd.supported_directions = BIT(DL_DIRECTION_RIGHT) |
						  BIT(DL_DIRECTION_LEFT);
	drvdata->dldev_kbd.max_palette_entries = 2;
	drvdata->dldev_kbd.current_effect = DL_EFFECT_STATIC;
	drvdata->dldev_kbd.supported_effects = BIT(DL_EFFECT_OFF) |
					       BIT(DL_EFFECT_STATIC) |
					       BIT(DL_EFFECT_BREATHING) |
					       BIT(DL_EFFECT_STROBE) |
					       BIT(DL_EFFECT_SPECTRUM_CYCLE) |
					       BIT(DL_EFFECT_RAINBOW);

	if (is_strix_direct && !drvdata->is_strix_4zone) {
		drvdata->dldev_kbd.zone_type = DL_ZONE_TYPE_KEYBOARD_PER_KEY;
		drvdata->dldev_kbd.led_count = ROG_STRIX_DIRECT_LEDS;
		drvdata->dldev_kbd.supported_effects |= BIT(DL_EFFECT_DIRECT);
	} else if (drvdata->is_strix_4zone) {
		drvdata->dldev_kbd.zone_type = DL_ZONE_TYPE_GENERIC;
		drvdata->dldev_kbd.led_count = 4;
	} else {
		drvdata->dldev_kbd.zone_type = DL_ZONE_TYPE_GENERIC;
	}

	ret = devm_led_classdev_dynamic_register(&hdev->dev, &drvdata->dldev_kbd);
	if (ret < 0) {
		hid_warn(hdev, "Failed to register kbd dynamic lighting: %d\n", ret);
		return ret;
	}
	drvdata->has_dldev_kbd = true;

	/* Set initial default palette (#ff0000 ROG Red) */
	if (drvdata->dldev_kbd.palette) {
		drvdata->dldev_kbd.palette[0].r = 255;
		drvdata->dldev_kbd.palette[0].g = 0;
		drvdata->dldev_kbd.palette[0].b = 0;
		drvdata->dldev_kbd.num_palette_entries = 1;
	}

	/* Register chassis lightbar if detected or supported */
	if (has_lightbar) {
		drvdata->dldev_lightbar.cdev.name = "aura:lightbar";
		drvdata->dldev_lightbar.cdev.max_brightness = 255;
		drvdata->dldev_lightbar.cdev.brightness = 255;
		drvdata->dldev_lightbar.cdev.brightness_set_blocking =
			asus_aura_brightness_set_blocking;
		drvdata->dldev_lightbar.ops = drvdata->is_strix_4zone ?
			&asus_aura_lightbar_4zone_ops : &asus_aura_lightbar_ops;
		drvdata->dldev_lightbar.driver_data = drvdata;
		drvdata->dldev_lightbar.zone_type = DL_ZONE_TYPE_LIGHTBAR;
		drvdata->dldev_lightbar.led_count = drvdata->is_strix_4zone ?
			ROG_STRIX_4ZONE_LIGHTBAR_LEDS : ROG_STRIX_LIGHTBAR_LEDS;
		drvdata->dldev_lightbar.speed = 1;
		drvdata->dldev_lightbar.max_speed = 2;
		drvdata->dldev_lightbar.direction = DL_DIRECTION_RIGHT;
		drvdata->dldev_lightbar.supported_directions =
			BIT(DL_DIRECTION_RIGHT) | BIT(DL_DIRECTION_LEFT);
		drvdata->dldev_lightbar.max_palette_entries = 2;
		drvdata->dldev_lightbar.current_effect = DL_EFFECT_STATIC;
		drvdata->dldev_lightbar.supported_effects =
			BIT(DL_EFFECT_OFF) |
			BIT(DL_EFFECT_STATIC) |
			BIT(DL_EFFECT_BREATHING) |
			BIT(DL_EFFECT_STROBE) |
			BIT(DL_EFFECT_SPECTRUM_CYCLE) |
			BIT(DL_EFFECT_RAINBOW);
		if (!drvdata->is_strix_4zone)
			drvdata->dldev_lightbar.supported_effects |=
				BIT(DL_EFFECT_DIRECT);

		ret = devm_led_classdev_dynamic_register(&hdev->dev,
							 &drvdata->dldev_lightbar);
		if (ret < 0) {
			hid_warn(hdev, "Failed to register lightbar dynamic lighting: %d\n",
				 ret);
		} else {
			drvdata->has_dldev_lightbar = true;
			if (drvdata->dldev_lightbar.palette) {
				drvdata->dldev_lightbar.palette[0].r = 255;
				drvdata->dldev_lightbar.palette[0].g = 0;
				drvdata->dldev_lightbar.palette[0].b = 0;
				drvdata->dldev_lightbar.num_palette_entries = 1;
			}
		}
	}

	hid_info(hdev, "Registered dynamic lighting zones: kbd=%d, lightbar=%d\n",
		 drvdata->has_dldev_kbd, drvdata->has_dldev_lightbar);

	return 0;
}

#else /* !IS_REACHABLE(CONFIG_LEDS_CLASS_DYNAMIC) */

static inline int asus_init_dynamic_lighting(struct hid_device *hdev)
{
	return 0;
}

#endif /* IS_REACHABLE(CONFIG_LEDS_CLASS_DYNAMIC) */

/*
 * [0]       REPORT_ID (same value defined in report descriptor)
 * [1]	     rest battery level. range [0..255]
 * [2]..[7]  Bluetooth hardware address (MAC address)
 * [8]       charging status
 *            = 0 : AC offline / discharging
 *            = 1 : AC online  / charging
 *            = 2 : AC online  / fully charged
 */
static int asus_parse_battery(struct asus_drvdata *drvdata, u8 *data, int size)
{
	u8 sts;
	u8 lvl;
	int val;

	lvl = data[1];
	sts = data[8];

	drvdata->battery_capacity = ((int)lvl * 100) / (int)BATTERY_LEVEL_MAX;

	switch (sts) {
	case BATTERY_STAT_CHARGING:
		val = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case BATTERY_STAT_FULL:
		val = POWER_SUPPLY_STATUS_FULL;
		break;
	case BATTERY_STAT_DISCONNECT:
	default:
		val = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	}
	drvdata->battery_stat = val;

	return 0;
}

static int asus_report_battery(struct asus_drvdata *drvdata, u8 *data, int size)
{
	/* notify only the autonomous event by device */
	if ((drvdata->battery_in_query == false) &&
			 (size == BATTERY_REPORT_SIZE))
		power_supply_changed(drvdata->battery);

	return 0;
}

static int asus_battery_query(struct asus_drvdata *drvdata)
{
	u8 *buf;
	int ret = 0;

	buf = kmalloc(BATTERY_REPORT_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	drvdata->battery_in_query = true;
	ret = hid_hw_raw_request(drvdata->hdev, BATTERY_REPORT_ID,
				buf, BATTERY_REPORT_SIZE,
				HID_INPUT_REPORT, HID_REQ_GET_REPORT);
	drvdata->battery_in_query = false;
	if (ret == BATTERY_REPORT_SIZE)
		ret = asus_parse_battery(drvdata, buf, BATTERY_REPORT_SIZE);
	else
		ret = -ENODATA;

	kfree(buf);

	return ret;
}

static enum power_supply_property asus_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

#define	QUERY_MIN_INTERVAL	(60 * HZ)	/* 60[sec] */

static int asus_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct asus_drvdata *drvdata = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CAPACITY:
		if (time_before(drvdata->battery_next_query, jiffies)) {
			drvdata->battery_next_query =
					 jiffies + QUERY_MIN_INTERVAL;
			ret = asus_battery_query(drvdata);
			if (ret)
				return ret;
		}
		if (psp == POWER_SUPPLY_PROP_STATUS)
			val->intval = drvdata->battery_stat;
		else
			val->intval = drvdata->battery_capacity;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = drvdata->hdev->name;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int asus_battery_probe(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	struct power_supply_config pscfg = { .drv_data = drvdata };
	int ret = 0;

	drvdata->battery_capacity = 0;
	drvdata->battery_stat = POWER_SUPPLY_STATUS_UNKNOWN;
	drvdata->battery_in_query = false;

	drvdata->battery_desc.properties = asus_battery_props;
	drvdata->battery_desc.num_properties = ARRAY_SIZE(asus_battery_props);
	drvdata->battery_desc.get_property = asus_battery_get_property;
	drvdata->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	drvdata->battery_desc.use_for_apm = 0;
	drvdata->battery_desc.name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
					"asus-keyboard-%s-battery",
					strlen(hdev->uniq) ?
					hdev->uniq : dev_name(&hdev->dev));
	if (!drvdata->battery_desc.name)
		return -ENOMEM;

	drvdata->battery_next_query = jiffies;

	drvdata->battery = devm_power_supply_register(&hdev->dev,
				&(drvdata->battery_desc), &pscfg);
	if (IS_ERR(drvdata->battery)) {
		ret = PTR_ERR(drvdata->battery);
		drvdata->battery = NULL;
		hid_err(hdev, "Unable to register battery device\n");
		return ret;
	}

	power_supply_powers(drvdata->battery, &hdev->dev);

	return ret;
}

static int asus_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	struct input_dev *input = hi->input;
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	/* T100CHI uses MULTI_INPUT, bind the touchpad to the mouse hid_input */
	if (drvdata->quirks & QUIRK_T100CHI &&
	    hi->report->id != T100CHI_MOUSE_REPORT_ID)
		return 0;

	/* Handle MULTI_INPUT on E1239T mouse/touchpad USB interface */
	if (drvdata->tp && (drvdata->quirks & QUIRK_MEDION_E1239T)) {
		switch (hi->report->id) {
		case E1239T_TP_TOGGLE_REPORT_ID:
			input_set_capability(input, EV_KEY, KEY_F21);
			input->name = "Asus Touchpad Keys";
			drvdata->tp_kbd_input = input;
			return 0;
		case INPUT_REPORT_ID:
			break; /* Touchpad report, handled below */
		default:
			return 0; /* Ignore other reports */
		}
	}

	if (drvdata->tp) {
		int ret;

		input_set_abs_params(input, ABS_MT_POSITION_X, 0,
				     drvdata->tp->max_x, 0, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y, 0,
				     drvdata->tp->max_y, 0, 0);
		input_abs_set_res(input, ABS_MT_POSITION_X, drvdata->tp->res_x);
		input_abs_set_res(input, ABS_MT_POSITION_Y, drvdata->tp->res_y);

		if (drvdata->tp->contact_size >= 5) {
			input_set_abs_params(input, ABS_TOOL_WIDTH, 0,
					     MAX_TOUCH_MAJOR, 0, 0);
			input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0,
					     MAX_TOUCH_MAJOR, 0, 0);
			input_set_abs_params(input, ABS_MT_PRESSURE, 0,
					      MAX_PRESSURE, 0, 0);
		}

		__set_bit(BTN_LEFT, input->keybit);
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

		ret = input_mt_init_slots(input, drvdata->tp->max_contacts,
					  INPUT_MT_POINTER);

		if (ret) {
			hid_err(hdev, "Asus input mt init slots failed: %d\n", ret);
			return ret;
		}
	}

	drvdata->input = input;

	if ((drvdata->quirks & QUIRK_HID_FN_LOCK) &&
	    (asus_kbd_fn_lock_set(drvdata, true)))
		hid_warn(hdev, "Error while setting FN lock to ON\n");

	return 0;
}

#define asus_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, \
						    max, EV_KEY, (c))
static int asus_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit,
		int *max)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_SKIP_INPUT_MAPPING) {
		/* Don't map anything from the HID report.
		 * We do it all manually in asus_input_configured
		 */
		return -1;
	}

	/*
	 * Ignore a bunch of bogus collections in the T100CHI descriptor.
	 * This avoids a bunch of non-functional hid_input devices getting
	 * created because of the T100CHI using HID_QUIRK_MULTI_INPUT.
	 */
	if ((drvdata->quirks & (QUIRK_T100CHI | QUIRK_T90CHI)) &&
	    (field->application == (HID_UP_GENDESK | 0x0080) ||
	     field->application == HID_GD_MOUSE ||
	     usage->hid == (HID_UP_GENDEVCTRLS | 0x0024) ||
	     usage->hid == (HID_UP_GENDEVCTRLS | 0x0025) ||
	     usage->hid == (HID_UP_GENDEVCTRLS | 0x0026)))
		return -1;

	/* ASUS-specific keyboard hotkeys and led backlight */
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_ASUSVENDOR) {
		switch (usage->hid & HID_USAGE) {
		case 0x10: asus_map_key_clear(KEY_BRIGHTNESSDOWN);	break;
		case 0x20: asus_map_key_clear(KEY_BRIGHTNESSUP);		break;
		case 0x35: asus_map_key_clear(KEY_DISPLAY_OFF);		break;
		case 0x6c: asus_map_key_clear(KEY_SLEEP);		break;
		case 0x7c: asus_map_key_clear(KEY_MICMUTE);		break;
		case 0x82: asus_map_key_clear(KEY_CAMERA);		break;
		case 0x85: asus_map_key_clear(KEY_CAMERA);		break;
		case 0x86: asus_map_key_clear(KEY_PROG1);	break; /* MyASUS key */
		case 0x88: asus_map_key_clear(KEY_RFKILL);			break;
		case 0xb5: asus_map_key_clear(KEY_CALC);			break;
		case 0xc4: asus_map_key_clear(KEY_KBDILLUMUP);		break;
		case 0xc5: asus_map_key_clear(KEY_KBDILLUMDOWN);		break;
		case 0xc7: asus_map_key_clear(KEY_KBDILLUMTOGGLE);	break;
		case 0x4e: asus_map_key_clear(KEY_FN_ESC);		break;
		case 0x7e: asus_map_key_clear(KEY_EMOJI_PICKER);	break;

		case 0x8b: asus_map_key_clear(KEY_PROG1);	break; /* ProArt Creator Hub key */
		case 0x5f: asus_map_key_clear(KEY_PROG2);	break; /* S-shaped programmable key */
		case 0x6b: asus_map_key_clear(KEY_F21);		break; /* ASUS touchpad toggle */
		case 0x38: asus_map_key_clear(KEY_PROG1);	break; /* ROG key */
		case 0xba: asus_map_key_clear(KEY_PROG2);	break; /* Fn+C ASUS Splendid */
		case 0x5c: asus_map_key_clear(KEY_PROG3);	break; /* Fn+Space Power4Gear */
		case 0x99: asus_map_key_clear(KEY_PROG4);	break; /* Fn+F5 "fan" symbol */
		case 0xae: asus_map_key_clear(KEY_PROG4);	break; /* Fn+F5 "fan" symbol */
		case 0x92: asus_map_key_clear(KEY_CALC);	break; /* Fn+Ret "Calc" symbol */
		case 0xb2: asus_map_key_clear(KEY_PROG2);	break; /* Fn+Left previous aura */
		case 0xb3: asus_map_key_clear(KEY_PROG3);	break; /* Fn+Left next aura */
		case 0x6a: asus_map_key_clear(KEY_F13);		break; /* Screenpad toggle */
		case 0x4b: asus_map_key_clear(KEY_F14);		break; /* Arrows/Pg-Up/Dn toggle */
		case 0xa5: asus_map_key_clear(KEY_F15);		break; /* ROG Ally left back */
		case 0xa6: asus_map_key_clear(KEY_F16);		break; /* ROG Ally QAM button */
		case 0xa7: asus_map_key_clear(KEY_F17);		break; /* ROG Ally ROG long-press */
		case 0xa8: asus_map_key_clear(KEY_F18);		break; /* ROG Ally ROG long-press-release */
		case 0x9c: asus_map_key_clear(KEY_F19);		break; /* Zephyrus Duo tent mode */

		default:
			/* ASUS lazily declares 256 usages, ignore the rest,
			 * as some make the keyboard appear as a pointer device. */
			return -1;
		}

		set_bit(EV_REP, hi->input->evbit);
		return 1;
	}

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_MSVENDOR) {
		switch (usage->hid & HID_USAGE) {
		case 0xff01: asus_map_key_clear(BTN_1);	break;
		case 0xff02: asus_map_key_clear(BTN_2);	break;
		case 0xff03: asus_map_key_clear(BTN_3);	break;
		case 0xff04: asus_map_key_clear(BTN_4);	break;
		case 0xff05: asus_map_key_clear(BTN_5);	break;
		case 0xff06: asus_map_key_clear(BTN_6);	break;
		case 0xff07: asus_map_key_clear(BTN_7);	break;
		case 0xff08: asus_map_key_clear(BTN_8);	break;
		case 0xff09: asus_map_key_clear(BTN_9);	break;
		case 0xff0a: asus_map_key_clear(BTN_A);	break;
		case 0xff0b: asus_map_key_clear(BTN_B);	break;
		case 0x00f1: asus_map_key_clear(KEY_WLAN);	break;
		case 0x00f2: asus_map_key_clear(KEY_BRIGHTNESSDOWN);	break;
		case 0x00f3: asus_map_key_clear(KEY_BRIGHTNESSUP);	break;
		case 0x00f4: asus_map_key_clear(KEY_DISPLAY_OFF);	break;
		case 0x00f7: asus_map_key_clear(KEY_CAMERA);	break;
		case 0x00f8: asus_map_key_clear(KEY_PROG1);	break;
		default:
			return 0;
		}

		set_bit(EV_REP, hi->input->evbit);
		return 1;
	}

	if (drvdata->quirks & QUIRK_NO_CONSUMER_USAGES &&
		(usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER) {
		switch (usage->hid & HID_USAGE) {
		case 0xe2: /* Mute */
		case 0xe9: /* Volume up */
		case 0xea: /* Volume down */
			return 0;
		default:
			/* Ignore dummy Consumer usages which make the
			 * keyboard incorrectly appear as a pointer device.
			 */
			return -1;
		}
	}

	/*
	 * The mute button is broken and only sends press events, we
	 * deal with this in our raw_event handler, so do not map it.
	 */
	if ((drvdata->quirks & QUIRK_MEDION_E1239T) &&
	    usage->hid == (HID_UP_CONSUMER | 0xe2)) {
		input_set_capability(hi->input, EV_KEY, KEY_MUTE);
		return -1;
	}

	return 0;
}

static int asus_start_multitouch(struct hid_device *hdev)
{
	int ret;
	static const unsigned char buf[] = {
		FEATURE_REPORT_ID, 0x00, 0x03, 0x01, 0x00
	};
	unsigned char *dmabuf = kmemdup(buf, sizeof(buf), GFP_KERNEL);

	if (!dmabuf) {
		ret = -ENOMEM;
		hid_err(hdev, "Asus failed to alloc dma buf: %d\n", ret);
		return ret;
	}

	ret = hid_hw_raw_request(hdev, dmabuf[0], dmabuf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	kfree(dmabuf);

	if (ret != sizeof(buf)) {
		hid_err(hdev, "Asus failed to start multitouch: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __maybe_unused asus_resume(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	/*
	 * If we have a backlight listener registered, restore the previous state,
	 * in case of error do not fail: most models restore the backlight
	 * automatically, and the error is non-fatal.
	 */
	if (drvdata->listener.brightness_set)
		asus_kbd_backlight_set(&drvdata->listener, drvdata->kbd_backlight_brightness);

	return 0;
}

static int __maybe_unused asus_reset_resume(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->tp)
		return asus_start_multitouch(hdev);

	return 0;
}

static int asus_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hid_report_enum *rep_enum;
	struct asus_drvdata *drvdata;
	struct hid_report *rep;
	bool is_vendor = false;
	int ret;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	hid_set_drvdata(hdev, drvdata);

#if IS_REACHABLE(CONFIG_LEDS_CLASS_DYNAMIC)
	ret = devm_mutex_init(&hdev->dev, &drvdata->aura_lock);
	if (ret)
		return ret;

	drvdata->aura_buf = devm_kzalloc(&hdev->dev, AURA_FEATURE_REPORT_SIZE,
					 GFP_KERNEL);
	if (!drvdata->aura_buf)
		return -ENOMEM;
#endif

	drvdata->quirks = id->driver_data;

	/*
	 * T90CHI's keyboard dock returns same ID values as T100CHI's dock.
	 * Thus, identify T90CHI dock with product name string.
	 */
	if (strstr(hdev->name, "T90CHI")) {
		drvdata->quirks &= ~QUIRK_T100CHI;
		drvdata->quirks |= QUIRK_T90CHI;
	}

	if (drvdata->quirks & QUIRK_IS_MULTITOUCH)
		drvdata->tp = &asus_i2c_tp;

	if ((drvdata->quirks & QUIRK_T100_KEYBOARD) && hid_is_usb(hdev)) {
		struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

		if (intf->altsetting->desc.bInterfaceNumber == T100_TPAD_INTF) {
			drvdata->quirks = QUIRK_SKIP_INPUT_MAPPING;
			/*
			 * The T100HA uses the same USB-ids as the T100TAF and
			 * the T200TA uses the same USB-ids as the T100TA, while
			 * both have different max x/y values as the T100TA[F].
			 */
			if (dmi_match(DMI_PRODUCT_NAME, "T100HAN"))
				drvdata->tp = &asus_t100ha_tp;
			else if (dmi_match(DMI_PRODUCT_NAME, "T200TA"))
				drvdata->tp = &asus_t200ta_tp;
			else
				drvdata->tp = &asus_t100ta_tp;
		}
	}

	if (drvdata->quirks & QUIRK_T100CHI) {
		/*
		 * All functionality is on a single HID interface and for
		 * userspace the touchpad must be a separate input_dev.
		 */
		hdev->quirks |= HID_QUIRK_MULTI_INPUT;
		drvdata->tp = &asus_t100chi_tp;
	}

	if ((drvdata->quirks & QUIRK_MEDION_E1239T) && hid_is_usb(hdev)) {
		struct usb_host_interface *alt =
			to_usb_interface(hdev->dev.parent)->altsetting;

		if (alt->desc.bInterfaceNumber == MEDION_E1239T_TPAD_INTF) {
			/* For separate input-devs for tp and tp toggle key */
			hdev->quirks |= HID_QUIRK_MULTI_INPUT;
			drvdata->quirks |= QUIRK_SKIP_INPUT_MAPPING;
			drvdata->tp = &medion_e1239t_tp;
		}
	}

	if (drvdata->quirks & QUIRK_NO_INIT_REPORTS)
		hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

	drvdata->hdev = hdev;

	if (drvdata->quirks & (QUIRK_T100CHI | QUIRK_T90CHI)) {
		ret = asus_battery_probe(hdev);
		if (ret) {
			hid_err(hdev,
			    "Asus hid battery_probe failed: %d\n", ret);
			return ret;
		}
	}

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Asus hid parse failed: %d\n", ret);
		return ret;
	}

	/* Check for vendor for RGB init and handle generic devices properly. */
	rep_enum = &hdev->report_enum[HID_INPUT_REPORT];
	list_for_each_entry(rep, &rep_enum->report_list, list) {
		if ((rep->application & HID_USAGE_PAGE) == HID_UP_ASUSVENDOR)
			is_vendor = true;
	}

	/*
	 * A vendor collection may be the only application collection on the
	 * interface, which hidinput_connect() otherwise skips, leaving the
	 * hotkey usages unmapped. Unpopulated inputs are dropped later.
	 */
	if (is_vendor && (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD))
		hdev->quirks |= HID_QUIRK_HIDINPUT_FORCE;

	ret = asus_worker_create(hdev, drvdata);
	if (ret) {
		hid_warn(hdev, "Failed to initialize worker: %d\n", ret);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		asus_worker_stop(drvdata->worker);
		hid_err(hdev, "Asus hw start failed: %d\n", ret);
		return ret;
	}

	if (!drvdata->tp) {
		for (int r = 0; r < ARRAY_SIZE(asus_report_id_init); r++) {
			if (asus_has_report_id(hdev, asus_report_id_init[r])) {
				ret = asus_kbd_init(hdev, asus_report_id_init[r]);
				if (ret < 0)
					hid_warn(hdev, "Failed to initialize 0x%x: %d.\n",
						 asus_report_id_init[r], ret);
			}
		}
	}

	/* Laptops keyboard backlight is always at 0x5a */
	if (is_vendor && (drvdata->quirks & QUIRK_USE_KBD_BACKLIGHT) &&
	    (asus_has_report_id(hdev, FEATURE_KBD_REPORT_ID)) &&
		(asus_kbd_register_leds(hdev)))
		hid_warn(hdev, "Failed to initialize backlight.\n");

	if (asus_has_report_id(hdev, FEATURE_KBD_LED_REPORT_ID1)) {
		ret = asus_init_dynamic_lighting(hdev);
		if (ret < 0)
			hid_warn(hdev, "Failed to initialize dynamic lighting: %d\n", ret);
	}

	/*
	 * For ROG keyboards, skip rename for consistency and ->input check as
	 * some devices do not have inputs.
	 */
	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD)
		return 0;

	/*
	 * Check that input registration succeeded. Checking that
	 * HID_CLAIMED_INPUT is set prevents a UAF when all input devices
	 * were freed during registration due to no usages being mapped,
	 * leaving drvdata->input pointing to freed memory.
	 */
	if (drvdata->input && (hdev->claimed & HID_CLAIMED_INPUT)) {
		if (drvdata->tp)
			drvdata->input->name = "Asus TouchPad";
		else
			drvdata->input->name = "Asus Keyboard";

		if (drvdata->tp) {
			ret = asus_start_multitouch(hdev);
			if (ret)
				goto err_stop_hw;
		}
	}

	return 0;
err_stop_hw:
	if (drvdata->listener.brightness_set)
		asus_hid_unregister_listener(&drvdata->listener);

	asus_worker_stop(drvdata->worker);
	hid_hw_stop(hdev);
	return ret;
}

static void asus_remove(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->listener.brightness_set)
		asus_hid_unregister_listener(&drvdata->listener);

	asus_worker_stop(drvdata->worker);
	hid_hw_stop(hdev);
}

static const __u8 asus_g752_fixed_rdesc[] = {
        0x19, 0x00,			/*   Usage Minimum (0x00)       */
        0x2A, 0xFF, 0x00,		/*   Usage Maximum (0xFF)       */
};

static const __u8 *asus_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_FIX_NOTEBOOK_REPORT &&
			*rsize >= 56 && rdesc[54] == 0x25 && rdesc[55] == 0x65) {
		hid_info(hdev, "Fixing up Asus notebook report descriptor\n");
		rdesc[55] = 0xdd;
	}
	/* For the T100TA/T200TA keyboard dock */
	if (drvdata->quirks & QUIRK_T100_KEYBOARD &&
		 (*rsize == 76 || *rsize == 101) &&
		 rdesc[73] == 0x81 && rdesc[74] == 0x01) {
		hid_info(hdev, "Fixing up Asus T100 keyb report descriptor\n");
		rdesc[74] &= ~HID_MAIN_ITEM_CONSTANT;
	}
	/* For the T100CHI/T90CHI keyboard dock */
	if (drvdata->quirks & (QUIRK_T100CHI | QUIRK_T90CHI)) {
		int rsize_orig;
		int offs;

		if (drvdata->quirks & QUIRK_T100CHI) {
			rsize_orig = 403;
			offs = 388;
		} else {
			rsize_orig = 306;
			offs = 291;
		}

		/*
		 * Change Usage (76h) to Usage Minimum (00h), Usage Maximum
		 * (FFh) and clear the flags in the Input() byte.
		 * Note the descriptor has a bogus 0 byte at the end so we
		 * only need 1 extra byte.
		 */
		if (*rsize == rsize_orig &&
			rdesc[offs] == 0x09 && rdesc[offs + 1] == 0x76) {
			__u8 *new_rdesc;

			new_rdesc = devm_kzalloc(&hdev->dev, rsize_orig + 1,
						 GFP_KERNEL);
			if (!new_rdesc)
				return rdesc;

			hid_info(hdev, "Fixing up %s keyb report descriptor\n",
				drvdata->quirks & QUIRK_T100CHI ?
				"T100CHI" : "T90CHI");

			memcpy(new_rdesc, rdesc, rsize_orig);
			*rsize = rsize_orig + 1;
			rdesc = new_rdesc;

			memmove(rdesc + offs + 4, rdesc + offs + 2, 12);
			rdesc[offs] = 0x19;
			rdesc[offs + 1] = 0x00;
			rdesc[offs + 2] = 0x29;
			rdesc[offs + 3] = 0xff;
			rdesc[offs + 14] = 0x00;
		}
	}

	if (drvdata->quirks & QUIRK_G752_KEYBOARD &&
		 *rsize == 75 && rdesc[61] == 0x15 && rdesc[62] == 0x00) {
		/* report is missing usage minimum and maximum */
		__u8 *new_rdesc;
		size_t new_size = *rsize + sizeof(asus_g752_fixed_rdesc);

		new_rdesc = devm_kzalloc(&hdev->dev, new_size, GFP_KERNEL);
		if (new_rdesc == NULL)
			return rdesc;

		hid_info(hdev, "Fixing up Asus G752 keyb report descriptor\n");
		/* copy the valid part */
		memcpy(new_rdesc, rdesc, 61);
		/* insert missing part */
		memcpy(new_rdesc + 61, asus_g752_fixed_rdesc, sizeof(asus_g752_fixed_rdesc));
		/* copy remaining data */
		memcpy(new_rdesc + 61 + sizeof(asus_g752_fixed_rdesc), rdesc + 61, *rsize - 61);

		*rsize = new_size;
		rdesc = new_rdesc;
	}

	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD &&
			*rsize == 331 && rdesc[190] == 0x85 && rdesc[191] == 0x5a &&
			rdesc[204] == 0x95 && rdesc[205] == 0x05) {
		hid_info(hdev, "Fixing up Asus N-KEY keyb report descriptor\n");
		rdesc[205] = 0x01;
	}

	/* match many more n-key devices */
	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD && *rsize > 15) {
		for (int i = 0; i < *rsize - 15; i++) {
			/* offset to the count from 0x5a report part always 14 */
			if (rdesc[i] == 0x85 && rdesc[i + 1] == 0x5a &&
			    rdesc[i + 14] == 0x95 && rdesc[i + 15] == 0x05) {
				hid_info(hdev, "Fixing up Asus N-Key report descriptor\n");
				rdesc[i + 15] = 0x01;
				break;
			}
		}
	}

	return rdesc;
}

static const struct hid_device_id asus_devices[] = {
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_I2C_KEYBOARD), I2C_KEYBOARD_QUIRKS},
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_I2C_ZENBOOK_KEYBOARD),
	  I2C_KEYBOARD_QUIRKS | QUIRK_FILTER_CAMERA_COMPANION },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_I2C_TOUCHPAD), I2C_TOUCHPAD_QUIRKS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_ROG_KEYBOARD1), QUIRK_USE_KBD_BACKLIGHT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_ROG_KEYBOARD2), QUIRK_USE_KBD_BACKLIGHT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_ROG_KEYBOARD3), QUIRK_G752_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_FX503VD_KEYBOARD),
	  QUIRK_USE_KBD_BACKLIGHT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_KEYBOARD),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_KEYBOARD2),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD | QUIRK_HID_FN_LOCK },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_KEYBOARD2),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD | QUIRK_HID_FN_LOCK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_KEYBOARD3),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_Z13_LIGHTBAR),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_ALLY),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD | QUIRK_ROG_ALLY_XPAD},
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_ALLY_X),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD | QUIRK_ROG_ALLY_XPAD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_XGM_2022),
	},
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_XGM_2023),
	},
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_CLAYMORE_II_KEYBOARD),
	  QUIRK_ROG_CLAYMORE_II_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_T100TA_KEYBOARD),
	  QUIRK_T100_KEYBOARD | QUIRK_NO_CONSUMER_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_T100TAF_KEYBOARD),
	  QUIRK_T100_KEYBOARD | QUIRK_NO_CONSUMER_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_ASUS_AK1D) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_TURBOX, USB_DEVICE_ID_ASUS_MD_5110) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_JESS, USB_DEVICE_ID_ASUS_MD_5112) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_T100CHI_KEYBOARD), QUIRK_T100CHI },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ITE, USB_DEVICE_ID_ITE_MEDION_E1239T),
		QUIRK_MEDION_E1239T },
	/*
	 * Note bind to the HID_GROUP_GENERIC group, so that we only bind to the keyboard
	 * part, while letting hid-multitouch.c handle the touchpad.
	 */
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		USB_VENDOR_ID_ASUSTEK, USB_DEVICE_ID_ASUSTEK_ROG_Z13_FOLIO),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_DEVICE(BUS_BLUETOOTH, HID_GROUP_GENERIC,
		USB_VENDOR_ID_ASUSTEK, USB_DEVICE_ID_ASUSTEK_ROG_NKEY_KEYBOARD3_BT),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		USB_VENDOR_ID_ASUSTEK, USB_DEVICE_ID_ASUSTEK_T101HA_KEYBOARD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, asus_devices);

static struct hid_driver asus_driver = {
	.name			= "asus",
	.id_table		= asus_devices,
	.report_fixup		= asus_report_fixup,
	.probe                  = asus_probe,
	.remove			= asus_remove,
	.input_mapping          = asus_input_mapping,
	.input_configured       = asus_input_configured,
	.reset_resume           = pm_ptr(asus_reset_resume),
	.resume			= pm_ptr(asus_resume),
	.event			= asus_event,
	.raw_event		= asus_raw_event
};
module_hid_driver(asus_driver);

MODULE_IMPORT_NS("ASUS_WMI");
MODULE_LICENSE("GPL");
