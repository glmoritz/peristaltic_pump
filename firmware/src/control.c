/*
 * Level control state machine. See control.h and design.md §6.
 */

#include "control.h"

#include <stddef.h>

/* Ignore crossings shorter than this: they are sensor chatter, not a fill. */
#define MIN_MEAS_MS (20 * 1000)

/* Fast ramp kept this long after EMERGENCY so the speed drops at once. */
#define FAST_RAMP_HOLD_MS (30 * 1000)

void ctl_params_default(struct ctl_params *p)
{
	*p = (struct ctl_params){
		.v_lh_ml = 65.0f,           /* cad/reservoir.py, design.md §4.7 */
		.k_lo = 0.8f,
		.k_hi = 1.25f,
		.k_hwet = 2.0f,
		.q_min_mlh = 100.0f,
		.q_hat_max_mlh = 3000.0f,   /* 2 L/h design max (§3.1) + pump slip */
		.q_emerg_mlh = 12000.0f,    /* ~120 RPM at 1.7 mL/rev */
		.q_safe_mlh = 3000.0f,
		.ema_alpha = 0.3f,
		.boost_factor = 1.5f,
		.idle_factor = 0.7f,
		.night_cap_mlh = 0.0f,
		.max_boosts = 1,
		/*
		 * Dead pump at 1.5 L/h: H wet -> boost at 60 s -> EMERGENCY at
		 * 120 s -> problem at 165 s, before the ~194 s it takes to fill
		 * the 81 mL above H.
		 */
		.t_drain_s = 60,
		.t_fail_s = 45,
		.v_below_l_ml = 50.0f,
		.t_idle_s = 600,
		.t_sensor_fault_s = 30,
	};
}

const char *ctl_state_name(enum ctl_state s)
{
	static const char *const names[CTL_STATE_COUNT] = {
		[CTL_IDLE] = "idle",
		[CTL_FILL] = "fill",
		[CTL_DRAIN] = "drain",
		[CTL_EMERGENCY] = "emergency",
		[CTL_SENSOR_FAULT] = "sensor_fault",
	};

	return (s < CTL_STATE_COUNT) ? names[s] : "?";
}

