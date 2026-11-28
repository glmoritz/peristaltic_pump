#include "timesync.h"

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/timeutil.h>

LOG_MODULE_REGISTER(timesync, LOG_LEVEL_INF);

#define NTP_TIMEOUT_MS 5000
#define MIN_VALID_UNIX 1735689600LL /* 2025-01-01 */

static atomic_t valid;

int timesync_now(const char *server)
{
	struct sntp_time t;
	int err = sntp_simple(server, NTP_TIMEOUT_MS, &t);

	if (err) {
		LOG_WRN("NTP %s failed (%d)", server, err);
		return err;
	}
	if ((int64_t)t.seconds < MIN_VALID_UNIX) {
		LOG_WRN("NTP %s returned an implausible time", server);
		return -ERANGE;
	}

	struct timespec ts = {
		.tv_sec = (time_t)t.seconds,
		.tv_nsec = (long)(((uint64_t)t.fraction * 1000000000ULL) >> 32),
	};

	err = sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
	if (err) {
		return err;
	}
	if (!atomic_set(&valid, 1)) {
		LOG_INF("clock set from %s", server);
	}
	return 0;
}

bool timesync_valid(void)
{
	return atomic_get(&valid) != 0;
}

static bool local_tm(float tz_h, struct tm *tm)
{
	struct timespec ts;

	if (!timesync_valid() || sys_clock_gettime(SYS_CLOCK_REALTIME, &ts) != 0) {
		return false;
	}
	time_t t = ts.tv_sec + (time_t)(tz_h * 3600.0f);

	return gmtime_r(&t, tm) != NULL;
}

bool timesync_local_hours(float tz_h, float *hours)
{
	struct tm tm;

	if (!local_tm(tz_h, &tm)) {
		return false;
	}
	*hours = (float)tm.tm_hour + (float)tm.tm_min / 60.0f + (float)tm.tm_sec / 3600.0f;
	return true;
}

void timesync_format(float tz_h, char *buf, size_t len)
{
	struct tm tm;

	if (!local_tm(tz_h, &tm)) {
		buf[0] = '\0';
		return;
	}
	snprintk(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
		 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

bool timesync_in_window(float h, float start, float end)
{
	if (start == end) {
		return false;
	}
	if (start < end) {
		return h >= start && h < end;
	}
	return h >= start || h < end;
}
