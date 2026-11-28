/* Wall-clock time from NTP (the C3 has no battery RTC), for night mode. */

#ifndef PUMP_TIMESYNC_H_
#define PUMP_TIMESYNC_H_

#include <stdbool.h>
#include <stddef.h>

/* One SNTP query; sets the realtime clock. Blocks up to a few seconds:
 * network thread only. */
int timesync_now(const char *server);

/* The realtime clock has been set since boot. */
bool timesync_valid(void);

/* Local time of day in hours [0, 24), for UTC + tz_h. False if not valid. */
bool timesync_local_hours(float tz_h, float *hours);

/* "2026-11-28T22:15:03" local time, or "" when not valid. */
void timesync_format(float tz_h, char *buf, size_t len);

/* h inside [start, end), wrapping midnight when start > end; never when
 * start == end. */
bool timesync_in_window(float h, float start, float end);

#endif /* PUMP_TIMESYNC_H_ */
