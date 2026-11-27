/*
 * Peristaltic condensate pump: control loop.
 *
 * Every CONFIG_PUMP_TICK_MS: read the debounced sensors, advance the motor
 * ramp, run the level controller (control.c) and command the pump. The loop
 * is fully local; the network (later) only observes and tunes it.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.h"
#include "control.h"
#include "level.h"
#include "params.h"
#include "pump.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define QHAT_SAVE_MIN_MS   (10 * 60 * 1000) /* flash wear: q̂ at most every 10 min */
#define TOTALS_SAVE_MS     (60 * 60 * 1000) /* counters hourly */
#define MANUAL_MAX_S       3600

#define USER_NODE DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(USER_NODE, buzzer_gpios)
#define HAVE_BUZZER 1
static const struct gpio_dt_spec buzzer = GPIO_DT_SPEC_GET(USER_NODE, buzzer_gpios);
#endif

static struct ctl ctl;
static struct k_spinlock lock;
static struct app_status status;

/* Requests from other threads, taken under the lock. */
static float manual_q = -1.0f;
static int64_t manual_until_ms;
static bool req_hose_reset;
static bool req_save;

/* Counters persisted across boots = value at boot + this boot's share. */
static double total_at_boot_ml;
static double hose_at_boot_revs;

void app_get_status(struct app_status *st)
{
	K_SPINLOCK(&lock) {
		*st = status;
	}
}

void app_manual(float q_mlh, uint32_t seconds)
{
	if (seconds == 0 || seconds > MANUAL_MAX_S) {
		seconds = MANUAL_MAX_S;
	}
	K_SPINLOCK(&lock) {
		manual_q = (q_mlh > 0.0f) ? q_mlh : 0.0f;
		manual_until_ms = k_uptime_get() + (int64_t)seconds * 1000;
	}
}

void app_auto(void)
{
	K_SPINLOCK(&lock) {
		manual_q = -1.0f;
	}
}

void app_hose_replaced(void)
{
	K_SPINLOCK(&lock) {
		req_hose_reset = true;
	}
}

void app_save(void)
{
	K_SPINLOCK(&lock) {
		req_save = true;
	}
}

static void log_events(const struct ctl *c, uint32_t ev, const struct ctl_out *out)
{
	if (ev & CTL_EV_MEASURED) {
		LOG_INF("inflow measured %.0f mL/h -> q_hat %.0f mL/h (#%u)",
			(double)c->last_meas_mlh, (double)c->q_hat_mlh, c->n_measurements);
	}
	if (ev & CTL_EV_BOOST) {
		LOG_WRN("level high for long: q_hat raised to %.0f mL/h", (double)c->q_hat_mlh);
	}
	if (ev & CTL_EV_STATE) {
		LOG_INF("state %s, q %.0f mL/h (q_hat %.0f)", ctl_state_name(c->state),
			(double)out->q_cmd_mlh, (double)c->q_hat_mlh);
	}
	if (ev & CTL_EV_EMERGENCY) {
		LOG_WRN("EMERGENCY: full speed until the low sensor is dry");
	}
	if (ev & CTL_EV_PROBLEM_ON) {
		LOG_ERR("PROBLEM: %s", c->sensor_fault ? "sensor fault (L dry, H wet)"
						       : "pump cannot clear the high level");
	}
	if (ev & CTL_EV_PROBLEM_OFF) {
		LOG_INF("problem cleared");
	}
}

static void buzzer_update(int64_t now, bool alarm)
{
#ifdef HAVE_BUZZER
	/* 200 ms beep every 5 s while the alarm is on. */
	gpio_pin_set_dt(&buzzer, alarm && (now % 5000) < 200);
#else
	ARG_UNUSED(now);
	ARG_UNUSED(alarm);
#endif
}

