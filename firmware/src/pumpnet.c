/*
 * Network thread: Wi-Fi, NTP, MQTT + Home Assistant discovery (net.h).
 *
 * Runs at NET_PRIO, below the control loop. It only ever reads the control
 * loop's snapshot and event queue (app.h), so a slow broker, a dead Wi-Fi or
 * a blocked socket stall this thread and nothing else.
 */

#include "pumpnet.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/atomic.h>

#include "app.h"
#include "cmnd.h"
#include "netcfg.h"
#include "params.h"
#include "report.h"
#include "timesync.h"
#include "wifi_prov.h"

LOG_MODULE_REGISTER(net, LOG_LEVEL_INF);

#define NET_PRIO        10
#define NET_STACK       6144
#define POLL_MS         250
#define CONNACK_MS      10000
#define BACKOFF_MAX_S   60
#define NTP_PERIOD_MS   (6LL * 3600 * 1000)
#define NTP_RETRY_MS    (5LL * 60 * 1000)
#define SENSOR_MIN_MS   1000 /* on-change telemetry, at most 1/s */

static K_THREAD_STACK_DEFINE(net_stack, NET_STACK);
static struct k_thread net_thread_data;

static atomic_t shutdown_req;
static atomic_t mqtt_up;
static atomic_t mqtt_connects;

/* MQTT session state: only the net thread touches it. */
static struct mqtt_client client;
static struct net_sockaddr_storage broker;
static uint8_t rx_buf[512];
static uint8_t tx_buf[512];
static struct netcfg cfg;
static bool connected;
static bool rediscover;
static uint16_t msg_id;

static char topic_buf[128];
static char payload[1024];
static char cmd_in[256];
static char result[1536];

/* ---- Small helpers --------------------------------------------------------- */

static const char *mk_topic(const char *prefix, const char *leaf)
{
	snprintk(topic_buf, sizeof(topic_buf), "%s/%s/%s", prefix, cfg.topic, leaf);
	return topic_buf;
}

static int pub(const char *topic, const char *data, bool retain)
{
	struct mqtt_publish_param p = {0};

	p.message.topic.topic.utf8 = (const uint8_t *)topic;
	p.message.topic.topic.size = strlen(topic);
	p.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	p.message.payload.data = (uint8_t *)data;
	p.message.payload.len = strlen(data);
	p.message_id = ++msg_id;
	p.retain_flag = retain;
	return mqtt_publish(&client, &p);
}

static const char *on_off(bool b)
{
	return b ? "ON" : "OFF";
}

/* ---- Home Assistant discovery ---------------------------------------------- */

enum comp { C_SENSOR, C_BINARY, C_NUMBER, C_SWITCH, C_BUTTON };

static const char *const comp_name[] = {"sensor", "binary_sensor", "number", "switch",
					"button"};

/*
 * One entity. Topics are "<prefix>:<leaf>", e.g. "tele:SENSOR"; for buttons
 * and numbers `stat` is the command topic. `extra` is a raw JSON fragment
 * appended to the config.
 */
struct ent {
	uint8_t comp;
	const char *id;
	const char *name;
	const char *stat;
	const char *key;   /* value_json.<key>, or NULL for a raw payload */
	const char *unit;
	const char *dev_cla;
	const char *stat_cla;
	const char *icon;
	const char *extra;
};

#define DIAG "\"ent_cat\":\"diagnostic\""
#define CONF "\"ent_cat\":\"config\""

