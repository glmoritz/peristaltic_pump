/*
 * Level control for the condensate pump (design.md §6).
 *
 * Pure C with no Zephyr dependencies, so the same file builds into the
 * firmware and into the host closed-loop tests (tests/host).
 *
 * The pump never stops while there is water. It alternates between two
 * speeds just below (FILL) and just above (DRAIN) the estimated inflow q̂, so
 * the level drifts slowly between the low (L) and high (H) sensors. Every
 * sensor-to-sensor crossing measures the real inflow and refines q̂:
 *
 *   rise  L wet -> H wet :  q_in = (V_LH + V_pumped) / t
 *   fall  H dry -> L dry :  q_in = (V_pumped - V_LH) / t
 *
 * V_pumped is the volume actually pumped during the interval (integrated by
 * the motor layer), so the measurement stays correct across speed ramps.
 *
 * Escalation when H stays wet (pump too slow, rising inflow, dead pump) is
 * deliberately fast: the reservoir has only ~81 mL above H, which is ~3 min
 * at 1.5 L/h (design.md §4.7).
 */

#ifndef PUMP_CONTROL_H_
#define PUMP_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ctl_state {
	CTL_IDLE = 0,     /* L dry long enough: no water, motor stopped */
	CTL_FILL,         /* q = k_lo * q̂, level rises towards H */
	CTL_DRAIN,        /* q = k_hi * q̂, level falls towards L */
	CTL_EMERGENCY,    /* full speed until L is dry */
	CTL_SENSOR_FAULT, /* L dry + H wet: latched until L is seen wet again */
	CTL_STATE_COUNT,
};

/* Event bits, accumulated in ctl.events until the caller clears them. */
#define CTL_EV_STATE         (1u << 0) /* state changed */
#define CTL_EV_MEASURED      (1u << 1) /* new inflow measurement, q̂ updated */
#define CTL_EV_BOOST         (1u << 2) /* H stuck in DRAIN: q̂ raised */
#define CTL_EV_EMERGENCY     (1u << 3) /* entered EMERGENCY */
#define CTL_EV_PROBLEM_ON    (1u << 4) /* problem flag raised */
#define CTL_EV_PROBLEM_OFF   (1u << 5) /* problem flag cleared */
#define CTL_EV_QHAT_CHANGED  (1u << 6) /* q̂ changed (persist it) */

struct ctl_params {
	float v_lh_ml;          /* volume between the L and H trip lines */
	float k_lo;             /* FILL speed factor (< 1) */
	float k_hi;             /* DRAIN speed factor once H is dry (> 1) */
	float k_hwet;           /* DRAIN speed factor while H is still wet */
	float q_min_mlh;        /* floor for q̂ */
	float q_hat_max_mlh;    /* ceiling for q̂ (design max inflow + margin) */
	float q_emerg_mlh;      /* EMERGENCY flow */
	float q_safe_mlh;       /* SENSOR_FAULT flow */
	float ema_alpha;        /* weight of a new measurement in q̂ (0..1] */
	float boost_factor;     /* q̂ multiplier when H stays wet in DRAIN */
	float idle_factor;      /* q̂ multiplier when FILL times out to IDLE */
	float night_cap_mlh;    /* night cap on FILL and H-dry DRAIN, 0 = none */
	uint32_t max_boosts;    /* boosts in one DRAIN before EMERGENCY */
	uint32_t t_drain_s;     /* H wet this long in DRAIN -> boost */
	uint32_t t_fail_s;      /* H wet this long in EMERGENCY -> problem */
	float v_below_l_ml;     /* pumped with L dry in FILL -> IDLE (77 mL below L) */
	uint32_t t_idle_s;      /* L dry this long in FILL -> IDLE */
	uint32_t t_sensor_fault_s; /* L dry + H wet this long -> SENSOR_FAULT */
};

struct ctl_in {
	int64_t now_ms;   /* monotonic time */
	bool l_wet;       /* debounced low sensor */
	bool h_wet;       /* debounced high sensor */
	double pumped_ml; /* cumulative pumped volume (monotonic) */
	bool night;       /* night mode window active */
};

struct ctl_out {
	float q_cmd_mlh;  /* commanded pump flow */
	bool fast_ramp;   /* use the fast ramp (emergency) */
};

struct ctl_ref {
	bool valid;
	int64_t t_ms;
	double pumped_ml;
};

struct ctl {
	const struct ctl_params *p;
	enum ctl_state state;
	float q_hat_mlh;        /* estimated inflow */
	float last_meas_mlh;    /* last raw measurement (diagnostic) */
	bool problem;           /* pump failure: HA "problem" binary sensor */
	bool sensor_fault;
	uint32_t events;
	uint32_t n_measurements;

	/* internal */
	bool started;
	bool prev_l, prev_h;
	int64_t t_state_ms;     /* entered the current state */
	int64_t t_boost_ms;     /* last boost (or DRAIN entry) */
	uint32_t n_boosts;      /* boosts in the current DRAIN */
	int64_t fast_until_ms;  /* keep the fast ramp until then (leaving EMERGENCY) */
	int64_t t_h_wet_ms;     /* H became wet (valid while H wet) */
	int64_t t_l_dry_ms;     /* L became dry (valid while L dry) */
	double pumped_l_dry_ml; /* pumped volume when L became dry */
	int64_t t_bad_ms;       /* L dry + H wet since, -1 = consistent */
	struct ctl_ref rise;    /* set on L wet edge */
	struct ctl_ref fall;    /* set on H dry edge */
};

/* Default tunables (design.md §6, timeouts shortened for the 81 mL margin). */
void ctl_params_default(struct ctl_params *p);

void ctl_init(struct ctl *c, const struct ctl_params *p, float q_hat_mlh);

/* Run one control step. Call it periodically (e.g. every 100 ms). */
void ctl_step(struct ctl *c, const struct ctl_in *in, struct ctl_out *out);

/* Forget running measurements, e.g. after a manual override moved water. */
void ctl_invalidate(struct ctl *c);

const char *ctl_state_name(enum ctl_state s);

#ifdef __cplusplus
}
#endif

#endif /* PUMP_CONTROL_H_ */
