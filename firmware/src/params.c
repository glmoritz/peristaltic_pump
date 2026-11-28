#include "params.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(params, LOG_LEVEL_INF);

struct app_params g_par;
struct app_state g_state;

static atomic_t generation;

#define F(field, label, unit, lo, hi, st)                                                       \
	{ #field, label, unit, &g_par.ctl.field, PARAM_FLOAT, lo, hi, st }
#define U(field, label, unit, lo, hi)                                                           \
	{ #field, label, unit, &g_par.ctl.field, PARAM_U32, lo, hi, 1 }
#define A(field, label, unit, lo, hi, st)                                                       \
	{ #field, label, unit, &g_par.field, PARAM_FLOAT, lo, hi, st }
#define AU(field, label, unit, lo, hi)                                                          \
	{ #field, label, unit, &g_par.field, PARAM_U32, lo, hi, 1 }
#define B(field, label) { #field, label, "", &g_par.field, PARAM_BOOL, 0, 1, 1 }

static const struct param_desc descs[] = {
	F(v_lh_ml, "Volume L to H", "mL", 5, 500, 1),
	F(k_lo, "Fill factor", "", 0.3f, 0.99f, 0.01f),
	F(k_hi, "Drain factor", "", 1.01f, 3, 0.01f),
	F(k_hwet, "Drain factor, H wet", "", 1.01f, 5, 0.01f),
	F(q_min_mlh, "Inflow estimate floor", "mL/h", 10, 1000, 10),
	F(q_hat_max_mlh, "Inflow estimate ceiling", "mL/h", 500, 10000, 10),
	F(q_emerg_mlh, "Emergency flow", "mL/h", 1000, 30000, 100),
	F(q_safe_mlh, "Sensor fault flow", "mL/h", 100, 10000, 10),
	F(ema_alpha, "Estimate smoothing", "", 0.01f, 1, 0.01f),
	F(boost_factor, "Boost factor", "", 1.05f, 4, 0.05f),
	F(idle_factor, "Idle factor", "", 0.1f, 1, 0.05f),
	F(night_cap_mlh, "Night cap", "mL/h", 0, 10000, 10),
	F(v_below_l_ml, "Volume below L before idle", "mL", 5, 200, 1),
	U(max_boosts, "Boosts before emergency", "", 0, 10),
	U(t_drain_s, "Boost delay", "s", 10, 3600),
	U(t_fail_s, "Problem delay", "s", 10, 3600),
	U(t_idle_s, "Idle timeout", "s", 10, 86400),
	U(t_sensor_fault_s, "Sensor fault delay", "s", 5, 3600),
	A(v_rev_ml, "Volume per revolution", "mL", 0.1f, 20, 0.01f),
	A(ramp_mlh_s, "Ramp rate", "mL/h/s", 1, 10000, 10),
	A(ramp_fast_mlh_s, "Emergency ramp rate", "mL/h/s", 10, 100000, 100),
	A(hose_life_revs, "Hose life", "rev", 0, 1e9f, 1000),
	B(night_sched, "Night schedule"),
	B(quiet, "Quiet now"),
	A(night_start_h, "Night start", "h", 0, 24, 0.25f),
	A(night_end_h, "Night end", "h", 0, 24, 0.25f),
	A(tz_h, "UTC offset", "h", -12, 14, 0.5f),
	AU(tele_period_s, "Telemetry period", "s", 10, 3600),
};

static void defaults(void)
{
	ctl_params_default(&g_par.ctl);
	g_par.v_rev_ml = 1.7f;           /* design.md §3.2, r = 16 mm, to calibrate */
	g_par.ramp_mlh_s = 100.0f;       /* 2 L/h in 20 s (design.md §6.2) */
	g_par.ramp_fast_mlh_s = 3000.0f; /* 12 L/h in 4 s */
	g_par.hose_life_revs = 0.0f;     /* learned from the first hose */
	g_par.night_sched = 0;
	g_par.quiet = 0;
	g_par.night_start_h = 22.0f;
	g_par.night_end_h = 7.0f;
	g_par.tz_h = -3.0f; /* America/Sao_Paulo, no DST since 2019 */
	g_par.tele_period_s = 60;

	g_state.q_hat_mlh = 800.0f;
	g_state.total_ml = 0.0;
	g_state.hose_revs = 0.0;
}

size_t params_count(void)
{
	return ARRAY_SIZE(descs);
}

const struct param_desc *params_get(size_t i)
{
	return (i < ARRAY_SIZE(descs)) ? &descs[i] : NULL;
}

float params_value(const struct param_desc *d)
{
	return (d->kind == PARAM_FLOAT) ? *(float *)d->ptr : (float)*(uint32_t *)d->ptr;
}

uint32_t params_generation(void)
{
	return (uint32_t)atomic_get(&generation);
}

const struct param_desc *params_find(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(descs); i++) {
		if (strcasecmp(descs[i].name, name) == 0) {
			return &descs[i];
		}
	}
	return NULL;
}

static void apply(const struct param_desc *d, float v)
{
	if (d->kind == PARAM_FLOAT) {
		*(float *)d->ptr = v;
	} else {
		*(uint32_t *)d->ptr = (uint32_t)(v + 0.5f);
	}
}

static int parse_bool(const struct param_desc *d, const char *s, float *v)
{
	if (!strcasecmp(s, "1") || !strcasecmp(s, "on") || !strcasecmp(s, "true")) {
		*v = 1.0f;
	} else if (!strcasecmp(s, "0") || !strcasecmp(s, "off") || !strcasecmp(s, "false")) {
		*v = 0.0f;
	} else if (!strcasecmp(s, "toggle") || !strcasecmp(s, "2")) {
		*v = *(uint32_t *)d->ptr ? 0.0f : 1.0f;
	} else {
		return -EINVAL;
	}
	return 0;
}

int params_set(const char *name, const char *value)
{
	const struct param_desc *d = params_find(name);
	char key[48];
	char *end;
	float v;

	if (d == NULL) {
		return -ENOENT;
	}

	if (d->kind == PARAM_BOOL) {
		if (parse_bool(d, value, &v)) {
			return -EINVAL;
		}
	} else {
		v = strtof(value, &end);
		if (end == value || *end != '\0' || v < d->min || v > d->max) {
			return -EINVAL;
		}
	}

	apply(d, v);
	atomic_inc(&generation);
	snprintk(key, sizeof(key), "pump/p/%s", d->name);
	return settings_save_one(key, &v, sizeof(v));
}

int params_save_state(const struct app_state *s)
{
	int err = settings_save_one("pump/s/q_hat", &s->q_hat_mlh, sizeof(s->q_hat_mlh));

	err = err ?: settings_save_one("pump/s/total_ml", &s->total_ml, sizeof(s->total_ml));
	err = err ?: settings_save_one("pump/s/hose_revs", &s->hose_revs, sizeof(s->hose_revs));
	return err;
}

static int load_value(size_t len, settings_read_cb read_cb, void *cb_arg, void *dst, size_t size)
{
	if (len != size) {
		return -EINVAL;
	}
	return (read_cb(cb_arg, dst, size) == (ssize_t)size) ? 0 : -EIO;
}

static int h_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(key, "p", &next) && next != NULL) {
		const struct param_desc *d = params_find(next);
		float v;

		if (d == NULL) {
			LOG_WRN("stored tunable %s is unknown, ignored", next);
			return 0;
		}
		if (load_value(len, read_cb, cb_arg, &v, sizeof(v)) == 0 && v >= d->min &&
		    v <= d->max) {
			apply(d, v);
		}
		return 0;
	}
	if (settings_name_steq(key, "s/q_hat", NULL)) {
		return load_value(len, read_cb, cb_arg, &g_state.q_hat_mlh,
				  sizeof(g_state.q_hat_mlh));
	}
	if (settings_name_steq(key, "s/total_ml", NULL)) {
		return load_value(len, read_cb, cb_arg, &g_state.total_ml,
				  sizeof(g_state.total_ml));
	}
	if (settings_name_steq(key, "s/hose_revs", NULL)) {
		return load_value(len, read_cb, cb_arg, &g_state.hose_revs,
				  sizeof(g_state.hose_revs));
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(pump, "pump", NULL, h_set, NULL, NULL);

int params_init(void)
{
	int err;

	defaults();

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings init failed (%d), running on defaults", err);
		return err;
	}

	err = settings_load_subtree("pump");
	if (err) {
		LOG_ERR("settings load failed (%d)", err);
	}
	LOG_INF("q_hat %.0f mL/h, total %.1f L, hose %.0f rev", (double)g_state.q_hat_mlh,
		g_state.total_ml / 1000.0, g_state.hose_revs);
	return err;
}
