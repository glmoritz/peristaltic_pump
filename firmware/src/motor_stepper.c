/*
 * Motor backend for the device: TMC2209 (enable) + Zephyr's CPU step/dir
 * motion controller timed by a hardware timer (boards/esp32c3_supermini.overlay).
 *
 * Phase 1 (design.md §5.1): the current is set on the module's VREF pot and
 * the microsteps by its MS1/MS2 strapping. Phase 2 adds UART register access.
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/stepper/stepper.h>
#include <zephyr/drivers/stepper/stepper_ctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "hw.h"

LOG_MODULE_REGISTER(motor, LOG_LEVEL_INF);

static const struct device *const drv = DEVICE_DT_GET(DT_ALIAS(pump_driver));
static const struct device *const motion = DEVICE_DT_GET(DT_ALIAS(pump_motion));

static bool running;
static bool enabled;
static int64_t pos_base;

/* The controller keeps an int32 position; rebase it long before it wraps. */
#define REBASE_LIMIT (1 << 30)

int motor_hw_init(void)
{
	if (!device_is_ready(drv) || !device_is_ready(motion)) {
		LOG_ERR("stepper devices not ready");
		return -ENODEV;
	}
	return stepper_ctrl_set_reference_position(motion, 0);
}

int motor_hw_set_speed(float usteps_per_s)
{
	int err;

	if (usteps_per_s <= 0.0f) {
		if (running) {
			err = stepper_ctrl_stop(motion);
			if (err) {
				return err;
			}
			running = false;
		}
		if (enabled) {
			/* De-energise: no hold current, no heat in the wall. */
			err = stepper_disable(drv);
			if (err) {
				return err;
			}
			enabled = false;
		}
		return 0;
	}

	if (!enabled) {
		err = stepper_enable(drv);
		if (err) {
			return err;
		}
		enabled = true;
	}

	err = stepper_ctrl_set_microstep_interval(motion,
						  (uint64_t)(1e9f / usteps_per_s));
	if (err) {
		return err;
	}

	if (!running) {
		err = stepper_ctrl_run(motion, STEPPER_CTRL_DIRECTION_POSITIVE);
		if (err) {
			return err;
		}
		running = true;
	}
	return 0;
}

int64_t motor_hw_position(void)
{
	int32_t pos = 0;

	if (stepper_ctrl_get_actual_position(motion, &pos) != 0) {
		return pos_base;
	}
	if (pos > REBASE_LIMIT || pos < -REBASE_LIMIT) {
		/* A few microsteps may slip between the read and the reset. */
		pos_base += pos;
		(void)stepper_ctrl_set_reference_position(motion, 0);
		pos = 0;
	}
	return pos_base + pos;
}
