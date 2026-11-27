#include "level.h"

#include "hw.h"

struct debounced {
	bool raw;
	bool stable;
	int64_t since_ms;
	bool primed;
};

static struct debounced low, high;

static void debounce(struct debounced *s, bool raw, int64_t now_ms)
{
	if (!s->primed) {
		/* First sample: trust it, so boot does not wait for the debounce. */
		*s = (struct debounced){ raw, raw, now_ms, true };
		return;
	}
	if (raw != s->raw) {
		s->raw = raw;
		s->since_ms = now_ms;
	}
	if (s->stable != s->raw && now_ms - s->since_ms >= CONFIG_PUMP_DEBOUNCE_MS) {
		s->stable = s->raw;
	}
}

int level_init(void)
{
	return level_hw_init();
}

void level_tick(int64_t now_ms)
{
	bool l, h;

	level_hw_read(&l, &h);
	debounce(&low, l, now_ms);
	debounce(&high, h, now_ms);
}

bool level_low_wet(void)
{
	return low.stable;
}

bool level_high_wet(void)
{
	return high.stable;
}

bool level_low_raw(void)
{
	return low.raw;
}

bool level_high_raw(void)
{
	return high.raw;
}
