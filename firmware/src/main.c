/*
 * Peristaltic condensate pump: control loop.
 *
 * Every CONFIG_PUMP_TICK_MS: read the debounced sensors, advance the motor
 * ramp, run the level controller (control.c) and command the pump. The loop
 * is fully local and never waits on the network, the shell or flash (see
 * app.h). A task watchdog resets the chip if a tick is ever missed by
 * seconds.
 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/app_version.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "app.h"
#include "control.h"
#include "level.h"
#include "pumpnet.h"
#include "params.h"
#include "pump.h"
#include "timesync.h"
#include "wifi_prov.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define QHAT_SAVE_MIN_MS   (10 * 60 * 1000) /* flash wear: q̂ at most every 10 min */
#define TOTALS_SAVE_MS     (60 * 60 * 1000) /* counters hourly */
#define MANUAL_MAX_S       3600
#define WDT_TIMEOUT_MS     3000
#define MUTE_MS            (60 * 60 * 1000) /* button silences the buzzer for 1 h */
#define LONG_PRESS_MS      5000             /* button: reboot into the portal */
#define CALIB_Q_MLH        3000.0f
#define CALIB_MAX_MS       (15 * 60 * 1000)

#define STORE_PRIO 13 /* below the MQTT thread, above nothing that matters */

#define USER_NODE DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(USER_NODE, buzzer_gpios)
#define HAVE_BUZZER 1
static const struct gpio_dt_spec buzzer = GPIO_DT_SPEC_GET(USER_NODE, buzzer_gpios);
#endif
#if DT_NODE_HAS_PROP(USER_NODE, button_gpios)
#define HAVE_BUTTON 1
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(USER_NODE, button_gpios);
#endif

static struct ctl ctl;
static struct k_spinlock lock;
static struct app_status status;

K_MSGQ_DEFINE(event_q, sizeof(struct app_event), 32, 1);
static uint32_t events_dropped;

/* Requests from other threads, taken under the lock. */
static float manual_q = -1.0f;
static int64_t manual_until_ms;
static float calib_req_revs;
static int64_t alarm_test_until_ms;
static bool req_hose_reset;
static bool req_save;

/* Counters persisted across boots = value at boot + this boot's share. */
static double total_at_boot_ml;
static double hose_at_boot_revs;

/* ---- Blocking work, off the control thread ------------------------------ */

static K_THREAD_STACK_DEFINE(store_stack, 2048);
static struct k_work_q store_q;
static bool restart_portal;

static void save_now(void)
{
	struct app_status st;

	app_get_status(&st);
	struct app_state s = {
		.q_hat_mlh = st.q_hat_mlh,
		.total_ml = st.total_ml,
		.hose_revs = st.hose_revs,
	};
	int err = params_save_state(&s);

	if (err) {
		LOG_ERR("saving state failed (%d)", err);
	}
}

static void save_handler(struct k_work *w)
{
	ARG_UNUSED(w);
	save_now();
}
static K_WORK_DEFINE(save_work, save_handler);

