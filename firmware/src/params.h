/*
 * Tunables and persistent state, stored with the settings subsystem (NVS).
 *
 *   pump/p/<name>    tunables (floats), editable from the shell, MQTT and HA
 *   pump/s/<name>    state: q̂, total pumped volume, hose revolutions
 *
 * Tunables are written by the shell / MQTT threads and read by the control
 * loop. Each is one aligned 32-bit word, so a reader sees the old or the new
 * value, never a torn one.
 */

#ifndef PUMP_PARAMS_H_
#define PUMP_PARAMS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "control.h"

struct app_params {
	struct ctl_params ctl;
	float v_rev_ml;        /* pumped volume per rotor revolution (calibrate) */
	float ramp_mlh_s;      /* normal speed ramp rate (mL/h per second) */
	float ramp_fast_mlh_s; /* emergency ramp rate */
	float hose_life_revs;  /* hose replacement interval, 0 = unknown */
	/* Night mode (design.md §6.4): ctl.night_cap_mlh applies while active. */
	uint32_t night_sched;  /* bool: apply the cap between the hours below */
	uint32_t quiet;        /* bool: apply the cap now, whatever the time */
	float night_start_h;   /* local time, hours (22.5 = 22:30) */
	float night_end_h;
	float tz_h;            /* local time = UTC + tz_h (Curitiba: -3, no DST) */
	uint32_t tele_period_s; /* MQTT telemetry heartbeat */
};

struct app_state {
	float q_hat_mlh;  /* inflow estimate, restored at boot */
	double total_ml;  /* water pumped, lifetime */
	double hose_revs; /* rotor revolutions on the current hose */
};

extern struct app_params g_par;
extern struct app_state g_state; /* as loaded at boot */

enum param_kind {
	PARAM_FLOAT,
	PARAM_U32,
	PARAM_BOOL, /* stored as uint32_t 0/1; a switch in HA */
};

struct param_desc {
	const char *name;  /* command / settings key, also the HA object id */
	const char *label; /* human name (HA) */
	const char *unit;
	void *ptr;
	enum param_kind kind;
	float min;
	float max;
	float step;
};

/* Load defaults, then whatever is stored. */
int params_init(void);

/* Parse, range-check, apply and store one tunable. Names are matched
 * case-insensitively; bools take 0/1/on/off/true/false/toggle. */
int params_set(const char *name, const char *value);

/* Iterate the tunables (shell, MQTT commands, HA discovery). */
size_t params_count(void);
const struct param_desc *params_get(size_t i);
const struct param_desc *params_find(const char *name);
float params_value(const struct param_desc *d);

/* Bumped by every successful params_set(), so observers can republish. */
uint32_t params_generation(void);

/* Store q̂ and the counters. Blocks on flash: never call it from the control
 * loop (main.c hands it to a low-priority work queue). */
int params_save_state(const struct app_state *s);

#endif /* PUMP_PARAMS_H_ */
