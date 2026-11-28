#include "cmnd.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.h"
#include "netcfg.h"
#include "params.h"
#include "report.h"
#include "timesync.h"
#include "wifi_prov.h"

LOG_MODULE_REGISTER(cmnd, LOG_LEVEL_INF);

/* The shell and the MQTT thread may both call in; commands are rare. */
static K_MUTEX_DEFINE(lock);
static char tmp[1024];

#define PURGE_Q_MLH 3000.0f

struct cmd {
	const char *name;
	/* Fill w (inside the result object). Return 0 or -EINVAL. */
	int (*fn)(const char *name, const char *pl, struct jw *w);
	const char *help;
};

static int error(struct jw *w, const char *name, const char *hint)
{
	jw_str(w, name, "Error");
	jw_str(w, "Hint", hint);
	return -EINVAL;
}

static bool to_float(const char *s, float *v)
{
	char *end;

	*v = strtof(s, &end);
	return end != s && (*end == '\0' || isspace((unsigned char)*end));
}

static int c_status(const char *name, const char *pl, struct jw *w)
{
	struct app_status st;

	ARG_UNUSED(name);
	ARG_UNUSED(pl);
	app_get_status(&st);
	if (report_sensor(tmp, sizeof(tmp), &st) > 0) {
		jw_key(w, "Sensor");
		jw_raw(w, tmp);
	}
	if (report_state(tmp, sizeof(tmp)) > 0) {
		jw_key(w, "State");
		jw_raw(w, tmp);
	}
	return 0;
}

static int c_params(const char *name, const char *pl, struct jw *w)
{
	ARG_UNUSED(pl);
	if (report_params(tmp, sizeof(tmp)) > 0) {
		jw_key(w, name);
		jw_raw(w, tmp);
	}
	return 0;
}

static int c_manual(const char *name, const char *pl, struct jw *w)
{
	struct app_status st;

	if (*pl) {
		char *end;
		float q = strtof(pl, &end);
		unsigned long s = strtoul(end, NULL, 10);

		if (end == pl || q < 0.0f || q > 30000.0f) {
			return error(w, name, "Manual <mL/h> [seconds, default 600]");
		}
		app_manual(q, s ? (uint32_t)s : 600U);
		k_msleep(2 * CONFIG_PUMP_TICK_MS); /* let the loop take it */
	}
	app_get_status(&st);
	jw_obj_open(w, name);
	jw_bool(w, "on", st.manual);
	jw_num(w, "q", (double)st.manual_q_mlh, 0);
	jw_int(w, "left", st.manual_left_s);
	jw_obj_close(w);
	return 0;
}

static int c_auto(const char *name, const char *pl, struct jw *w)
{
	ARG_UNUSED(pl);
	app_auto();
	jw_str(w, name, "Done");
	return 0;
}

static int c_purge(const char *name, const char *pl, struct jw *w)
{
	unsigned long s = *pl ? strtoul(pl, NULL, 10) : 60;

	if (s == 0 || s > 3600) {
		return error(w, name, "Purge [seconds, default 60]");
	}
	app_manual(PURGE_Q_MLH, (uint32_t)s);
	jw_obj_open(w, name);
	jw_num(w, "q", (double)PURGE_Q_MLH, 0);
	jw_int(w, "seconds", (long long)s);
	jw_obj_close(w);
	return 0;
}

static int c_calibrate(const char *name, const char *pl, struct jw *w)
{
	float revs = 20.0f;

	if (*pl && !to_float(pl, &revs)) {
		return error(w, name, "Calibrate [revolutions, default 20]");
	}
	if (app_calibrate(revs)) {
		return error(w, name, "1 to 500 revolutions");
	}
	jw_obj_open(w, name);
	jw_num(w, "revolutions", (double)revs, 1);
	jw_str(w, "next", "measure the water, then CalibrateMl <mL>");
	jw_obj_close(w);
	return 0;
}

static int c_calibrate_ml(const char *name, const char *pl, struct jw *w)
{
	float ml, v;

	if (!to_float(pl, &ml)) {
		return error(w, name, "CalibrateMl <mL collected>");
	}
	if (app_calibrate_ml(ml, &v)) {
		return error(w, name, "run Calibrate first and wait for it to finish");
	}
	jw_num(w, "v_rev_ml", (double)v, 3);
	return 0;
}

