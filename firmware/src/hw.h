/*
 * Hardware backends. Implemented by motor_stepper.c + level_gpio.c on the
 * device, and by sim.c on native_sim (CONFIG_PUMP_SIM).
 */

#ifndef PUMP_HW_H_
#define PUMP_HW_H_

#include <stdbool.h>
#include <stdint.h>

int motor_hw_init(void);

/* Microsteps per second, > 0 runs forward; 0 stops and de-energises. */
int motor_hw_set_speed(float usteps_per_s);

/* Cumulative microsteps since boot. */
int64_t motor_hw_position(void);

int level_hw_init(void);

/* Raw (undebounced) sensor states, true = wet. */
void level_hw_read(bool *low_wet, bool *high_wet);

#endif /* PUMP_HW_H_ */
