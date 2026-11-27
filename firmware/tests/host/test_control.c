/*
 * Closed-loop host test of control.c against a simulated reservoir.
 *
 * Build and run anywhere with a C compiler:  make -C firmware/tests/host
 *
 * The plant model uses the reservoir volumes from cad/reservoir.py
 * (design.md §4.7), sensor hysteresis, the 2 s firmware debounce, the motor
 * speed ramp and a pump that really moves less than the firmware believes
 * (V_rev error), so the estimator is tested against a biased pumped volume.
 */

#include "control.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DT_MS        100
#define H_MS         (3600LL * 1000)

/* Reservoir, mL above the floor. */
#define V_L_TRIP     77.0
#define V_H_TRIP     (77.0 + 65.0)
#define V_LID        (77.0 + 65.0 + 81.0)
#define V_AIR        5.0   /* below this the pickup draws air */
#define HYST_ML      3.0   /* sensor hysteresis, ± around the trip */
#define DEBOUNCE_MS  2000

#define RAMP_MLH_S       100.0  /* normal ramp: 2 L/h in 20 s */
#define RAMP_FAST_MLH_S  3000.0

struct sensor {
	bool raw, stable;
	int64_t since;
};

struct plant {
	double v_ml;        /* water in the reservoir */
	double q_motor;     /* ramped motor flow, firmware units (mL/h) */
	double pumped_est;  /* firmware's integral of q_motor */
	double efficiency;  /* real / believed displacement */
	bool dead;          /* hose slipped: motor turns, no water moves */
	bool l_stuck_dry;
	struct sensor l, h;
};

struct stats {
	double v_min, v_max;
	bool overflow, air;
	int64_t t_overflow, t_problem, t_emergency;
	int emergencies, problems, measurements;
};

static void sensor_update(struct sensor *s, double v, double trip, int64_t now)
{
	if (!s->raw && v > trip + HYST_ML) {
		s->raw = true;
		s->since = now;
	} else if (s->raw && v < trip - HYST_ML) {
		s->raw = false;
		s->since = now;
	}
	if (s->raw != s->stable && now - s->since >= DEBOUNCE_MS) {
		s->stable = s->raw;
	}
}

typedef double (*inflow_fn)(int64_t t_ms);

static struct ctl_params P;
static const char *verbose_name; /* argv[1]: trace this scenario */

static void run(const char *name, struct plant *pl, struct ctl *c, struct stats *st,
		inflow_fn inflow, int64_t t0, int64_t t1, bool night, bool quiet)
{
	struct ctl_out out = { 0 };

	if (verbose_name && strcmp(verbose_name, name) == 0) {
		quiet = false;
	}

	for (int64_t t = t0; t < t1; t += DT_MS) {
		double q_in = inflow(t);
		double dt_h = DT_MS / 3600000.0;

		/* Motor ramp towards the command. */
		double rate = out.fast_ramp ? RAMP_FAST_MLH_S : RAMP_MLH_S;
		double step = rate * DT_MS / 1000.0;
		double d = out.q_cmd_mlh - pl->q_motor;
		pl->q_motor += (fabs(d) <= step) ? d : copysign(step, d);

		pl->pumped_est += pl->q_motor * dt_h;
		double q_real = pl->dead ? 0.0 : pl->q_motor * pl->efficiency;

		pl->v_ml += (q_in - q_real) * dt_h;
		if (pl->v_ml < 0.0) {
			pl->v_ml = 0.0;
		}
		if (pl->v_ml < V_AIR && q_real > 0.0 && !st->air) {
			st->air = true;
			if (!quiet) {
				printf("  [%s] AIR at t=%.2f h\n", name, t / (double)H_MS);
			}
		}
		if (pl->v_ml > V_LID && !st->overflow) {
			st->overflow = true;
			st->t_overflow = t;
			if (!quiet) {
				printf("  [%s] OVERFLOW at t=%.2f h\n", name, t / (double)H_MS);
			}
		}
		st->v_min = fmin(st->v_min, pl->v_ml);
		st->v_max = fmax(st->v_max, pl->v_ml);

		sensor_update(&pl->l, pl->l_stuck_dry ? 0.0 : pl->v_ml, V_L_TRIP, t);
		sensor_update(&pl->h, pl->v_ml, V_H_TRIP, t);

		struct ctl_in in = {
			.now_ms = t,
			.l_wet = pl->l.stable,
			.h_wet = pl->h.stable,
			.pumped_ml = pl->pumped_est,
			.night = night,
		};
		enum ctl_state before = c->state;

		ctl_step(c, &in, &out);

		if (c->events & CTL_EV_MEASURED) {
			st->measurements++;
		}
		if (c->events & CTL_EV_EMERGENCY) {
			st->emergencies++;
			if (st->t_emergency < 0) {
				st->t_emergency = t;
			}
		}
		if (c->events & CTL_EV_PROBLEM_ON) {
			st->problems++;
			if (st->t_problem < 0) {
				st->t_problem = t;
			}
		}
		if (!quiet && (c->events & (CTL_EV_STATE | CTL_EV_MEASURED | CTL_EV_PROBLEM_ON |
					    CTL_EV_PROBLEM_OFF | CTL_EV_BOOST))) {
			printf("  [%s] t=%7.3f h  %-12s -> %-12s v=%6.1f q_in=%6.0f "
			       "q_cmd=%6.0f q_hat=%6.0f%s%s\n",
			       name, t / (double)H_MS, ctl_state_name(before),
			       ctl_state_name(c->state), pl->v_ml, q_in, out.q_cmd_mlh,
			       c->q_hat_mlh,
			       (c->events & CTL_EV_MEASURED) ? " meas" : "",
			       (c->events & CTL_EV_PROBLEM_ON) ? " PROBLEM" : "");
		}
		c->events = 0;
	}
}