static float clampf(float v, float lo, float hi)
{
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static int64_t max64(int64_t a, int64_t b)
{
	return (a > b) ? a : b;
}

static void set_qhat(struct ctl *c, float q)
{
	q = clampf(q, c->p->q_min_mlh, c->p->q_hat_max_mlh);
	if (q != c->q_hat_mlh) {
		c->q_hat_mlh = q;
		c->events |= CTL_EV_QHAT_CHANGED;
	}
}

static void enter(struct ctl *c, enum ctl_state s, int64_t now)
{
	if (c->state != s) {
		c->state = s;
		c->events |= CTL_EV_STATE;
	}
	c->t_state_ms = now;

	switch (s) {
	case CTL_FILL:
		c->rise.valid = false;
		break;
	case CTL_DRAIN:
		c->t_boost_ms = now;
		c->n_boosts = 0;
		c->fall.valid = false;
		break;
	case CTL_EMERGENCY:
		c->fall.valid = false;
		c->events |= CTL_EV_EMERGENCY;
		break;
	default:
		break;
	}
}

/*
 * Inflow from a crossing. sign = +1 for a rise (water came in faster than we
 * pumped), -1 for a fall. Returns false if the interval is too short.
 */
static bool measure(struct ctl *c, const struct ctl_ref *ref, const struct ctl_in *in,
		    float sign, float *q_out)
{
	if (!ref->valid) {
		return false;
	}

	int64_t dt_ms = in->now_ms - ref->t_ms;

	if (dt_ms < MIN_MEAS_MS) {
		return false;
	}

	float v_pumped = (float)(in->pumped_ml - ref->pumped_ml);
	float q = (v_pumped + sign * c->p->v_lh_ml) * 3600000.0f / (float)dt_ms;

	c->last_meas_mlh = q;
	c->n_measurements++;
	c->events |= CTL_EV_MEASURED;
	*q_out = q;
	return true;
}

static void update_ema(struct ctl *c, float q)
{
	set_qhat(c, c->q_hat_mlh + c->p->ema_alpha * (q - c->q_hat_mlh));
}

void ctl_init(struct ctl *c, const struct ctl_params *p, float q_hat_mlh)
{
	*c = (struct ctl){
		.p = p,
		.state = CTL_IDLE,
		.t_bad_ms = -1,
	};
	c->q_hat_mlh = clampf(q_hat_mlh, p->q_min_mlh, p->q_hat_max_mlh);
}

void ctl_invalidate(struct ctl *c)
{
	c->rise.valid = false;
	c->fall.valid = false;
}

static void set_problem(struct ctl *c, bool on)
{
	if (on != c->problem) {
		c->problem = on;
		c->events |= on ? CTL_EV_PROBLEM_ON : CTL_EV_PROBLEM_OFF;
	}
}

/*
 * Night mode caps the speed for quiet, but DRAIN must still out-pump the
 * inflow, so there the cap never goes below NIGHT_DRAIN_MIN * q̂. A cap set
 * below the real inflow therefore shrinks the saw-tooth instead of causing
 * emergencies.
 */
#define NIGHT_DRAIN_MIN 1.1f

static float night_capped(const struct ctl *c, const struct ctl_in *in, float q, bool drain)
{
	float cap = c->p->night_cap_mlh;

	if (!in->night || cap <= 0.0f) {
		return q;
	}
	if (drain && cap < NIGHT_DRAIN_MIN * c->q_hat_mlh) {
		cap = NIGHT_DRAIN_MIN * c->q_hat_mlh;
	}
	return (q > cap) ? cap : q;
}

void ctl_step(struct ctl *c, const struct ctl_in *in, struct ctl_out *out)
{
	const struct ctl_params *p = c->p;
	const int64_t now = in->now_ms;
	float q;

	if (!c->started) {
		c->started = true;
		c->prev_l = in->l_wet;
		c->prev_h = in->h_wet;
		c->t_h_wet_ms = now;
		c->t_l_dry_ms = now;
		c->pumped_l_dry_ml = in->pumped_ml;
		enter(c, in->h_wet ? CTL_DRAIN : (in->l_wet ? CTL_FILL : CTL_IDLE), now);
	}

	const bool l_rise = in->l_wet && !c->prev_l;
	const bool l_fall = !in->l_wet && c->prev_l;
	const bool h_rise = in->h_wet && !c->prev_h;
	const bool h_fall = !in->h_wet && c->prev_h;

	c->prev_l = in->l_wet;
	c->prev_h = in->h_wet;
	if (h_rise) {
		c->t_h_wet_ms = now;
	}
	if (l_fall) {
		c->t_l_dry_ms = now;
		c->pumped_l_dry_ml = in->pumped_ml;
	}

	/*
	 * Sensor plausibility: H wet with L dry cannot happen physically. The
	 * fault latches until L is seen wet again (a stuck-dry L would
	 * otherwise clear it every time H dries).
	 */
	if (!in->l_wet && in->h_wet) {
		if (c->t_bad_ms < 0) {
			c->t_bad_ms = now;
		}
		if (c->state != CTL_SENSOR_FAULT &&
		    now - c->t_bad_ms >= (int64_t)p->t_sensor_fault_s * 1000) {
			c->sensor_fault = true;
			c->events |= CTL_EV_PROBLEM_ON;
			enter(c, CTL_SENSOR_FAULT, now);
		}
	} else {
		c->t_bad_ms = -1;
		if (c->state == CTL_SENSOR_FAULT && in->l_wet) {
			c->sensor_fault = false;
			c->events |= CTL_EV_PROBLEM_OFF;
			ctl_invalidate(c);
			enter(c, in->h_wet ? CTL_DRAIN : CTL_FILL, now);
		}
	}

	switch (c->state) {
	case CTL_IDLE:
		if (in->h_wet && in->l_wet) {
			enter(c, CTL_DRAIN, now);
		} else if (in->l_wet) {
			enter(c, CTL_FILL, now);
		}
		break;

	case CTL_FILL:
		if (l_fall && c->rise.valid) {
			/*
			 * L was wet in this FILL and dried again while we pumped
			 * k_lo * q̂: the inflow is below that.
			 */
			c->rise.valid = false;
			set_qhat(c, c->q_hat_mlh * p->k_lo);
		}
		if (in->h_wet && in->l_wet) {
			if (h_rise && measure(c, &c->rise, in, +1.0f, &q)) {
				update_ema(c, q);
			}
			enter(c, CTL_DRAIN, now);
		} else if (!in->l_wet &&
			   (in->pumped_ml - c->pumped_l_dry_ml >= (double)p->v_below_l_ml ||
			    now - max64(c->t_l_dry_ms, c->t_state_ms) >=
				    (int64_t)p->t_idle_s * 1000)) {
			/*
			 * Inflow below k_lo * q̂, or the AC is off. Stop before the
			 * 77 mL under L is gone and the pickup draws air.
			 */
			set_qhat(c, c->q_hat_mlh * p->idle_factor);
			enter(c, CTL_IDLE, now);
		}
		break;

	case CTL_DRAIN:
		if (!in->l_wet) {
			if (l_fall && measure(c, &c->fall, in, -1.0f, &q)) {
				update_ema(c, q);
			}
			enter(c, CTL_FILL, now);
		} else if (in->h_wet) {
			if (h_rise) {
				/*
				 * H dried and got wet again while we pumped
				 * k_hi * q̂ with H dry: the inflow is above that.
				 */
				c->fall.valid = false;
				set_qhat(c, c->q_hat_mlh * p->k_hi);
				c->events |= CTL_EV_BOOST;
			}
			if (now - max64(c->t_boost_ms, c->t_h_wet_ms) >=
			    (int64_t)p->t_drain_s * 1000) {
				/* Pump too slow for the inflow: raise q̂. */
				if (c->n_boosts >= p->max_boosts) {
					enter(c, CTL_EMERGENCY, now);
				} else {
					set_qhat(c, c->q_hat_mlh * p->boost_factor);
					c->n_boosts++;
					c->t_boost_ms = now;
					c->events |= CTL_EV_BOOST;
				}
			}
		}
		break;

	case CTL_EMERGENCY:
		if (!in->l_wet) {
			/*
			 * A full-speed fall is short and biased by the debounce
			 * delay, so it is only blended in like any other one.
			 */
			if (l_fall && measure(c, &c->fall, in, -1.0f, &q)) {
				update_ema(c, q);
			}
			/* Come down from full speed quickly, or we drain below L. */
			c->fast_until_ms = now + FAST_RAMP_HOLD_MS;
			enter(c, CTL_FILL, now);
		} else if (in->h_wet && !c->problem &&
			   now - max64(c->t_state_ms, c->t_h_wet_ms) >=
				   (int64_t)p->t_fail_s * 1000) {
			/* Full speed and still not clearing H: pump failure. */
			set_problem(c, true);
		}
		break;

	case CTL_SENSOR_FAULT:
	default:
		break;
	}

	/* Measurement references, taken after the state transitions. */
	if (l_rise && c->state == CTL_FILL && !c->rise.valid) {
		c->rise = (struct ctl_ref){ true, now, in->pumped_ml };
	}
	if (h_fall && (c->state == CTL_DRAIN || c->state == CTL_EMERGENCY)) {
		c->fall = (struct ctl_ref){ true, now, in->pumped_ml };
	}

	if (c->problem && !in->h_wet) {
		set_problem(c, false);
	}

	out->fast_ramp = (now < c->fast_until_ms);
	switch (c->state) {
	case CTL_FILL:
		out->q_cmd_mlh = night_capped(c, in, p->k_lo * c->q_hat_mlh, false);
		break;
	case CTL_DRAIN:
		/* Never capped while H is wet: only ~81 mL left above H. */
		out->q_cmd_mlh = in->h_wet ? p->k_hwet * c->q_hat_mlh
					   : night_capped(c, in, p->k_hi * c->q_hat_mlh, true);
		break;
	case CTL_EMERGENCY:
		out->q_cmd_mlh = p->q_emerg_mlh;
		out->fast_ramp = true;
		break;
	case CTL_SENSOR_FAULT:
		/* Only H is trusted: safe speed while it is wet, else about q̂. */
		out->q_cmd_mlh = in->h_wet ? p->q_safe_mlh : c->q_hat_mlh;
		break;
	case CTL_IDLE:
	default:
		out->q_cmd_mlh = 0.0f;
		break;
	}
}
