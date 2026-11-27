/* Interface between the control loop (main.c) and its users (shell, later MQTT). */

#ifndef PUMP_APP_H_
#define PUMP_APP_H_

#include <stdbool.h>
#include <stdint.h>

#include "control.h"

struct app_status {
	enum ctl_state state;
	bool low_wet, high_wet;   /* debounced */
	bool low_raw, high_raw;
	float q_hat_mlh;          /* inflow estimate */
	float last_meas_mlh;
	uint32_t n_measurements;
	float q_cmd_mlh;          /* what the pump is told */
	float flow_mlh;           /* ramped flow now */
	float rpm;
	double total_ml;          /* lifetime */
	double hose_revs;
	bool problem;             /* pump failure */
	bool sensor_fault;
	bool hose_worn;
	bool manual;
	uint32_t manual_left_s;
};

void app_get_status(struct app_status *st);

/* Run at a fixed flow for some seconds (purge, calibration, test); the
 * control loop keeps watching and EMERGENCY still overrides it. */
void app_manual(float q_mlh, uint32_t seconds);
void app_auto(void);

void app_hose_replaced(void);
void app_save(void);

#endif /* PUMP_APP_H_ */