static int failures;

#define CHECK(cond, ...)                                                          \
	do {                                                                      \
		if (!(cond)) {                                                    \
			failures++;                                               \
			printf("  FAIL %s:%d: ", __FILE__, __LINE__);             \
			printf(__VA_ARGS__);                                      \
			printf("\n");                                             \
		}                                                                 \
	} while (0)

static void setup(struct plant *pl, struct ctl *c, struct stats *st, double v0, float qhat0)
{
	memset(pl, 0, sizeof(*pl));
	pl->v_ml = v0;
	pl->efficiency = 0.85;
	memset(st, 0, sizeof(*st));
	st->v_min = 1e9;
	st->t_problem = st->t_emergency = st->t_overflow = -1;
	ctl_init(c, &P, qhat0);
}

/* ---- inflow profiles --------------------------------------------------- */

static double in_1000(int64_t t) { (void)t; return 1000.0; }
static double in_1500(int64_t t) { (void)t; return 1500.0; }

static double in_steps(int64_t t)
{
	double h = t / (double)H_MS;

	if (h < 6) return 500.0;
	if (h < 12) return 1500.0;
	if (h < 14) return 0.0;    /* AC off */
	return 800.0;
}

static double in_wavy(int64_t t)
{
	/* Inverter AC: slow sinusoid 400..1600 mL/h, 3 h period. */
	return 1000.0 + 600.0 * sin(2.0 * M_PI * (t / (double)H_MS) / 3.0);
}

/* ---- scenarios ---------------------------------------------------------- */

static void test_steady(void)
{
	struct plant pl; struct ctl c; struct stats st;

	printf("steady 1.0 L/h, q_hat starts at 0.5 L/h, 24 h\n");
	setup(&pl, &c, &st, 100.0, 500.0f);
	run("steady", &pl, &c, &st, in_1000, 0, 24 * H_MS, false, true);

	/* q̂ is in firmware units; the pump really moves 85 %, so q̂ ≈ q_in/0.85. */
	double expect = 1000.0 / pl.efficiency;
	printf("  q_hat=%.0f (expect ~%.0f) measurements=%d v=[%.1f, %.1f] emerg=%d\n",
	       c.q_hat_mlh, expect, st.measurements, st.v_min, st.v_max, st.emergencies);
	CHECK(!st.overflow && !st.air, "overflow/air");
	CHECK(fabs(c.q_hat_mlh - expect) < 0.10 * expect, "q_hat %.0f not ~%.0f",
	      c.q_hat_mlh, expect);
	CHECK(st.measurements > 20, "only %d measurements", st.measurements);
	CHECK(st.v_min > V_L_TRIP - 25.0, "level fell to %.1f", st.v_min);
	CHECK(st.problems == 0, "problem raised");
}

