/* Simulated reservoir + motor for native_sim (CONFIG_PUMP_SIM). */

#ifndef PUMP_SIM_H_
#define PUMP_SIM_H_

#include <stdbool.h>

struct sim_status {
	double volume_ml;
	float inflow_mlh;
	float real_outflow_mlh;
	bool dead;
	bool overflowed;
	bool air;
};

void sim_set_inflow(float mlh);
void sim_set_dead(bool dead);
void sim_set_volume(double ml);
void sim_get_status(struct sim_status *st);

#endif /* PUMP_SIM_H_ */
