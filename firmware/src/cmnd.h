/*
 * Tasmota-style commands: `<Command> [payload]`, case-insensitive, an empty
 * payload queries, the result is a JSON object. The same table serves
 *
 *   MQTT   cmnd/<topic>/<Command>  payload   ->  stat/<topic>/RESULT
 *   shell  cmnd <Command> [payload]
 *
 * Every tunable of `pump params` is a command of its own name. The full list
 * is in firmware/README.md.
 */

#ifndef PUMP_CMND_H_
#define PUMP_CMND_H_

#include <stddef.h>

/* 0, -ENOENT (unknown command) or -EINVAL; `out` holds the JSON either way. */
int cmnd_exec(const char *cmd, const char *payload, char *out, size_t size);

/* Walk the command list (shell help). */
void cmnd_list(void (*print)(void *ctx, const char *name, const char *help), void *ctx);

#endif /* PUMP_CMND_H_ */