static const struct ent ents[] = {
	/* Pump telemetry: these are the graphs. */
	{C_SENSOR, "flow", "Pump flow", "tele:SENSOR", "flow", "mL/h", NULL, "measurement",
	 "mdi:pump", NULL},
	{C_SENSOR, "target", "Commanded flow", "tele:SENSOR", "target", "mL/h", NULL,
	 "measurement", "mdi:speedometer", NULL},
	{C_SENSOR, "rpm", "Rotor speed", "tele:SENSOR", "rpm", "rpm", NULL, "measurement",
	 "mdi:rotate-right", "\"sug_dsp_prc\":2"},
	{C_SENSOR, "inflow", "Condensate inflow", "tele:SENSOR", "inflow", "mL/h", NULL,
	 "measurement", "mdi:water-plus", NULL},
	{C_SENSOR, "measured", "Last measured inflow", "tele:SENSOR", "measured", "mL/h", NULL,
	 "measurement", "mdi:water-check", NULL},
	{C_SENSOR, "total", "Water pumped", "tele:SENSOR", "total", "L", "water",
	 "total_increasing", NULL, "\"sug_dsp_prc\":2"},
	{C_SENSOR, "hose_rev", "Hose revolutions", "tele:SENSOR", "hose_rev", "rev", NULL,
	 "total_increasing", "mdi:counter", NULL},
	{C_SENSOR, "hose_wear", "Hose wear", "tele:SENSOR", "hose_wear", "%", NULL,
	 "measurement", "mdi:progress-wrench", NULL},
	{C_SENSOR, "state", "Controller state", "stat:STATE", NULL, NULL, "enum", NULL,
	 "mdi:state-machine",
	 "\"ops\":[\"idle\",\"fill\",\"drain\",\"emergency\",\"sensor_fault\"]"},
	{C_SENSOR, "boosts", "Boosts", "tele:SENSOR", "boosts", NULL, NULL, "total_increasing",
	 "mdi:arrow-up-bold", DIAG},
	{C_SENSOR, "emergencies", "Emergencies", "tele:SENSOR", "emergencies", NULL, NULL,
	 "total_increasing", "mdi:alert", DIAG},
	{C_SENSOR, "n_meas", "Inflow measurements", "tele:SENSOR", "n_meas", NULL, NULL,
	 "total_increasing", "mdi:counter", DIAG},
	/* Device. */
	{C_SENSOR, "rssi", "Wi-Fi signal", "tele:STATE", "rssi", "dBm", "signal_strength",
	 "measurement", NULL, DIAG},
	{C_SENSOR, "uptime", "Uptime", "tele:STATE", "uptime", "s", "duration", NULL, NULL,
	 DIAG},
	{C_SENSOR, "ip", "IP address", "tele:STATE", "ip", NULL, NULL, NULL, "mdi:ip-network",
	 DIAG},
	/* Level sensors and alarms: one retained topic each, every edge. */
	{C_BINARY, "low", "Level low", "stat:LOW", NULL, NULL, "moisture", NULL, NULL, NULL},
	{C_BINARY, "high", "Level high", "stat:HIGH", NULL, NULL, "moisture", NULL, NULL, NULL},
	{C_BINARY, "problem", "Problem", "stat:PROBLEM", NULL, NULL, "problem", NULL, NULL,
	 NULL},
	{C_BINARY, "sensor_fault", "Sensor fault", "stat:SFAULT", NULL, NULL, "problem", NULL,
	 NULL, NULL},
	{C_BINARY, "hose_worn", "Hose worn", "stat:HOSE", NULL, NULL, "problem", NULL, NULL,
	 NULL},
	{C_BINARY, "manual", "Manual override", "stat:MANUAL", NULL, NULL, "running", NULL,
	 "mdi:hand-back-right", NULL},
	{C_BINARY, "night", "Night cap active", "stat:NIGHT", NULL, NULL, NULL, NULL,
	 "mdi:weather-night", NULL},
	/* Undebounced inputs, for wiring checks: off by default in HA. */
	{C_BINARY, "low_raw", "Level low (raw)", "tele:SENSOR", "low_raw", NULL, "moisture",
	 NULL, NULL, DIAG ",\"en\":false"},
	{C_BINARY, "high_raw", "Level high (raw)", "tele:SENSOR", "high_raw", NULL, "moisture",
	 NULL, NULL, DIAG ",\"en\":false"},
	/* Actions. `stat` holds the command topic, `key` the payload. */
	{C_BUTTON, "hose_replaced", "Hose replaced", "cmnd:HoseReplaced", "1", NULL, NULL, NULL,
	 "mdi:pipe", NULL},
	{C_BUTTON, "purge", "Purge 1 min", "cmnd:Purge", "60", NULL, NULL, NULL,
	 "mdi:water-pump", NULL},
	{C_BUTTON, "calibrate", "Calibrate (20 rev)", "cmnd:Calibrate", "20", NULL, NULL, NULL,
	 "mdi:beaker-outline", CONF},
	{C_BUTTON, "auto", "Automatic mode", "cmnd:Auto", "1", NULL, NULL, NULL, "mdi:autorenew",
	 NULL},
	{C_BUTTON, "test_alarm", "Test alarm", "cmnd:TestAlarm", "3", NULL, NULL, NULL,
	 "mdi:bell-ring", DIAG},
	{C_BUTTON, "restart", "Restart", "cmnd:Restart", "1", NULL, "restart", NULL, NULL, CONF},
	/* Numbers that are commands, not tunables. */
	{C_NUMBER, "manual_flow", "Manual flow", "cmnd:Manual", "manual_q", "mL/h", NULL, NULL,
	 "mdi:hand-water",
	 "\"min\":0,\"max\":12000,\"step\":10,\"mode\":\"box\""},
	{C_NUMBER, "calibration_ml", "Calibration volume", "cmnd:CalibrateMl", NULL, "mL", NULL,
	 NULL, "mdi:beaker-check", "\"min\":0,\"max\":1000,\"step\":0.5,\"mode\":\"box\"," CONF},
};