static void restart_handler(struct k_work *w)
{
	ARG_UNUSED(w);
	save_now();
	if (restart_portal) {
		(void)wifi_prov_request_portal();
	}
	LOG_WRN("restarting%s", restart_portal ? " into the Wi-Fi portal" : "");
	pumpnet_shutdown();
	k_msleep(500); /* let the log and the MQTT goodbye out */
	sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(restart_work, restart_handler);

void app_restart(bool portal)
{
	restart_portal = portal;
	k_work_reschedule_for_queue(&store_q, &restart_work, K_MSEC(300));
}

/* ---- Observers' API ------------------------------------------------------ */

void app_get_status(struct app_status *st)
{
	K_SPINLOCK(&lock) {
		*st = status;
	}
}

int app_event_get(struct app_event *ev)
{
	return k_msgq_get(&event_q, ev, K_NO_WAIT);
}

void app_events_purge(void)
{
	k_msgq_purge(&event_q);
}

static void post(enum app_event_type type, uint8_t value)
{
	struct app_event ev = {.type = (uint8_t)type, .value = value};

	if (k_msgq_put(&event_q, &ev, K_NO_WAIT) != 0) {
		events_dropped++; /* nobody is draining it: MQTT is down */
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
		calib_req_revs = 0.0f;
	}
}

void app_auto(void)
{
	K_SPINLOCK(&lock) {
		manual_q = -1.0f;
		calib_req_revs = -1.0f; /* also aborts a calibration */
	}
}

int app_calibrate(float revs)
{
	if (revs < 1.0f || revs > 500.0f) {
		return -EINVAL;
	}
	K_SPINLOCK(&lock) {
		calib_req_revs = revs;
	}
	return 0;
}

int app_calibrate_ml(float ml, float *v_rev_out)
{
	struct app_status st;
	char val[16];

	app_get_status(&st);
	if (st.calibrating || st.calib_revs < 0.5f || ml <= 0.0f) {
		return -EAGAIN;
	}
	float v = ml / st.calib_revs;

	snprintf(val, sizeof(val), "%.3f", (double)v);
	int err = params_set("v_rev_ml", val);

	if (err == 0) {
		*v_rev_out = v;
		LOG_INF("calibrated: %.1f mL / %.2f rev = %.3f mL/rev", (double)ml,
			(double)st.calib_revs, (double)v);
	}
	return err;
}

void app_test_alarm(uint32_t seconds)
{
	K_SPINLOCK(&lock) {
		alarm_test_until_ms = k_uptime_get() + (int64_t)MIN(seconds, 60U) * 1000;
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

/* ---- Local helpers ------------------------------------------------------- */

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

static bool night_active(void)
{
	float h;

	if (g_par.quiet) {
		return true;
	}
	return g_par.night_sched && timesync_local_hours(g_par.tz_h, &h) &&
	       timesync_in_window(h, g_par.night_start_h, g_par.night_end_h);
}

/* Button: a short press silences the alarm for an hour; holding it 5 s
 * reboots into the Wi-Fi portal. Returns true while a chirp is due. */
static bool button_update(int64_t now, bool alarm, int64_t *mute_until)
{
#ifdef HAVE_BUTTON
	static int64_t down_since = -1;
	bool down = gpio_pin_get_dt(&button) > 0;

	if (down && down_since < 0) {
		down_since = now;
	} else if (!down && down_since >= 0) {
		int64_t held = now - down_since;

		down_since = -1;
		if (held >= LONG_PRESS_MS) {
			app_restart(true);
		} else if (held >= 50 && alarm) {
			*mute_until = now + MUTE_MS;
			LOG_WRN("alarm silenced for 1 h (button)");
		}
	}
	/* Chirp once the long press is armed, so you know when to let go. */
	return down_since >= 0 && now - down_since >= LONG_PRESS_MS &&
	       now - down_since < LONG_PRESS_MS + 300;
#else
	ARG_UNUSED(now);
	ARG_UNUSED(alarm);
	ARG_UNUSED(mute_until);
	return false;
#endif
}

static void buzzer_set(bool on)
{
#ifdef HAVE_BUZZER
	gpio_pin_set_dt(&buzzer, on);
#else
	ARG_UNUSED(on);
#endif
}

static void watchdog_init(int *channel)
{
	const struct device *hw = DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));
	int err;

	if (hw && !device_is_ready(hw)) {
		hw = NULL;
	}
	err = task_wdt_init(hw);
	if (err) {
		LOG_ERR("task watchdog init failed (%d)", err);
		*channel = -1;
		return;
	}
	/* No callback: a stuck control loop resets the chip. */
	*channel = task_wdt_add(WDT_TIMEOUT_MS, NULL, NULL);
	LOG_INF("control loop watchdog %d ms%s", WDT_TIMEOUT_MS,
		hw ? ", hardware fallback" : "");
}

enum calib_phase { CAL_OFF, CAL_RUN, CAL_STOP };

int main(void)
{
	struct ctl_out out;
	int64_t last_qhat_save = 0;
	int64_t last_totals_save = k_uptime_get();
	int64_t mute_until = 0;
	bool qhat_dirty = false;
	bool hose_warned = false;
	int wdt = -1;
	uint32_t n_boosts = 0, n_emergencies = 0;
	int err;

	/* Calibration run: CAL_RUN turns until the revolutions are done,
	 * CAL_STOP holds the motor at 0 until it has stopped. */
	enum calib_phase cal = CAL_OFF;
	double cal_start_revs = 0.0;
	float cal_target = 0.0f;
	float cal_done_revs = 0.0f;
	int64_t cal_started = 0;

	/* Last values posted as events, to post only the edges. */
	struct app_status prev = {.state = CTL_STATE_COUNT};

	LOG_INF("peristaltic pump firmware %s, %s", APP_VERSION_STRING,
		IS_ENABLED(CONFIG_PUMP_SIM) ? "SIMULATED reservoir and motor" : CONFIG_BOARD);

	(void)params_init(); /* defaults are usable even if storage fails */
	total_at_boot_ml = g_state.total_ml;
	hose_at_boot_revs = g_state.hose_revs;

	k_work_queue_start(&store_q, store_stack, K_THREAD_STACK_SIZEOF(store_stack), STORE_PRIO,
			   &(struct k_work_queue_config){.name = "store"});

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
#ifdef HAVE_BUTTON
	if (gpio_is_ready_dt(&button)) {
		gpio_pin_configure_dt(&button, GPIO_INPUT);
	}
#endif

	ctl_init(&ctl, &g_par.ctl, g_state.q_hat_mlh);

	/* The network runs in its own lower-priority threads from here on. */
	pumpnet_start();

	watchdog_init(&wdt);

	int64_t next = k_uptime_get();

	for (;;) {
		int64_t now = k_uptime_get();
		/* Set under the lock below; initialised for the compiler. */
		float m_q = -1.0f;
		int64_t m_until = 0;
		float cal_req = 0.0f;
		int64_t test_until = 0;
		bool hose_reset = false, save = false;

		if (wdt >= 0) {
			task_wdt_feed(wdt);
		}

		K_SPINLOCK(&lock) {
			m_q = manual_q;
			m_until = manual_until_ms;
			cal_req = calib_req_revs;
			test_until = alarm_test_until_ms;
			hose_reset = req_hose_reset;
			save = req_save;
			calib_req_revs = 0.0f;
			req_hose_reset = false;
			req_save = false;
		}

		level_tick(now);
		pump_tick(now);

		bool night = night_active();
		struct ctl_in in = {
			.now_ms = now,
			.l_wet = level_low_wet(),
			.h_wet = level_high_wet(),
			.pumped_ml = pump_pumped_ml(),
			.night = night,
		};

		ctl_step(&ctl, &in, &out);

		uint32_t ev = ctl.events;

		ctl.events = 0;
		log_events(&ctl, ev, &out);
		if (ev & CTL_EV_BOOST) {
			n_boosts++;
		}
		if (ev & CTL_EV_EMERGENCY) {
			n_emergencies++;
		}

		/* Calibration requests. */
		if (cal_req > 0.0f) {
			cal = CAL_RUN;
			cal_target = cal_req;
			cal_start_revs = pump_revs();
			cal_started = now;
			K_SPINLOCK(&lock) {
				manual_q = -1.0f; /* a calibration replaces a manual run */
			}
			m_q = -1.0f;
			LOG_INF("calibration: %.0f revolutions at %.0f mL/h", (double)cal_target,
				(double)CALIB_Q_MLH);
		} else if (cal_req < 0.0f && cal != CAL_OFF) {
			cal = CAL_OFF;
			LOG_WRN("calibration aborted");
		}
		if (cal != CAL_OFF && (ctl.state == CTL_EMERGENCY || now - cal_started > CALIB_MAX_MS)) {
			cal = CAL_OFF;
			LOG_WRN("calibration aborted (%s)",
				ctl.state == CTL_EMERGENCY ? "emergency" : "timeout");
		}
		if (cal == CAL_RUN && pump_revs() - cal_start_revs >= (double)cal_target) {
			cal = CAL_STOP;
		}
		if (cal == CAL_STOP && pump_flow_mlh() <= 0.0f) {
			cal = CAL_OFF;
			cal_done_revs = (float)(pump_revs() - cal_start_revs);
			LOG_INF("calibration run done: %.2f rev. Measure the water and send "
				"`CalibrateMl <mL>`", (double)cal_done_revs);
		}

		/* Manual override or calibration, except that an emergency always wins. */
		bool manual = cal != CAL_OFF ||
			      (m_q >= 0.0f && now < m_until && ctl.state != CTL_EMERGENCY);
		float q_manual = (cal == CAL_RUN) ? CALIB_Q_MLH : (cal == CAL_STOP) ? 0.0f : m_q;

		if (manual) {
			ctl_invalidate(&ctl); /* the water we move is not inflow */
			pump_set_flow(q_manual, cal == CAL_STOP);
		} else {
			if (m_q >= 0.0f) {
				K_SPINLOCK(&lock) {
					if (manual_until_ms == m_until) {
						manual_q = -1.0f; /* expired, not renewed */
					}
				}
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
		double total_ml = total_at_boot_ml + pump_pumped_ml();
		double hose_revs = hose_at_boot_revs + pump_revs();
		bool hose_worn = g_par.hose_life_revs > 0.0f &&
				 hose_revs >= (double)g_par.hose_life_revs;

		if (hose_worn && !hose_warned) {
			hose_warned = true;
			LOG_WRN("hose due for replacement (%.0f rev)", hose_revs);
		}

		if (ev & CTL_EV_QHAT_CHANGED) {
			qhat_dirty = true;
		}
		bool save_due = save || (qhat_dirty && now - last_qhat_save >= QHAT_SAVE_MIN_MS) ||
				now - last_totals_save >= TOTALS_SAVE_MS;

		/* Alarm, mute, button, buzzer. */
		bool alarm = ctl.problem || ctl.sensor_fault;

		if (!alarm) {
			mute_until = 0;
		}
		bool chirp = button_update(now, alarm, &mute_until);
		bool muted = alarm && now < mute_until;
		bool testing = now < test_until;

		/* Alarm: 200 ms beep every 5 s. Test: 200 ms on / 200 ms off. */
		buzzer_set(chirp || (alarm && !muted && (now % 5000) < 200) ||
			   (testing && (now % 400) < 200));

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
				.total_ml = total_ml,
				.hose_revs = hose_revs,
				.problem = ctl.problem,
				.sensor_fault = ctl.sensor_fault,
				.hose_worn = hose_worn,
				.manual = manual,
				.manual_q_mlh = manual ? q_manual : 0.0f,
				.manual_left_s = (manual && cal == CAL_OFF)
							 ? (uint32_t)((m_until - now) / 1000) : 0,
				.night = night,
				.muted = muted,
				.calibrating = cal != CAL_OFF,
				.calib_revs = cal_done_revs,
				.n_boosts = n_boosts,
				.n_emergencies = n_emergencies,
				.events_dropped = events_dropped,
			};
		}

		/* After the snapshot, so a save sees this tick's values. */
		if (save_due) {
			k_work_submit_to_queue(&store_q, &save_work);
			qhat_dirty = false;
			last_qhat_save = now;
			last_totals_save = now;
		}

		/* Edges for the observers, oldest first. */
		if (in.l_wet != prev.low_wet || prev.state == CTL_STATE_COUNT) {
			post(APP_EV_LOW, in.l_wet);
		}
		if (in.h_wet != prev.high_wet || prev.state == CTL_STATE_COUNT) {
			post(APP_EV_HIGH, in.h_wet);
		}
		if (ctl.state != prev.state) {
			post(APP_EV_STATE, (uint8_t)ctl.state);
		}
		if (ctl.problem != prev.problem) {
			post(APP_EV_PROBLEM, ctl.problem);
		}
		if (ctl.sensor_fault != prev.sensor_fault) {
			post(APP_EV_SENSOR_FAULT, ctl.sensor_fault);
		}
		if (hose_worn != prev.hose_worn) {
			post(APP_EV_HOSE_WORN, hose_worn);
		}
		if (manual != prev.manual) {
			post(APP_EV_MANUAL, manual);
		}
		if (night != prev.night) {
			post(APP_EV_NIGHT, night);
		}
		prev.low_wet = in.l_wet;
		prev.high_wet = in.h_wet;
		prev.state = ctl.state;
		prev.problem = ctl.problem;
		prev.sensor_fault = ctl.sensor_fault;
		prev.hose_worn = hose_worn;
		prev.manual = manual;
		prev.night = night;

		next += CONFIG_PUMP_TICK_MS;
		if (next < now) {
			next = now; /* fell behind: do not try to catch up */
		}
		k_sleep(K_TIMEOUT_ABS_MS(next));
	}

	return 0;
}
