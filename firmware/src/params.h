/*
 * Tunables and persistent state, stored with the settings subsystem (NVS).
 *
 *   pump/p/<name>    tunables (floats), editable from the shell (later HA)
 *   pump/s/<name>    state: q̂, total pumped volume, hose revolutions
 */

#ifndef PUMP_PARAMS_H_
#define PUMP_PARAMS_H_

#include <stdbool.h>
#include <stddef.h>

#include "control.h"

struct app_params {
	struct ctl_params ctl;
	float v_rev_ml;        /* pumped volume per rotor revolution (calibrate) */
	float ramp_mlh_s;      /* normal speed ramp rate (mL/h per second) */
	float ramp_fast_mlh_s; /* emergency ramp rate */
	float hose_life_revs;  /* hose replacement interval, 0 = unknown */
};

struct app_state {
	float q_hat_mlh;  /* inflow estimate, restored at boot */
	double total_ml;  /* water pumped, lifetime */
	double hose_revs; /* rotor revolutions on the current hose */
};

extern struct app_params g_par;
extern struct app_state g_state;

struct param_desc {
	const char *name;
	const char *unit;
	void *ptr;
	bool is_u32;
	float min;
	float max;
};

/* Load defaults, then whatever is stored. */
int params_init(void);

/* Parse, range-check, apply and store one tunable. */
int params_set(const char *name, const char *value);

/* Iterate the tunables (for the shell and, later, HA discovery). */
size_t params_count(void);
const struct param_desc *params_get(size_t i);
float params_value(const struct param_desc *d);

/* Store q̂ and the counters. */
int params_save_state(void);

#endif /* PUMP_PARAMS_H_ */
