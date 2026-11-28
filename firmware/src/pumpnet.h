/*
 * Network side: Wi-Fi provisioning, NTP, and the MQTT session with Home
 * Assistant discovery, all in one thread below the control loop's priority.
 *
 * Topics (Tasmota layout, <t> = netcfg topic, default condpump_<id>):
 *
 *   tele/<t>/LWT       Online / Offline (retained, last will)
 *   tele/<t>/SENSOR    pump telemetry JSON: on change (max 1/s) and every
 *                      tele_period_s
 *   tele/<t>/STATE     device diagnostics JSON, every tele_period_s
 *   stat/<t>/LOW, HIGH, PROBLEM, SFAULT, HOSE, MANUAL, NIGHT   ON / OFF
 *   stat/<t>/STATE     controller state name               (all retained,
 *                      published on every edge, in order)
 *   stat/<t>/PARAMS    all tunables JSON (retained, on change)
 *   cmnd/<t>/<Command> commands (cmnd.h)  ->  stat/<t>/RESULT
 *   <prefix>/<component>/<t>/<object>/config   HA discovery (retained)
 */

#ifndef PUMP_PUMPNET_H_
#define PUMP_PUMPNET_H_

#include <stdbool.h>
#include <stdint.h>

struct pumpnet_stats {
	bool mqtt_up;
	uint32_t mqtt_connects; /* successful connections since boot */
};

/* Start the network thread (after params_init). Never blocks. */
void pumpnet_start(void);

/* Say goodbye to the broker before a reboot (best effort, ~0.5 s). */
void pumpnet_shutdown(void);

void pumpnet_get_stats(struct pumpnet_stats *out);

#endif /* PUMP_PUMPNET_H_ */
