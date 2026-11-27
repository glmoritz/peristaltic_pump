#include "pump.h"

#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "hw.h"
#include "params.h"

LOG_MODULE_REGISTER(pump, LOG_LEVEL_INF);

#define USTEPS_PER_REV (CONFIG_PUMP_STEPS_PER_REV * CONFIG_PUMP_MICROSTEPS)

/* Below this the motor is stopped instead of crawling. */
#define Q_STOP_MLH 5.0f

static float target_mlh;
static float flow_mlh;
static bool fast_ramp;
static float applied_usps = -1.0f;
static int64_t last_ms = -1;
static int64_t last_pos;
static double pumped_ml;
static double revs;

int pump_init(void)
{
	int err = motor_hw_init();

	if (err) {
		LOG_ERR("motor init failed (%d)", err);
		return err;
	}
	last_pos = motor_hw_position();
	return motor_hw_set_speed(0.0f);
}

void pump_set_flow(float q_mlh, bool fast)
{
	target_mlh = (q_mlh > 0.0f) ? q_mlh : 0.0f;
	fast_ramp = fast;
}

static float usps_for(float q_mlh)
{
	if (q_mlh < Q_STOP_MLH || g_par.v_rev_ml <= 0.0f) {
		return 0.0f;
	}
	return q_mlh / 3600.0f / g_par.v_rev_ml * (float)USTEPS_PER_REV;
}

void pump_tick(int64_t now_ms)
{
	if (last_ms < 0) {
		last_ms = now_ms;
	}

	float dt_s = (float)(now_ms - last_ms) / 1000.0f;

	last_ms = now_ms;

	/* Ramp the flow towards the target (design.md §6.2: no abrupt sound). */
	float rate = fast_ramp ? g_par.ramp_fast_mlh_s : g_par.ramp_mlh_s;
	float step = rate * dt_s;
	float d = target_mlh - flow_mlh;

	flow_mlh += (fabsf(d) <= step) ? d : copysignf(step, d);

	/* Only touch the hardware when the speed really changes (> 0.2 %). */
	float usps = usps_for(flow_mlh);

	if (applied_usps < 0.0f || (usps == 0.0f) != (applied_usps == 0.0f) ||
	    fabsf(usps - applied_usps) > 0.002f * applied_usps) {
		int err = motor_hw_set_speed(usps);

		if (err) {
			LOG_ERR("set speed %.1f usteps/s failed (%d)", (double)usps, err);
		} else {
			applied_usps = usps;
		}
	}

	/* Volume from the steps actually made, at the current V_rev. */
	int64_t pos = motor_hw_position();
	double d_revs = (double)(pos - last_pos) / USTEPS_PER_REV;

	last_pos = pos;
	revs += d_revs;
	pumped_ml += d_revs * (double)g_par.v_rev_ml;
}

float pump_flow_mlh(void)
{
	return flow_mlh;
}

float pump_target_mlh(void)
{
	return target_mlh;
}

float pump_rpm(void)
{
	return (g_par.v_rev_ml > 0.0f) ? flow_mlh / 60.0f / g_par.v_rev_ml : 0.0f;
}

double pump_pumped_ml(void)
{
	return pumped_ml;
}

double pump_revs(void)
{
	return revs;
}
