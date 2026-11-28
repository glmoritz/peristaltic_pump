#include "report.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>

#include "pumpnet.h"
#include "params.h"
#include "timesync.h"
#include "wifi_prov.h"

void jw_init(struct jw *w, char *buf, size_t size)
{
	w->buf = buf;
	w->size = size;
	w->len = 0;
	w->first = true;
	w->overflow = false;
	if (size) {
		buf[0] = '\0';
	}
}

static void put(struct jw *w, const char *fmt, ...)
{
	va_list ap;

	if (w->overflow) {
		return;
	}
	va_start(ap, fmt);
	int n = vsnprintf(w->buf + w->len, w->size - w->len, fmt, ap);

	va_end(ap);
	if (n < 0 || (size_t)n >= w->size - w->len) {
		w->overflow = true;
		w->buf[w->len] = '\0';
		return;
	}
	w->len += (size_t)n;
}

void jw_raw(struct jw *w, const char *s)
{
	put(w, "%s", s);
}

void jw_escape(struct jw *w, const char *s)
{
	for (; *s && !w->overflow; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') {
			put(w, "\\%c", c);
		} else if (c < 0x20) {
			put(w, "\\u%04x", c);
		} else {
			put(w, "%c", c);
		}
	}
}

void jw_key(struct jw *w, const char *key)
{
	put(w, "%s\"", w->first ? "" : ",");
	jw_escape(w, key);
	put(w, "\":");
	w->first = false;
}

void jw_str(struct jw *w, const char *key, const char *val)
{
	jw_key(w, key);
	put(w, "\"");
	jw_escape(w, val);
	put(w, "\"");
}

void jw_num(struct jw *w, const char *key, double val, int decimals)
{
	jw_key(w, key);
	put(w, "%.*f", decimals, val);
}

void jw_int(struct jw *w, const char *key, long long val)
{
	jw_key(w, key);
	put(w, "%lld", val);
}

void jw_bool(struct jw *w, const char *key, bool val)
{
	jw_key(w, key);
	put(w, val ? "true" : "false");
}

void jw_obj_open(struct jw *w, const char *key)
{
	if (key) {
		jw_key(w, key);
	} else if (!w->first) {
		put(w, ",");
	}
	put(w, "{");
	w->first = true;
}

void jw_obj_close(struct jw *w)
{
	put(w, "}");
	w->first = false;
}

static int done(struct jw *w)
{
	return w->overflow ? -ENOSPC : (int)w->len;
}

int report_sensor(char *buf, size_t size, const struct app_status *st)
{
	struct jw w;
	char t[24];
	float wear = 0.0f;

	if (g_par.hose_life_revs > 0.0f) {
		wear = (float)(100.0 * st->hose_revs / (double)g_par.hose_life_revs);
	}
	timesync_format(g_par.tz_h, t, sizeof(t));

	jw_init(&w, buf, size);
	jw_obj_open(&w, NULL);
	if (t[0]) {
		jw_str(&w, "time", t);
	}
	jw_str(&w, "state", ctl_state_name(st->state));
	jw_num(&w, "flow", (double)st->flow_mlh, 0);
	jw_num(&w, "target", (double)st->q_cmd_mlh, 0);
	jw_num(&w, "rpm", (double)st->rpm, 2);
	jw_num(&w, "inflow", (double)st->q_hat_mlh, 0);
	jw_num(&w, "measured", (double)st->last_meas_mlh, 0);
	jw_int(&w, "n_meas", st->n_measurements);
	jw_bool(&w, "low", st->low_wet);
	jw_bool(&w, "high", st->high_wet);
	jw_bool(&w, "low_raw", st->low_raw);
	jw_bool(&w, "high_raw", st->high_raw);
	jw_num(&w, "total", st->total_ml / 1000.0, 3);
	jw_num(&w, "hose_rev", st->hose_revs, 0);
	jw_num(&w, "hose_wear", (double)wear, 1);
	jw_bool(&w, "manual", st->manual);
	jw_num(&w, "manual_q", (double)st->manual_q_mlh, 0);
	jw_int(&w, "manual_left", st->manual_left_s);
	jw_bool(&w, "calibrating", st->calibrating);
	jw_num(&w, "calib_rev", (double)st->calib_revs, 2);
	jw_bool(&w, "night", st->night);
	jw_bool(&w, "problem", st->problem);
	jw_bool(&w, "sensor_fault", st->sensor_fault);
	jw_bool(&w, "hose_worn", st->hose_worn);
	jw_bool(&w, "muted", st->muted);
	jw_int(&w, "boosts", st->n_boosts);
	jw_int(&w, "emergencies", st->n_emergencies);
	jw_obj_close(&w);
	return done(&w);
}

int report_state(char *buf, size_t size)
{
	struct jw w;
	struct pumpnet_stats ns;
	struct app_status st;
	char t[24];

	pumpnet_get_stats(&ns);
	app_get_status(&st);
	timesync_format(g_par.tz_h, t, sizeof(t));

	jw_init(&w, buf, size);
	jw_obj_open(&w, NULL);
	if (t[0]) {
		jw_str(&w, "time", t);
	}
	jw_int(&w, "uptime", k_uptime_get() / 1000);
	jw_str(&w, "version", APP_VERSION_STRING);
	jw_str(&w, "ssid", wifi_prov_get_ssid());
	jw_int(&w, "rssi", wifi_prov_get_rssi());
	jw_str(&w, "ip", wifi_prov_get_ip());
	jw_int(&w, "mqtt_connects", ns.mqtt_connects);
	jw_int(&w, "events_dropped", st.events_dropped);
	jw_bool(&w, "time_valid", timesync_valid());
	jw_obj_close(&w);
	return done(&w);
}

int report_params(char *buf, size_t size)
{
	struct jw w;

	jw_init(&w, buf, size);
	jw_obj_open(&w, NULL);
	for (size_t i = 0; i < params_count(); i++) {
		const struct param_desc *d = params_get(i);

		if (d->kind == PARAM_FLOAT) {
			jw_num(&w, d->name, (double)params_value(d), 3);
		} else {
			jw_int(&w, d->name, (long long)params_value(d));
		}
	}
	jw_obj_close(&w);
	return done(&w);
}
