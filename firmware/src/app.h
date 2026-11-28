/*
 * Interface between the control loop (main.c) and its observers: the shell,
 * the command processor and the MQTT thread.
 *
 * Isolation rule: the control loop never waits on any of them. It hands out
 * a snapshot under a spinlock (a struct copy), posts events to a queue
 * without waiting (dropped and counted when full), and gives everything that
 * can block (flash writes, reboots) to a low-priority work queue. Requests
 * coming the other way are flags it picks up on its next tick.
 */

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
	float manual_q_mlh;       /* 0 when not manual */
	uint32_t manual_left_s;
	bool night;               /* night cap active */
	bool muted;               /* buzzer silenced by the button */
	bool calibrating;
	float calib_revs;         /* revolutions of the last calibration run */
	uint32_t n_boosts;        /* since boot */
	uint32_t n_emergencies;   /* since boot */
	uint32_t events_dropped;  /* event queue overflows since boot */
};

void app_get_status(struct app_status *st);

/* Discrete changes, in order, for the MQTT thread (edges of the HA
 * binary sensors and the controller state). */
enum app_event_type {
	APP_EV_LOW,     /* value: wet */
	APP_EV_HIGH,
	APP_EV_STATE,   /* value: enum ctl_state */
	APP_EV_PROBLEM,
	APP_EV_SENSOR_FAULT,
	APP_EV_HOSE_WORN,
	APP_EV_MANUAL,
	APP_EV_NIGHT,
	APP_EV_COUNT,
};

struct app_event {
	uint8_t type;
	uint8_t value;
};

/* Next event, without waiting. 0 or -EAGAIN. */
int app_event_get(struct app_event *ev);
/* Forget queued events (before republishing everything from a snapshot). */
void app_events_purge(void);

/* Run at a fixed flow for some seconds (purge, test); the control loop
 * keeps watching and EMERGENCY still overrides it. */
void app_manual(float q_mlh, uint32_t seconds);
void app_auto(void);

/* Calibration (design.md §6.4): turn `revs` revolutions at a fixed flow into
 * a measuring cup, then app_calibrate_ml() with what was collected. */
int app_calibrate(float revs);
int app_calibrate_ml(float ml, float *v_rev_out);

void app_test_alarm(uint32_t seconds);
void app_hose_replaced(void);
void app_save(void);

/* Save the state, then reboot (into the Wi-Fi portal if asked). */
void app_restart(bool portal);

#endif /* PUMP_APP_H_ */
