/*
 * JSON reports shared by the MQTT telemetry and the `Status` command, and a
 * tiny JSON writer for them.
 */

#ifndef PUMP_REPORT_H_
#define PUMP_REPORT_H_

#include <stdbool.h>
#include <stddef.h>

#include "app.h"

struct jw {
	char *buf;
	size_t size;
	size_t len;
	bool first;
	bool overflow;
};

void jw_init(struct jw *w, char *buf, size_t size);
void jw_raw(struct jw *w, const char *s);       /* verbatim */
void jw_key(struct jw *w, const char *key);     /* ,"key": */
void jw_str(struct jw *w, const char *key, const char *val);
void jw_num(struct jw *w, const char *key, double val, int decimals);
void jw_int(struct jw *w, const char *key, long long val);
void jw_bool(struct jw *w, const char *key, bool val);
void jw_obj_open(struct jw *w, const char *key); /* key NULL: bare { */
void jw_obj_close(struct jw *w);
void jw_escape(struct jw *w, const char *s);    /* string body, no quotes */

/* tele/<topic>/SENSOR: the pump. Returns the length, or -ENOSPC. */
int report_sensor(char *buf, size_t size, const struct app_status *st);
/* tele/<topic>/STATE: device and network diagnostics. */
int report_state(char *buf, size_t size);
/* stat/<topic>/PARAMS: every tunable. */
int report_params(char *buf, size_t size);

#endif /* PUMP_REPORT_H_ */