/* "tele:SENSOR" -> "tele/<t>/SENSOR" into w (JSON string body). */
static void jw_topic(struct jw *w, const char *spec)
{
	const char *colon = strchr(spec, ':');
	char prefix[8];
	size_t n = colon ? MIN((size_t)(colon - spec), sizeof(prefix) - 1) : 0;

	memcpy(prefix, spec, n);
	prefix[n] = '\0';
	jw_escape(w, prefix);
	jw_raw(w, "/");
	jw_escape(w, cfg.topic);
	jw_raw(w, "/");
	jw_escape(w, colon ? colon + 1 : spec);
}

static void jw_topic_key(struct jw *w, const char *key, const char *spec)
{
	jw_key(w, key);
	jw_raw(w, "\"");
	jw_topic(w, spec);
	jw_raw(w, "\"");
}

static void disc_common(struct jw *w, const char *id, const char *name)
{
	char uid[64];

	snprintk(uid, sizeof(uid), "%s_%s", cfg.topic, id);
	jw_str(w, "name", name);
	jw_str(w, "uniq_id", uid);
	jw_topic_key(w, "avty_t", "tele:LWT");
	jw_str(w, "pl_avail", "Online");
	jw_str(w, "pl_not_avail", "Offline");
	jw_obj_open(w, "dev");
	jw_key(w, "ids");
	jw_raw(w, "[\"");
	jw_escape(w, cfg.topic);
	jw_raw(w, "\"]");
	jw_str(w, "name", "Condensate pump");
	jw_str(w, "mdl", "Peristaltic condensate pump");
	jw_str(w, "mf", "DIY");
	jw_str(w, "sw", APP_VERSION_STRING);
	jw_obj_close(w);
	jw_obj_open(w, "o");
	jw_str(w, "name", "peristaltic_pump");
	jw_str(w, "sw", APP_VERSION_STRING);
	jw_obj_close(w);
}

