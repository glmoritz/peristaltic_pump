#include "netcfg.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(netcfg, LOG_LEVEL_INF);

static struct netcfg cfg;
static K_MUTEX_DEFINE(lock);
static atomic_t generation;
static char dev_id[13];

enum field_type { T_STR, T_PORT, T_BOOL };

struct field {
	const char *key;
	enum field_type type;
	size_t off;
	size_t size;
};

#define S(name) { #name, T_STR, offsetof(struct netcfg, name), sizeof(cfg.name) }

static const struct field fields[] = {
	S(mqtt_host),
	{ "mqtt_port", T_PORT, offsetof(struct netcfg, mqtt_port), sizeof(cfg.mqtt_port) },
	S(mqtt_user),
	S(mqtt_pass),
	S(topic),
	S(disc_prefix),
	{ "discovery", T_BOOL, offsetof(struct netcfg, discovery), sizeof(cfg.discovery) },
	S(ntp),
};

static const struct field *find(const char *key)
{
	for (size_t i = 0; i < ARRAY_SIZE(fields); i++) {
		if (strcasecmp(fields[i].key, key) == 0) {
			return &fields[i];
		}
	}
	return NULL;
}

const char *netcfg_device_id(void)
{
	if (dev_id[0] == '\0') {
		uint8_t id[8];
		ssize_t n = hwinfo_get_device_id(id, sizeof(id));

		if (n < 3) {
			strcpy(dev_id, "000000");
		} else {
			/* Last three bytes: on the ESP32 the id is the MAC. */
			snprintk(dev_id, sizeof(dev_id), "%02x%02x%02x", id[n - 3], id[n - 2],
				 id[n - 1]);
		}
	}
	return dev_id;
}

static void defaults(void)
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.mqtt_port = 1883;
	snprintk(cfg.topic, sizeof(cfg.topic), "condpump_%s", netcfg_device_id());
	strcpy(cfg.disc_prefix, "homeassistant");
	cfg.discovery = true;
	strcpy(cfg.ntp, "pool.ntp.org");
}

/* MQTT topic level: no wildcards, separators or spaces. */
static bool topic_ok(const char *s)
{
	if (*s == '\0') {
		return false;
	}
	for (; *s; s++) {
		if (*s == '#' || *s == '+' || *s == '/' || isspace((unsigned char)*s)) {
			return false;
		}
	}
	return true;
}

static int parse(const struct field *f, const char *value, struct netcfg *c)
{
	uint8_t *p = (uint8_t *)c + f->off;

	switch (f->type) {
	case T_STR:
		if (strlen(value) >= f->size) {
			return -EINVAL;
		}
		if ((!strcmp(f->key, "topic") || !strcmp(f->key, "disc_prefix")) &&
		    !topic_ok(value)) {
			return -EINVAL;
		}
		strcpy((char *)p, value);
		return 0;
	case T_PORT: {
		char *end;
		unsigned long v = strtoul(value, &end, 10);

		if (end == value || *end != '\0' || v == 0 || v > 65535) {
			return -EINVAL;
		}
		*(uint16_t *)p = (uint16_t)v;
		return 0;
	}
	case T_BOOL:
		if (!strcasecmp(value, "1") || !strcasecmp(value, "on")) {
			*(bool *)p = true;
		} else if (!strcasecmp(value, "0") || !strcasecmp(value, "off")) {
			*(bool *)p = false;
		} else {
			return -EINVAL;
		}
		return 0;
	}
	return -EINVAL;
}

int netcfg_print(const char *key, char *buf, size_t len, bool reveal)
{
	const struct field *f = find(key);

	if (f == NULL) {
		return -ENOENT;
	}
	k_mutex_lock(&lock, K_FOREVER);
	const uint8_t *p = (const uint8_t *)&cfg + f->off;

	switch (f->type) {
	case T_STR:
		if (!strcmp(f->key, "mqtt_pass") && !reveal) {
			snprintk(buf, len, "%s", ((const char *)p)[0] ? "****" : "");
		} else {
			snprintk(buf, len, "%s", (const char *)p);
		}
		break;
	case T_PORT:
		snprintk(buf, len, "%u", *(const uint16_t *)p);
		break;
	case T_BOOL:
		snprintk(buf, len, "%d", *(const bool *)p ? 1 : 0);
		break;
	}
	k_mutex_unlock(&lock);
	return 0;
}

int netcfg_set(const char *key, const char *value)
{
	const struct field *f = find(key);
	char skey[24];
	int err;

	if (f == NULL) {
		return -ENOENT;
	}

	k_mutex_lock(&lock, K_FOREVER);
	struct netcfg next = cfg;

	err = parse(f, value, &next);
	if (err == 0) {
		cfg = next;
		/* Store the text form: easy to read back whatever the type. */
		snprintk(skey, sizeof(skey), "net/%s", f->key);
		err = settings_save_one(skey, value, strlen(value) + 1);
		atomic_inc(&generation);
	}
	k_mutex_unlock(&lock);
	return err;
}

void netcfg_get(struct netcfg *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	*out = cfg;
	k_mutex_unlock(&lock);
}

uint32_t netcfg_generation(void)
{
	return (uint32_t)atomic_get(&generation);
}

static int h_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const struct field *f = find(key);
	char buf[72];
	ssize_t n;

	if (f == NULL || len == 0 || len > sizeof(buf)) {
		return 0;
	}
	n = read_cb(cb_arg, buf, len);
	if (n <= 0) {
		return 0;
	}
	buf[MIN((size_t)n, sizeof(buf) - 1)] = '\0';
	if (parse(f, buf, &cfg) != 0) {
		LOG_WRN("stored net/%s is invalid, ignored", f->key);
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(netcfg, "net", NULL, h_set, NULL, NULL);

int netcfg_init(void)
{
	defaults();
	int err = settings_load_subtree("net");

	LOG_INF("MQTT %s:%u, topic %s", cfg.mqtt_host[0] ? cfg.mqtt_host : "(not set)",
		cfg.mqtt_port, cfg.topic);
	return err;
}