static void test_steps(void)
{
	struct plant pl; struct ctl c; struct stats st;

	printf("steps 0.5 -> 1.5 -> off -> 0.8 L/h, 20 h\n");
	setup(&pl, &c, &st, 100.0, 800.0f);
	run("steps", &pl, &c, &st, in_steps, 0, 20 * H_MS, false, true);
	printf("  q_hat=%.0f state=%s v=[%.1f, %.1f] emerg=%d problems=%d meas=%d\n",
	       c.q_hat_mlh, ctl_state_name(c.state), st.v_min, st.v_max,
	       st.emergencies, st.problems, st.measurements);
	CHECK(!st.overflow && !st.air, "overflow/air");
	CHECK(st.problems == 0, "problem raised");
	CHECK(fabs(c.q_hat_mlh - 800.0 / pl.efficiency) < 0.15 * 800.0 / pl.efficiency,
	      "q_hat %.0f after the last step", c.q_hat_mlh);
}

static void test_wavy(void)
{
	struct plant pl; struct ctl c; struct stats st;

	printf("inverter AC, 0.4..1.6 L/h sinusoid, 48 h\n");
	setup(&pl, &c, &st, 100.0, 1000.0f);
	run("wavy", &pl, &c, &st, in_wavy, 0, 48 * H_MS, false, true);
	printf("  v=[%.1f, %.1f] emerg=%d problems=%d meas=%d\n", st.v_min, st.v_max,
	       st.emergencies, st.problems, st.measurements);
	CHECK(!st.overflow && !st.air, "overflow/air");
	CHECK(st.problems == 0, "problem raised");
	CHECK(st.emergencies == 0, "%d emergencies on a smooth inflow", st.emergencies);
}

static void test_dead_pump(void)
{
	struct plant pl; struct ctl c; struct stats st;

	printf("dead pump at 1.5 L/h (worst-case design inflow)\n");
	setup(&pl, &c, &st, 100.0, 1500.0f / 0.85f);
	run("dead", &pl, &c, &st, in_1500, 0, 1 * H_MS, false, true);
	int64_t t_dead = 1 * H_MS;

	pl.dead = true;
	run("dead", &pl, &c, &st, in_1500, t_dead, t_dead + H_MS, false, false);
	printf("  problem at +%.0f s, overflow at +%.0f s\n",
	       (st.t_problem - t_dead) / 1000.0, (st.t_overflow - t_dead) / 1000.0);
	CHECK(st.t_problem >= 0, "no problem raised");
	CHECK(st.t_problem >= 0 && (st.t_overflow < 0 || st.t_problem < st.t_overflow),
	      "problem came after the overflow");
}

static void test_sensor_fault(void)
{
	struct plant pl; struct ctl c; struct stats st;

	printf("L sensor stuck dry, 1.0 L/h\n");
	setup(&pl, &c, &st, 100.0, 1000.0f);
	pl.l_stuck_dry = true;
	run("sfault", &pl, &c, &st, in_1000, 0, 4 * H_MS, false, false);
	CHECK(c.sensor_fault, "sensor fault not detected");
	CHECK(!st.overflow, "overflow");
}

static void test_night_cap(void)
{
	struct plant pl; struct ctl c; struct stats st;

	printf("night cap 0.8 L/h with 1.0 L/h inflow (cap below the inflow)\n");
	setup(&pl, &c, &st, 100.0, 1000.0f);
	P.night_cap_mlh = 800.0f;
	run("night", &pl, &c, &st, in_1000, 0, 12 * H_MS, true, true);
	P.night_cap_mlh = 0.0f;
	printf("  v=[%.1f, %.1f] emerg=%d problems=%d\n", st.v_min, st.v_max,
	       st.emergencies, st.problems);
	CHECK(!st.overflow && !st.air, "overflow/air");
	CHECK(st.problems == 0, "problem raised");
	CHECK(st.emergencies <= 1, "%d emergencies: the cap fights the inflow", st.emergencies);
}

int main(int argc, char **argv)
{
	verbose_name = (argc > 1) ? argv[1] : NULL;
	ctl_params_default(&P);

	test_steady();
	test_steps();
	test_wavy();
	test_dead_pump();
	test_sensor_fault();
	test_night_cap();

	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
	       failures == 1 ? "" : "s");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