static void disc_entity(struct jw *w, const struct ent *e)
{
	char tpl[96];

	disc_common(w, e->id, e->name);

	if (e->comp == C_BUTTON || e->comp == C_NUMBER) {
		jw_topic_key(w, "cmd_t", e->stat);
		if (e->comp == C_BUTTON) {
			jw_str(w, "pl_prs", e->key);
		} else if (e->key) {
			/* State from the telemetry; without one HA is optimistic. */
			jw_topic_key(w, "stat_t", "tele:SENSOR");
			snprintk(tpl, sizeof(tpl), "{{ value_json.%s }}", e->key);
			jw_str(w, "val_tpl", tpl);
		}
	} else {
		jw_topic_key(w, "stat_t", e->stat);
		if (e->key && e->comp == C_BINARY) {
			snprintk(tpl, sizeof(tpl),
				 "{{ 'ON' if value_json.%s else 'OFF' }}", e->key);
			jw_str(w, "val_tpl", tpl);
		} else if (e->key) {
			snprintk(tpl, sizeof(tpl), "{{ value_json.%s }}", e->key);
			jw_str(w, "val_tpl", tpl);
		}
		if (!strcmp(e->stat, "tele:STATE") && !strcmp(e->id, "rssi")) {
			jw_topic_key(w, "json_attr_t", "tele:STATE");
		}
	}
	if (e->unit) {
		jw_str(w, "unit_of_meas", e->unit);
	}
	if (e->dev_cla) {
		jw_str(w, "dev_cla", e->dev_cla);
	}
	if (e->stat_cla) {
		jw_str(w, "stat_cla", e->stat_cla);
	}
	if (e->icon) {
		jw_str(w, "ic", e->icon);
	}
	if (e->extra) {
		jw_raw(w, ",");
		jw_raw(w, e->extra);
	}
}

static void disc_param(struct jw *w, const struct param_desc *d)
{
	char tpl[80];
	char cmd[40];

	disc_common(w, d->name, d->label);
	snprintk(cmd, sizeof(cmd), "cmnd:%s", d->name);
	jw_topic_key(w, "cmd_t", cmd);
	jw_topic_key(w, "stat_t", "stat:PARAMS");
	if (d->kind == PARAM_BOOL) {
		snprintk(tpl, sizeof(tpl), "{{ 'ON' if value_json.%s else 'OFF' }}", d->name);
		jw_str(w, "val_tpl", tpl);
	} else {
		snprintk(tpl, sizeof(tpl), "{{ value_json.%s }}", d->name);
		jw_str(w, "val_tpl", tpl);
		jw_num(w, "min", (double)d->min, 3);
		jw_num(w, "max", (double)d->max, 3);
		jw_num(w, "step", (double)d->step, 3);
		jw_str(w, "mode", "box");
		if (d->unit[0]) {
			jw_str(w, "unit_of_meas", d->unit);
		}
	}
	jw_str(w, "ent_cat", "config");
}

static int disc_publish_one(enum comp c, const char *id, const char *data)
{
	char t[128];

	snprintk(t, sizeof(t), "%s/%s/%s/%s/config", cfg.disc_prefix, comp_name[c], cfg.topic,
		 id);
	return pub(t, data, true);
}

/* Publish every config, or an empty one for each to remove them. */
static int discovery(bool enable)
{
	struct jw w;
	int err = 0;

	for (size_t i = 0; i < ARRAY_SIZE(ents) && !err; i++) {
		jw_init(&w, payload, sizeof(payload));
		if (enable) {
			jw_obj_open(&w, NULL);
			disc_entity(&w, &ents[i]);
			jw_obj_close(&w);
		}
		if (w.overflow) {
			LOG_ERR("discovery %s too long", ents[i].id);
			continue;
		}
		err = disc_publish_one(ents[i].comp, ents[i].id, payload);
	}
	for (size_t i = 0; i < params_count() && !err; i++) {
		const struct param_desc *d = params_get(i);

		jw_init(&w, payload, sizeof(payload));
		if (enable) {
			jw_obj_open(&w, NULL);
			disc_param(&w, d);
			jw_obj_close(&w);
		}
		if (w.overflow) {
			LOG_ERR("discovery %s too long", d->name);
			continue;
		}
		err = disc_publish_one(d->kind == PARAM_BOOL ? C_SWITCH : C_NUMBER, d->name,
				       payload);
	}
	if (!err) {
		LOG_INF("HA discovery %s (%u entities)", enable ? "published" : "removed",
			(unsigned int)(ARRAY_SIZE(ents) + params_count()));
	}
	return err;
}

/* ---- State publishing ------------------------------------------------------ */

static const char *const ev_leaf[APP_EV_COUNT] = {
	[APP_EV_LOW] = "LOW",       [APP_EV_HIGH] = "HIGH",
	[APP_EV_STATE] = "STATE",   [APP_EV_PROBLEM] = "PROBLEM",
	[APP_EV_SENSOR_FAULT] = "SFAULT", [APP_EV_HOSE_WORN] = "HOSE",
	[APP_EV_MANUAL] = "MANUAL", [APP_EV_NIGHT] = "NIGHT",
};