int main(void)
{
	struct ctl_out out;
	int64_t last_qhat_save = 0;
	int64_t last_totals_save = k_uptime_get();
	bool qhat_dirty = false;
	bool hose_warned = false;
	int err;

	LOG_INF("peristaltic pump firmware, %s",
		IS_ENABLED(CONFIG_PUMP_SIM) ? "SIMULATED reservoir and motor" : CONFIG_BOARD);

	(void)params_init(); /* defaults are usable even if storage fails */
	total_at_boot_ml = g_state.total_ml;
	hose_at_boot_revs = g_state.hose_revs;

	err = level_init();
	if (err) {
		LOG_ERR("level sensors init failed (%d)", err);
	}
	err = pump_init();
	if (err) {
		LOG_ERR("pump init failed (%d)", err);
	}
#ifdef HAVE_BUZZER
	if (gpio_is_ready_dt(&buzzer)) {
		gpio_pin_configure_dt(&buzzer, GPIO_OUTPUT_INACTIVE);
	}
#endif

	ctl_init(&ctl, &g_par.ctl, g_state.q_hat_mlh);

	int64_t next = k_uptime_get();

	for (;;) {
		int64_t now = k_uptime_get();
		/* Set under the lock below; initialised for the compiler. */
		float m_q = -1.0f;
		int64_t m_until = 0;
		bool hose_reset = false, save = false;

		K_SPINLOCK(&lock) {
			m_q = manual_q;
			m_until = manual_until_ms;
			hose_reset = req_hose_reset;
			save = req_save;
			req_hose_reset = false;
			req_save = false;
		}

		level_tick(now);
		pump_tick(now);

		struct ctl_in in = {
			.now_ms = now,
			.l_wet = level_low_wet(),
			.h_wet = level_high_wet(),
			.pumped_ml = pump_pumped_ml(),
			.night = false, /* M2: schedule from HA + NTP */
		};

		ctl_step(&ctl, &in, &out);

		uint32_t ev = ctl.events;

		ctl.events = 0;
		log_events(&ctl, ev, &out);

		/* Manual override, except that an emergency always wins. */
		bool manual = m_q >= 0.0f && now < m_until && ctl.state != CTL_EMERGENCY;

		if (manual) {
			ctl_invalidate(&ctl); /* the water we move is not inflow */
			pump_set_flow(m_q, false);
		} else {
			if (m_q >= 0.0f) {
				app_auto();
			}
			pump_set_flow(out.q_cmd_mlh, out.fast_ramp);
		}

		/* Counters and persistence. */
		if (hose_reset) {
			hose_at_boot_revs = -pump_revs();
			hose_warned = false;
			save = true;
			LOG_INF("hose replaced: revolution counter reset");
		}
		g_state.total_ml = total_at_boot_ml + pump_pumped_ml();
		g_state.hose_revs = hose_at_boot_revs + pump_revs();
		g_state.q_hat_mlh = ctl.q_hat_mlh;

		bool hose_worn = g_par.hose_life_revs > 0.0f &&
				 g_state.hose_revs >= (double)g_par.hose_life_revs;

		if (hose_worn && !hose_warned) {
			hose_warned = true;
			LOG_WRN("hose due for replacement (%.0f rev)", g_state.hose_revs);
		}

		if (ev & CTL_EV_QHAT_CHANGED) {
			qhat_dirty = true;
		}
		if (save || (qhat_dirty && now - last_qhat_save >= QHAT_SAVE_MIN_MS) ||
		    now - last_totals_save >= TOTALS_SAVE_MS) {
			err = params_save_state();
			if (err) {
				LOG_ERR("saving state failed (%d)", err);
			}
			qhat_dirty = false;
			last_qhat_save = now;
			last_totals_save = now;
		}

		buzzer_update(now, ctl.problem || ctl.sensor_fault);

		K_SPINLOCK(&lock) {
			status = (struct app_status){
				.state = ctl.state,
				.low_wet = in.l_wet,
				.high_wet = in.h_wet,
				.low_raw = level_low_raw(),
				.high_raw = level_high_raw(),
				.q_hat_mlh = ctl.q_hat_mlh,
				.last_meas_mlh = ctl.last_meas_mlh,
				.n_measurements = ctl.n_measurements,
				.q_cmd_mlh = pump_target_mlh(),
				.flow_mlh = pump_flow_mlh(),
				.rpm = pump_rpm(),
				.total_ml = g_state.total_ml,
				.hose_revs = g_state.hose_revs,
				.problem = ctl.problem,
				.sensor_fault = ctl.sensor_fault,
				.hose_worn = hose_worn,
				.manual = manual,
				.manual_left_s = manual ? (uint32_t)((m_until - now) / 1000) : 0,
			};
		}

		next += CONFIG_PUMP_TICK_MS;
		if (next < now) {
			next = now; /* fell behind: do not try to catch up */
		}
		k_sleep(K_TIMEOUT_ABS_MS(next));
	}

	return 0;
}
