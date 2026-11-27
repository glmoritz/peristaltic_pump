/* Shell: `pump ...` on every build, `sim ...` on native_sim. */

#include <stdlib.h>
#include <errno.h>

#include <zephyr/shell/shell.h>

#include "app.h"
#include "params.h"
#ifdef CONFIG_PUMP_SIM
#include "sim.h"
#endif

static const char *wet(bool w)
{
	return w ? "wet" : "dry";
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct app_status st;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	app_get_status(&st);

	shell_print(sh, "state      %s%s", ctl_state_name(st.state),
		    st.manual ? " (MANUAL override)" : "");
	if (st.manual) {
		shell_print(sh, "manual     %u s left", st.manual_left_s);
	}
	shell_print(sh, "level      L %s (raw %s)  H %s (raw %s)", wet(st.low_wet),
		    wet(st.low_raw), wet(st.high_wet), wet(st.high_raw));
	shell_print(sh, "inflow     q_hat %.0f mL/h, last measured %.0f mL/h (%u meas.)",
		    (double)st.q_hat_mlh, (double)st.last_meas_mlh, st.n_measurements);
	shell_print(sh, "pump       cmd %.0f mL/h, now %.0f mL/h, %.2f RPM",
		    (double)st.q_cmd_mlh, (double)st.flow_mlh, (double)st.rpm);
	shell_print(sh, "totals     %.2f L pumped, hose %.0f rev%s", st.total_ml / 1000.0,
		    st.hose_revs, st.hose_worn ? " (REPLACE HOSE)" : "");
	shell_print(sh, "problem    %s%s", st.problem ? "PUMP FAILURE" : "no",
		    st.sensor_fault ? ", SENSOR FAULT" : "");
	return 0;
}

static int cmd_params(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (size_t i = 0; i < params_count(); i++) {
		const struct param_desc *d = params_get(i);

		shell_print(sh, "%-18s %10.3f %-7s [%g .. %g]", d->name,
			    (double)params_value(d), d->unit, (double)d->min, (double)d->max);
	}
	return 0;
}

static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int err = params_set(argv[1], argv[2]);

	if (err == -ENOENT) {
		shell_error(sh, "unknown parameter %s (see `pump params`)", argv[1]);
	} else if (err == -EINVAL) {
		shell_error(sh, "bad value or out of range");
	} else if (err) {
		shell_error(sh, "applied, but saving failed (%d)", err);
	}
	return err;
}

static int cmd_manual(const struct shell *sh, size_t argc, char **argv)
{
	float q = strtof(argv[1], NULL);
	uint32_t s = (argc > 2) ? strtoul(argv[2], NULL, 10) : 600;

	app_manual(q, s);
	shell_print(sh, "manual %.0f mL/h for %u s (`pump auto` to stop)", (double)q, s);
	return 0;
}

static int cmd_auto(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	app_auto();
	shell_print(sh, "automatic control");
	return 0;
}

static int cmd_hose(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	app_hose_replaced();
	shell_print(sh, "hose revolution counter reset");
	return 0;
}

static int cmd_save(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	app_save();
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(pump_cmds,
	SHELL_CMD(status, NULL, "Controller, sensors and pump state", cmd_status),
	SHELL_CMD(params, NULL, "List tunables", cmd_params),
	SHELL_CMD_ARG(set, NULL, "<name> <value>  Set and store a tunable", cmd_set, 3, 0),
	SHELL_CMD_ARG(manual, NULL, "<mL/h> [s]  Fixed flow (purge, test)", cmd_manual, 2, 1),
	SHELL_CMD(auto, NULL, "Back to automatic control", cmd_auto),
	SHELL_CMD(hose_replaced, NULL, "Reset the hose revolution counter", cmd_hose),
	SHELL_CMD(save, NULL, "Store q_hat and counters now", cmd_save),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(pump, &pump_cmds, "Condensate pump", NULL);

#ifdef CONFIG_PUMP_SIM
static int cmd_sim_status(const struct shell *sh, size_t argc, char **argv)
{
	struct sim_status st;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	sim_get_status(&st);
	shell_print(sh, "reservoir  %.1f mL (L trip 77, H trip 142, lid 223)%s%s",
		    st.volume_ml, st.overflowed ? " OVERFLOW" : "", st.air ? " AIR" : "");
	shell_print(sh, "inflow     %.0f mL/h, real outflow %.0f mL/h%s",
		    (double)st.inflow_mlh, (double)st.real_outflow_mlh,
		    st.dead ? " (pump DEAD)" : "");
	return 0;
}

static int cmd_sim_inflow(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	sim_set_inflow(strtof(argv[1], NULL));
	return cmd_sim_status(sh, 0, NULL);
}

static int cmd_sim_dead(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	sim_set_dead(atoi(argv[1]) != 0);
	return cmd_sim_status(sh, 0, NULL);
}

static int cmd_sim_volume(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	sim_set_volume(strtod(argv[1], NULL));
	return cmd_sim_status(sh, 0, NULL);
}

SHELL_STATIC_SUBCMD_SET_CREATE(sim_cmds,
	SHELL_CMD(status, NULL, "Simulated reservoir", cmd_sim_status),
	SHELL_CMD_ARG(inflow, NULL, "<mL/h>  Condensate inflow", cmd_sim_inflow, 2, 0),
	SHELL_CMD_ARG(dead, NULL, "<0|1>  Hose slipped: motor turns, no water", cmd_sim_dead,
		      2, 0),
	SHELL_CMD_ARG(volume, NULL, "<mL>  Set the water volume", cmd_sim_volume, 2, 0),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(sim, &sim_cmds, "Simulated reservoir (native_sim)", NULL);
#endif