static int pub_event(const struct app_event *ev)
{
	if (ev->type >= APP_EV_COUNT) {
		return 0;
	}
	const char *val = (ev->type == APP_EV_STATE) ? ctl_state_name((enum ctl_state)ev->value)
						     : on_off(ev->value);

	return pub(mk_topic("stat", ev_leaf[ev->type]), val, true);
}

static struct app_status last_sent;
static int64_t last_sensor_ms;
static int64_t last_tele_ms;
static uint32_t last_params_gen;

static int pub_sensor(const struct app_status *st, int64_t now)
{
	if (report_sensor(payload, sizeof(payload), st) < 0) {
		return -ENOSPC;
	}
	last_sent = *st;
	last_sensor_ms = now;
	return pub(mk_topic("tele", "SENSOR"), payload, false);
}

static int pub_params(void)
{
	last_params_gen = params_generation();
	if (report_params(payload, sizeof(payload)) < 0) {
		return -ENOSPC;
	}
	return pub(mk_topic("stat", "PARAMS"), payload, true);
}

static int pub_state(void)
{
	if (report_state(payload, sizeof(payload)) < 0) {
		return -ENOSPC;
	}
	return pub(mk_topic("tele", "STATE"), payload, false);
}

/* Everything, from one snapshot: after a (re)connect or an HA restart. */
static int publish_all(void)
{
	struct app_status st;
	int64_t now = k_uptime_get();
	int err;

	app_events_purge(); /* the snapshot below supersedes them */
	app_get_status(&st);

	struct app_event evs[] = {
		{APP_EV_LOW, st.low_wet},
		{APP_EV_HIGH, st.high_wet},
		{APP_EV_STATE, (uint8_t)st.state},
		{APP_EV_PROBLEM, st.problem},
		{APP_EV_SENSOR_FAULT, st.sensor_fault},
		{APP_EV_HOSE_WORN, st.hose_worn},
		{APP_EV_MANUAL, st.manual},
		{APP_EV_NIGHT, st.night},
	};

	for (size_t i = 0; i < ARRAY_SIZE(evs); i++) {
		err = pub_event(&evs[i]);
		if (err) {
			return err;
		}
	}
	err = pub_params();
	err = err ?: pub_sensor(&st, now);
	err = err ?: pub_state();
	last_tele_ms = now;
	return err;
}

/* Worth a SENSOR message now? The flow is what HA graphs, so it gets the
 * finest threshold; the rest go out on any visible change. */
static bool sensor_changed(const struct app_status *a, const struct app_status *b)
{
	float df = fabsf(a->flow_mlh - b->flow_mlh);
	float tol = fmaxf(10.0f, 0.02f * fmaxf(a->flow_mlh, b->flow_mlh));

	return df >= tol || fabsf(a->q_cmd_mlh - b->q_cmd_mlh) >= 1.0f ||
	       fabsf(a->q_hat_mlh - b->q_hat_mlh) >= 1.0f ||
	       a->n_measurements != b->n_measurements || a->manual != b->manual ||
	       a->calibrating != b->calibrating || a->state != b->state ||
	       a->muted != b->muted || a->low_raw != b->low_raw || a->high_raw != b->high_raw ||
	       /* flow reached exactly zero: show the stop, not 9 mL/h */
	       (a->flow_mlh == 0.0f) != (b->flow_mlh == 0.0f);
}

/* ---- MQTT ------------------------------------------------------------------ */