static int c_simple(const char *name, const char *pl, struct jw *w)
{
	ARG_UNUSED(pl);
	if (!strcasecmp(name, "HoseReplaced")) {
		app_hose_replaced();
	} else if (!strcasecmp(name, "Save")) {
		app_save();
	}
	jw_str(w, name, "Done");
	return 0;
}

static int c_test_alarm(const char *name, const char *pl, struct jw *w)
{
	unsigned long s = *pl ? strtoul(pl, NULL, 10) : 3;

	app_test_alarm((uint32_t)s);
	jw_str(w, name, "Done");
	return 0;
}

static int c_restart(const char *name, const char *pl, struct jw *w)
{
	if (strcmp(pl, "1") != 0) {
		return error(w, name, "Restart 1");
	}
	app_restart(false);
	jw_str(w, name, "Restarting");
	return 0;
}

static int c_wificonfig(const char *name, const char *pl, struct jw *w)
{
	if (strcmp(pl, "2") != 0) {
		return error(w, name, "WifiConfig 2 reboots into the setup portal");
	}
	app_restart(true);
	jw_str(w, name, "Restarting into the portal");
	return 0;
}

static int c_ssid(const char *name, const char *pl, struct jw *w)
{
	if (*pl && wifi_prov_store_credentials(pl, NULL)) {
		return error(w, name, "1 to 32 characters");
	}
	jw_str(w, name, wifi_prov_get_stored_ssid());
	if (*pl) {
		jw_str(w, "Restart", "needed: Restart 1");
	}
	return 0;
}

static int c_password(const char *name, const char *pl, struct jw *w)
{
	if (*pl) {
		/* "-" clears it (open network); Tasmota uses a double quote. */
		if (wifi_prov_store_credentials(NULL, strcmp(pl, "-") ? pl : "")) {
			return error(w, name, "set SSId first");
		}
		jw_str(w, "Restart", "needed: Restart 1");
	}
	jw_str(w, name, "****");
	return 0;
}

static int c_time(const char *name, const char *pl, struct jw *w)
{
	char t[24];

	ARG_UNUSED(pl);
	timesync_format(g_par.tz_h, t, sizeof(t));
	jw_str(w, name, t[0] ? t : "not set");
	return 0;
}

static int c_backlog(const char *name, const char *pl, struct jw *w);

static const struct cmd cmds[] = {
	{"Status", c_status, "everything"},
	{"Params", c_params, "all tunables"},
	{"Manual", c_manual, "<mL/h> [s]  fixed flow (default 600 s)"},
	{"Auto", c_auto, "back to automatic control"},
	{"Purge", c_purge, "[s]  3 L/h for s seconds (default 60)"},
	{"Calibrate", c_calibrate, "[rev]  turn rev revolutions into a cup (default 20)"},
	{"CalibrateMl", c_calibrate_ml, "<mL>  sets v_rev_ml from the last Calibrate"},
	{"HoseReplaced", c_simple, "reset the hose revolution counter"},
	{"Save", c_simple, "store q_hat and the counters now"},
	{"TestAlarm", c_test_alarm, "[s]  beep for s seconds (default 3)"},
	{"Restart", c_restart, "1  reboot"},
	{"WifiConfig", c_wificonfig, "2  reboot into the Wi-Fi setup portal"},
	{"SSId", c_ssid, "[ssid]  Wi-Fi network (applied at restart)"},
	{"Password", c_password, "[pass]  Wi-Fi password, - for none (at restart)"},
	{"Time", c_time, "local time"},
	{"Backlog", c_backlog, "<cmd> [pl]; <cmd> [pl]; ...  several at once"},
};

/* Tasmota names for the netcfg keys. */
static const struct {
	const char *name;
	const char *key;
} net_names[] = {
	{"MqttHost", "mqtt_host"},     {"MqttPort", "mqtt_port"},
	{"MqttUser", "mqtt_user"},     {"MqttPassword", "mqtt_pass"},
	{"Topic", "topic"},            {"Discovery", "discovery"},
	{"DiscoveryPrefix", "disc_prefix"}, {"NtpServer", "ntp"},
};

static int run(const char *cmd, const char *pl, struct jw *w, bool nested);

