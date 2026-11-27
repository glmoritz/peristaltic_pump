/*
 * Pump flow layer: commanded flow (mL/h) -> ramped motor speed, and the
 * pumped volume integrated from the real motor position.
 */

#ifndef PUMP_PUMP_H_
#define PUMP_PUMP_H_

#include <stdbool.h>
#include <stdint.h>

int pump_init(void);

/* New target flow. fast = emergency ramp. */
void pump_set_flow(float q_mlh, bool fast);

/* Advance the ramp and the volume integral. Call every control tick. */
void pump_tick(int64_t now_ms);

float pump_flow_mlh(void);   /* current (ramped) flow */
float pump_target_mlh(void);
float pump_rpm(void);
double pump_pumped_ml(void); /* since boot */
double pump_revs(void);      /* since boot */

#endif /* PUMP_PUMP_H_ */
