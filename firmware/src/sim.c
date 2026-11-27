/*
 * Simulated hardware for native_sim: the reservoir of design.md §4.7 and a
 * motor whose real displacement is CONFIG_PUMP_SIM_EFFICIENCY_PCT of what
 * the firmware believes (V_rev not yet calibrated).
 *
 * Implements the hw.h backends, so the rest of the firmware is unchanged.
 */

#include "sim.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "hw.h"
#include "params.h"

LOG_MODULE_REGISTER(sim, LOG_LEVEL_INF);

/* mL above the inside floor (cad/reservoir.py). */
#define V_L_TRIP 77.0
#define V_H_TRIP (77.0 + 65.0)
#define V_LID    (77.0 + 65.0 + 81.0)
#define V_AIR    5.0
#define HYST_ML  3.0

#define USTEPS_PER_REV (CONFIG_PUMP_STEPS_PER_REV * CONFIG_PUMP_MICROSTEPS)

static struct k_spinlock lock;
static double volume_ml = 100.0;
static float inflow_mlh = 800.0f;
static bool dead;
static float usps;
static double position;
static int64_t last_ms = -1;
static bool l_raw, h_raw;
static bool overflowed, air;
static float real_out_mlh;

/* Caller holds the lock. */
static void advance(void)
{
	int64_t now = k_uptime_get();

	if (last_ms < 0) {
		last_ms = now;
	}

	double dt_s = (double)(now - last_ms) / 1000.0;

	last_ms = now;
	position += (double)usps * dt_s;

	double revs_per_h = (double)usps * 3600.0 / USTEPS_PER_REV;

	real_out_mlh = dead ? 0.0f
			    : (float)(revs_per_h * (double)g_par.v_rev_ml *
				      CONFIG_PUMP_SIM_EFFICIENCY_PCT / 100.0);
	if (volume_ml <= 0.0) {
		real_out_mlh = 0.0f; /* sucking air */
	}
	volume_ml += ((double)inflow_mlh - (double)real_out_mlh) * dt_s / 3600.0;
	if (volume_ml < 0.0) {
		volume_ml = 0.0;
	}

	bool now_air = volume_ml < V_AIR && usps > 0.0f;
	bool now_over = volume_ml > V_LID;

	if (now_air && !air) {
		LOG_WRN("SIM: pickup drawing air (%.1f mL)", volume_ml);
	}
	if (now_over && !overflowed) {
		LOG_ERR("SIM: OVERFLOW (%.1f mL)", volume_ml);
	}
	air = now_air;
	overflowed = now_over;

	if (!l_raw && volume_ml > V_L_TRIP + HYST_ML) {
		l_raw = true;
	} else if (l_raw && volume_ml < V_L_TRIP - HYST_ML) {
		l_raw = false;
	}
	if (!h_raw && volume_ml > V_H_TRIP + HYST_ML) {
		h_raw = true;
	} else if (h_raw && volume_ml < V_H_TRIP - HYST_ML) {
		h_raw = false;
	}
}

int motor_hw_init(void)
{
	return 0;
}

int motor_hw_set_speed(float usteps_per_s)
{
	K_SPINLOCK(&lock) {
		advance();
		usps = (usteps_per_s > 0.0f) ? usteps_per_s : 0.0f;
	}
	return 0;
}

int64_t motor_hw_position(void)
{
	int64_t pos = 0;

	K_SPINLOCK(&lock) {
		advance();
		pos = (int64_t)position;
	}
	return pos;
}

int level_hw_init(void)
{
	K_SPINLOCK(&lock) {
		l_raw = volume_ml > V_L_TRIP;
		h_raw = volume_ml > V_H_TRIP;
	}
	LOG_INF("SIM: reservoir %.0f mL, inflow %.0f mL/h, pump efficiency %d %%",
		volume_ml, (double)inflow_mlh, CONFIG_PUMP_SIM_EFFICIENCY_PCT);
	return 0;
}

void level_hw_read(bool *low_wet, bool *high_wet)
{
	K_SPINLOCK(&lock) {
		advance();
		*low_wet = l_raw;
		*high_wet = h_raw;
	}
}

void sim_set_inflow(float mlh)
{
	K_SPINLOCK(&lock) {
		advance();
		inflow_mlh = (mlh > 0.0f) ? mlh : 0.0f;
	}
}

void sim_set_dead(bool d)
{
	K_SPINLOCK(&lock) {
		advance();
		dead = d;
	}
}

void sim_set_volume(double ml)
{
	K_SPINLOCK(&lock) {
		advance();
		volume_ml = (ml > 0.0) ? ml : 0.0;
	}
}

void sim_get_status(struct sim_status *st)
{
	K_SPINLOCK(&lock) {
		advance();
		*st = (struct sim_status){
			.volume_ml = volume_ml,
			.inflow_mlh = inflow_mlh,
			.real_outflow_mlh = real_out_mlh,
			.dead = dead,
			.overflowed = overflowed,
			.air = air,
		};
	}
}