static int c_backlog(const char *name, const char *pl, struct jw *w)
{
	char list[256];
	char *save = NULL;
	int err = 0;

	if (strlen(pl) >= sizeof(list)) {
		return error(w, name, "too long");
	}
	strcpy(list, pl);
	jw_key(w, name);
	jw_raw(w, "[");
	w->first = true;
	for (char *one = strtok_r(list, ";", &save); one; one = strtok_r(NULL, ";", &save)) {
		while (isspace((unsigned char)*one)) {
			one++;
		}
		char *sp = one;

		while (*sp && !isspace((unsigned char)*sp)) {
			sp++;
		}
		char *arg = sp;

		if (*sp) {
			*sp = '\0';
			arg = sp + 1;
		}
		if (*one == '\0') {
			continue;
		}
		jw_obj_open(w, NULL);
		err = run(one, arg, w, true) ?: err;
		jw_obj_close(w);
	}
	jw_raw(w, "]");
	w->first = false;
	return err;
}

static void trim(char *s)
{
	char *p = s;

	while (isspace((unsigned char)*p)) {
		p++;
	}
	memmove(s, p, strlen(p) + 1);
	for (size_t n = strlen(s); n > 0 && isspace((unsigned char)s[n - 1]); n--) {
		s[n - 1] = '\0';
	}
}

static int run(const char *cmd, const char *payload, struct jw *w, bool nested)
{
	char pl[128];

	if (strlen(payload) >= sizeof(pl)) {
		return error(w, cmd, "payload too long");
	}
	strcpy(pl, payload);
	trim(pl);

	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (strcasecmp(cmd, cmds[i].name) == 0) {
			if (nested && cmds[i].fn == c_backlog) {
				return error(w, cmd, "no Backlog inside Backlog");
			}
			return cmds[i].fn(cmds[i].name, pl, w);
		}
	}

	/* Tunables, by name or by their Tasmota equivalent. */
	static const struct {
		const char *alias;
		const char *name;
	} param_alias[] = {
		{"TelePeriod", "tele_period_s"},
		{"Timezone", "tz_h"},
	};
	const struct param_desc *d = params_find(cmd);

	for (size_t i = 0; !d && i < ARRAY_SIZE(param_alias); i++) {
		if (!strcasecmp(cmd, param_alias[i].alias)) {
			d = params_find(param_alias[i].name);
		}
	}

	if (d) {
		if (*pl) {
			int err = params_set(d->name, pl);

			if (err == -EINVAL) {
				return error(w, d->name, d->kind == PARAM_BOOL ? "0/1/on/off/toggle"
									       : "out of range");
			}
		}
		if (d->kind == PARAM_FLOAT) {
			jw_num(w, d->name, (double)params_value(d), 3);
		} else {
			jw_int(w, d->name, (long long)params_value(d));
		}
		return 0;
	}

	/* Network settings, by Tasmota name or by key. */
	const char *key = NULL;
	const char *shown = cmd;

	for (size_t i = 0; i < ARRAY_SIZE(net_names); i++) {
		if (!strcasecmp(cmd, net_names[i].name) || !strcasecmp(cmd, net_names[i].key)) {
			key = net_names[i].key;
			shown = net_names[i].name;
			break;
		}
	}
	if (key) {
		char val[72];

		if (*pl) {
			int err = netcfg_set(key, pl);

			if (err == -EINVAL) {
				return error(w, shown, "invalid value");
			}
		}
		netcfg_print(key, val, sizeof(val), false);
		jw_str(w, shown, val);
		return 0;
	}

	jw_str(w, "Command", "Unknown");
	return -ENOENT;
}

int cmnd_exec(const char *cmd, const char *payload, char *out, size_t size)
{
	struct jw w;
	int err;

	k_mutex_lock(&lock, K_FOREVER);
	jw_init(&w, out, size);
	jw_obj_open(&w, NULL);
	err = run(cmd, payload ? payload : "", &w, false);
	jw_obj_close(&w);
	if (w.overflow) {
		snprintk(out, size, "{\"%s\":\"Error\",\"Hint\":\"result too long\"}", cmd);
	}
	k_mutex_unlock(&lock);

	LOG_INF("%s %s -> %s", cmd, (payload && *payload) ? "(set)" : "(query)",
		err == 0 ? "ok" : "error");
	return err;
}

/* For the shell's help text. */
void cmnd_list(void (*print)(void *ctx, const char *name, const char *help), void *ctx)
{
	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
		print(ctx, cmds[i].name, cmds[i].help);
	}
	for (size_t i = 0; i < ARRAY_SIZE(net_names); i++) {
		print(ctx, net_names[i].name, "[value]  network setting");
	}
	print(ctx, "TelePeriod", "[s]  telemetry heartbeat (= tele_period_s)");
	print(ctx, "Timezone", "[h]  UTC offset (= tz_h)");
	print(ctx, "<tunable>", "[value]  any name from `pump params`");
}