static void handle_publish(struct mqtt_client *c, const struct mqtt_publish_param *p)
{
	const char *t = (const char *)p->message.topic.topic.utf8;
	size_t tlen = p->message.topic.topic.size;
	size_t len = p->message.payload.len;
	char prefix[64];
	char name[40];

	if (len >= sizeof(cmd_in)) {
		/* Drain and drop: no command is this long. */
		uint8_t sink[64];

		while (len > 0) {
			int n = mqtt_read_publish_payload_blocking(c, sink, MIN(len, sizeof(sink)));

			if (n <= 0) {
				return;
			}
			len -= (size_t)n;
		}
		LOG_WRN("oversized message dropped");
		return;
	}
	if (mqtt_readall_publish_payload(c, (uint8_t *)cmd_in, len) != 0) {
		return;
	}
	cmd_in[len] = '\0';

	/* HA restarted: it lost the non-retained state, send it all again. */
	snprintk(prefix, sizeof(prefix), "%s/status", cfg.disc_prefix);
	if (tlen == strlen(prefix) && !strncmp(t, prefix, tlen)) {
		if (!strcmp(cmd_in, "online")) {
			rediscover = true;
		}
		return;
	}

	snprintk(prefix, sizeof(prefix), "cmnd/%s/", cfg.topic);
	size_t plen = strlen(prefix);

	if (tlen <= plen || strncmp(t, prefix, plen) != 0 || tlen - plen >= sizeof(name)) {
		return;
	}
	memcpy(name, t + plen, tlen - plen);
	name[tlen - plen] = '\0';

	cmnd_exec(name, cmd_in, result, sizeof(result));
	(void)pub(mk_topic("stat", "RESULT"), result, false);
}

static void mqtt_evt(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result == 0) {
			connected = true;
		} else {
			LOG_ERR("broker refused the connection (%d)", evt->result);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		connected = false;
		break;
	case MQTT_EVT_PUBLISH:
		handle_publish(c, &evt->param.publish);
		break;
	default:
		break;
	}
}

static int resolve(void)
{
	struct zsock_addrinfo hints = {
		.ai_family = NET_AF_UNSPEC,
		.ai_socktype = NET_SOCK_STREAM,
	};
	struct zsock_addrinfo *res = NULL;
	char port[8];
	int err;

	snprintk(port, sizeof(port), "%u", cfg.mqtt_port);
	err = zsock_getaddrinfo(cfg.mqtt_host, port, &hints, &res);
	if (err || !res) {
		LOG_WRN("cannot resolve %s (%d)", cfg.mqtt_host, err);
		return -EHOSTUNREACH;
	}
	memset(&broker, 0, sizeof(broker));
	memcpy(&broker, res->ai_addr, MIN((size_t)res->ai_addrlen, sizeof(broker)));
	zsock_freeaddrinfo(res);
	return 0;
}

static struct mqtt_utf8 user, pass, will_msg;
static struct mqtt_topic will_topic;
static char will_topic_str[64];

static void client_setup(void)
{
	mqtt_client_init(&client);
	client.broker = &broker;
	client.evt_cb = mqtt_evt;
	client.client_id.utf8 = (const uint8_t *)cfg.topic;
	client.client_id.size = strlen(cfg.topic);
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;
	client.clean_session = 1;

	if (cfg.mqtt_user[0]) {
		user.utf8 = (const uint8_t *)cfg.mqtt_user;
		user.size = strlen(cfg.mqtt_user);
		client.user_name = &user;
		pass.utf8 = (const uint8_t *)cfg.mqtt_pass;
		pass.size = strlen(cfg.mqtt_pass);
		client.password = &pass;
	}

	snprintk(will_topic_str, sizeof(will_topic_str), "tele/%s/LWT", cfg.topic);
	will_topic.topic.utf8 = (const uint8_t *)will_topic_str;
	will_topic.topic.size = strlen(will_topic_str);
	will_topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	will_msg.utf8 = (const uint8_t *)"Offline";
	will_msg.size = strlen("Offline");
	client.will_topic = &will_topic;
	client.will_message = &will_msg;
	client.will_retain = 1;
}

static int wait_readable(int ms)
{
	struct zsock_pollfd fd = {.fd = client.transport.tcp.sock, .events = ZSOCK_POLLIN};
	int n = zsock_poll(&fd, 1, ms);

	if (n < 0) {
		return -errno;
	}
	if (n > 0 && (fd.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL))) {
		return -ECONNRESET;
	}
	return n;
}

