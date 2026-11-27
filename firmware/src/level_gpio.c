/* Level sensor backend for the device: two GPIOs from zephyr,user. */

#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "hw.h"

LOG_MODULE_REGISTER(level_gpio, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec low = GPIO_DT_SPEC_GET(USER_NODE, level_low_gpios);
static const struct gpio_dt_spec high = GPIO_DT_SPEC_GET(USER_NODE, level_high_gpios);

int level_hw_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&low) || !gpio_is_ready_dt(&high)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&low, GPIO_INPUT);
	err = err ?: gpio_pin_configure_dt(&high, GPIO_INPUT);
	if (err) {
		LOG_ERR("sensor GPIO config failed (%d)", err);
	}
	return err;
}

void level_hw_read(bool *low_wet, bool *high_wet)
{
	/* Logical level: the overlay's ACTIVE_LOW/HIGH flag maps it to "wet". */
	*low_wet = gpio_pin_get_dt(&low) > 0;
	*high_wet = gpio_pin_get_dt(&high) > 0;
}
