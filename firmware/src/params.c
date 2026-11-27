#include "params.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(params, LOG_LEVEL_INF);

struct app_params g_par;
struct app_state g_state;

#define F(field, unit, lo, hi) { #field, unit, &g_par.ctl.field, false, lo, hi }
#define U(field, unit, lo, hi) { #field, unit, &g_par.ctl.field, true, lo, hi }
#define A(field, unit, lo, hi) { #field, unit, &g_par.field, false, lo, hi }

static const struct param_desc descs[] = {
	F(v_lh_ml, "mL", 5, 500),
	F(k_lo, "", 0.3f, 0.99f),
	F(k_hi, "", 1.01f, 3),
	F(k_hwet, "", 1.01f, 5),
	F(q_min_mlh, "mL/h", 10, 1000),
	F(q_hat_max_mlh, "mL/h", 500, 10000),
	F(q_emerg_mlh, "mL/h", 1000, 30000),
	F(q_safe_mlh, "mL/h", 100, 10000),
	F(ema_alpha, "", 0.01f, 1),
	F(boost_factor, "", 1.05f, 4),
	F(idle_factor, "", 0.1f, 1),
	F(night_cap_mlh, "mL/h", 0, 10000),
	F(v_below_l_ml, "mL", 5, 200),
	U(max_boosts, "", 0, 10),
	U(t_drain_s, "s", 10, 3600),
	U(t_fail_s, "s", 10, 3600),
	U(t_idle_s, "s", 10, 86400),
	U(t_sensor_fault_s, "s", 5, 3600),
	A(v_rev_ml, "mL", 0.1f, 20),
	A(ramp_mlh_s, "mL/h/s", 1, 10000),
	A(ramp_fast_mlh_s, "mL/h/s", 10, 100000),
	A(hose_life_revs, "rev", 0, 1e9f),
};

static void defaults(void)
{
	ctl_params_default(&g_par.ctl);
	g_par.v_rev_ml = 1.7f;           /* design.md §3.2, r = 16 mm, to calibrate */
	g_par.ramp_mlh_s = 100.0f;       /* 2 L/h in 20 s (design.md §6.2) */
	g_par.ramp_fast_mlh_s = 3000.0f; /* 12 L/h in 4 s */
	g_par.hose_life_revs = 0.0f;     /* learned from the first hose */

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
	return d->is_u32 ? (float)*(uint32_t *)d->ptr : *(float *)d->ptr;
}

static const struct param_desc *find(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(descs); i++) {
		if (strcmp(descs[i].name, name) == 0) {
			return &descs[i];
		}
	}
	return NULL;
}

static void apply(const struct param_desc *d, float v)
{
	if (d->is_u32) {
		*(uint32_t *)d->ptr = (uint32_t)(v + 0.5f);
	} else {
		*(float *)d->ptr = v;
	}
}

int params_set(const char *name, const char *value)
{
	const struct param_desc *d = find(name);
	char key[48];
	char *end;

	if (d == NULL) {
		return -ENOENT;
	}

	float v = strtof(value, &end);

	if (end == value || *end != '\0' || v < d->min || v > d->max) {
		return -EINVAL;
	}

	apply(d, v);
	snprintk(key, sizeof(key), "pump/p/%s", d->name);
	return settings_save_one(key, &v, sizeof(v));
}

int params_save_state(void)
{
	int err = settings_save_one("pump/s/q_hat", &g_state.q_hat_mlh,
				    sizeof(g_state.q_hat_mlh));

	err = err ?: settings_save_one("pump/s/total_ml", &g_state.total_ml,
				       sizeof(g_state.total_ml));
	err = err ?: settings_save_one("pump/s/hose_revs", &g_state.hose_revs,
				       sizeof(g_state.hose_revs));
	return err;
}

static int load_value(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg,
		      void *dst, size_t size)
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
		const struct param_desc *d = find(next);
		float v;

		if (d == NULL) {
			LOG_WRN("stored tunable %s is unknown, ignored", next);
			return 0;
		}
		if (load_value(next, len, read_cb, cb_arg, &v, sizeof(v)) == 0 &&
		    v >= d->min && v <= d->max) {
			apply(d, v);
		}
		return 0;
	}
	if (settings_name_steq(key, "s/q_hat", NULL)) {
		return load_value(key, len, read_cb, cb_arg, &g_state.q_hat_mlh,
				  sizeof(g_state.q_hat_mlh));
	}
	if (settings_name_steq(key, "s/total_ml", NULL)) {
		return load_value(key, len, read_cb, cb_arg, &g_state.total_ml,
				  sizeof(g_state.total_ml));
	}
	if (settings_name_steq(key, "s/hose_revs", NULL)) {
		return load_value(key, len, read_cb, cb_arg, &g_state.hose_revs,
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