static void maybe_ntp(int64_t *next_ntp)
{
	int64_t now = k_uptime_get();

	if (now < *next_ntp) {
		return;
	}
	*next_ntp = now + ((timesync_now(cfg.ntp) == 0) ? NTP_PERIOD_MS : NTP_RETRY_MS);
}

/*
 * One broker session. Returns when the connection is lost (error), or with
 * -ECANCELED when the network settings changed or a reboot is coming.
 */
static int session(int64_t *next_ntp)
{
	uint32_t gen = netcfg_generation();
	int err;

	err = resolve();
	if (err) {
		return err;
	}
	client_setup();
	connected = false;
	rediscover = false;

	err = mqtt_connect(&client);
	if (err) {
		LOG_WRN("MQTT connect to %s:%u failed (%d)", cfg.mqtt_host, cfg.mqtt_port, err);
		return err;
	}

	int64_t deadline = k_uptime_get() + CONNACK_MS;

	while (!connected && k_uptime_get() < deadline) {
		err = wait_readable(500);
		if (err < 0) {
			break;
		}
		if (err > 0 && mqtt_input(&client) != 0) {
			break;
		}
	}
	if (!connected) {
		LOG_WRN("no CONNACK from %s", cfg.mqtt_host);
		mqtt_abort(&client);
		return -ETIMEDOUT;
	}

	atomic_set(&mqtt_up, 1);
	atomic_inc(&mqtt_connects);
	LOG_INF("MQTT connected to %s:%u as %s", cfg.mqtt_host, cfg.mqtt_port, cfg.topic);

	char sub_cmnd[48], sub_ha[48];

	snprintk(sub_cmnd, sizeof(sub_cmnd), "cmnd/%s/+", cfg.topic);
	snprintk(sub_ha, sizeof(sub_ha), "%s/status", cfg.disc_prefix);
	struct mqtt_topic subs[] = {
		{.topic = {.utf8 = (uint8_t *)sub_cmnd, .size = strlen(sub_cmnd)},
		 .qos = MQTT_QOS_0_AT_MOST_ONCE},
		{.topic = {.utf8 = (uint8_t *)sub_ha, .size = strlen(sub_ha)},
		 .qos = MQTT_QOS_0_AT_MOST_ONCE},
	};
	struct mqtt_subscription_list sl = {
		.list = subs, .list_count = ARRAY_SIZE(subs), .message_id = ++msg_id};

	err = mqtt_subscribe(&client, &sl);
	err = err ?: pub(mk_topic("tele", "LWT"), "Online", true);
	err = err ?: discovery(cfg.discovery);
	err = err ?: publish_all();

	while (err == 0 && connected) {
		int64_t now = k_uptime_get();

		if (atomic_get(&shutdown_req)) {
			(void)pub(mk_topic("tele", "LWT"), "Offline", true);
			mqtt_disconnect(&client, NULL);
			err = -ECANCELED;
			break;
		}
		if (netcfg_generation() != gen) {
			LOG_INF("network settings changed: reconnecting");
			(void)pub(mk_topic("tele", "LWT"), "Offline", true);
			mqtt_disconnect(&client, NULL);
			err = -ECANCELED;
			break;
		}

		/* Inputs: commands, acks, pings. */
		int r = wait_readable(POLL_MS);

		if (r < 0) {
			err = r;
			break;
		}
		if (r > 0) {
			err = mqtt_input(&client);
			if (err) {
				break;
			}
		}
		err = mqtt_live(&client);
		if (err == -EAGAIN) {
			err = 0;
		}
		if (err) {
			break;
		}
		if (client.unacked_ping > 1) {
			LOG_WRN("broker stopped answering pings");
			err = -ETIMEDOUT;
			break;
		}

		/* Edges from the control loop, in order. */
		struct app_event ev;
		bool any_event = false;

		while (err == 0 && app_event_get(&ev) == 0) {
			err = pub_event(&ev);
			any_event = true;
		}

		if (rediscover) {
			rediscover = false;
			err = err ?: discovery(cfg.discovery);
			err = err ?: publish_all();
			continue;
		}
		if (params_generation() != last_params_gen) {
			err = err ?: pub_params();
		}

		now = k_uptime_get();
		struct app_status st;

		app_get_status(&st);
		bool tele_due = now - last_tele_ms >= (int64_t)g_par.tele_period_s * 1000;

		if (tele_due || ((any_event || sensor_changed(&st, &last_sent)) &&
				 now - last_sensor_ms >= SENSOR_MIN_MS)) {
			err = err ?: pub_sensor(&st, now);
		}
		if (tele_due) {
			err = err ?: pub_state();
			last_tele_ms = now;
		}

		maybe_ntp(next_ntp);
	}

	atomic_set(&mqtt_up, 0);
	if (err != -ECANCELED) {
		LOG_WRN("MQTT session ended (%d)", err);
		mqtt_abort(&client);
	}
	return err;
}

/* ---- Portal fields ----------------------------------------------------------- */

static const struct wifi_prov_field portal_fields[] = {
	{"mqtt_host", "MQTT broker (host or IP, empty = no MQTT)", 63, false},
	{"mqtt_port", "MQTT port", 5, false},
	{"mqtt_user", "MQTT user", 31, false},
	{"mqtt_pass", "MQTT password", 63, true},
	{"topic", "Device topic", 31, false},
};

static void portal_get(const char *key, char *buf, size_t len)
{
	(void)netcfg_print(key, buf, len, false);
}

static void portal_set(const char *key, const char *value)
{
	char cur[72];

	/* Unchanged fields must not bump the generation for nothing. */
	if (netcfg_print(key, cur, sizeof(cur), true) == 0 && !strcmp(cur, value)) {
		return;
	}
	if (netcfg_set(key, value) != 0) {
		LOG_WRN("portal: invalid %s ignored", key);
	}
}

static void wifi_cb(enum wifi_prov_state state, const char *info)
{
	if (state == WIFI_PROV_CONNECTED) {
		LOG_INF("network up: %s", info);
	} else if (state == WIFI_PROV_AP_ACTIVE) {
		LOG_WRN("setup portal open: join Wi-Fi \"%s\", then http://192.168.4.1/", info);
	}
}

/* ---- Thread -------------------------------------------------------------------- */

static void net_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int64_t next_ntp = 0;
	uint32_t backoff_s = 1;

	(void)netcfg_init();
	wifi_prov_init(wifi_cb);
	wifi_prov_set_fields(portal_fields, ARRAY_SIZE(portal_fields), portal_get, portal_set);
	wifi_prov_start(); /* returns once connected; later drops self-heal */

	for (;;) {
		if (atomic_get(&shutdown_req)) {
			k_sleep(K_FOREVER);
		}
		if (wifi_prov_get_state() != WIFI_PROV_CONNECTED) {
			k_sleep(K_SECONDS(1));
			continue;
		}
		netcfg_get(&cfg);
		maybe_ntp(&next_ntp);

		if (cfg.mqtt_host[0] == '\0') {
			k_sleep(K_SECONDS(2));
			continue;
		}

		int64_t started = k_uptime_get();
		int err = session(&next_ntp);

		if (err == -ECANCELED) {
			backoff_s = 1;
			continue;
		}
		/* A session that lasted a while was healthy: start over. */
		if (k_uptime_get() - started > 60 * 1000) {
			backoff_s = 1;
		}
		LOG_INF("MQTT retry in %u s", backoff_s);
		k_sleep(K_SECONDS(backoff_s));
		backoff_s = MIN(backoff_s * 2, BACKOFF_MAX_S);
	}
}

void pumpnet_start(void)
{
	k_thread_create(&net_thread_data, net_stack, K_THREAD_STACK_SIZEOF(net_stack), net_thread,
			NULL, NULL, NULL, NET_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&net_thread_data, "net");
}

void pumpnet_shutdown(void)
{
	atomic_set(&shutdown_req, 1);
}

void pumpnet_get_stats(struct pumpnet_stats *out)
{
	out->mqtt_up = atomic_get(&mqtt_up) != 0;
	out->mqtt_connects = (uint32_t)atomic_get(&mqtt_connects);
}
