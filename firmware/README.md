# Pump firmware

Zephyr v4.5.0 application for the ESP32-C3 SuperMini on the pump perfboard
([../docs/electronics.md](../docs/electronics.md)). The control strategy is
[design.md §6](../docs/design.md#6-control-strategy).

Milestone 1 (this): local control loop, simulator, shell. Next: Wi-Fi
provisioning + MQTT / Home Assistant discovery (M2), TMC2209 UART module (M3).

## Layout

| File | Role |
|------|------|
| `src/control.[ch]` | Level controller: FILL/DRAIN saw-tooth, inflow estimate q̂, escalation, alarms. Plain C, no Zephyr |
| `src/pump.[ch]` | Flow (mL/h) → ramped microstep rate; pumped volume from the real motor position |
| `src/level.[ch]` | 2 s debounce of the two sensors |
| `src/hw.h` | Hardware backends: `motor_stepper.c` + `level_gpio.c` on the device, `sim.c` on native_sim |
| `src/params.[ch]` | Tunables, q̂ and counters in settings/NVS |
| `src/main.c` | 100 ms control loop, persistence, manual override, buzzer |
| `src/app_shell.c` | `pump …` and `sim …` shell commands |
| `boards/esp32c3_supermini.*` | Pins (design.md §5.2), TMC2209 + step/dir controller on timer0 |
| `tests/host/` | Closed-loop test of `control.c` against a simulated reservoir (gcc only) |

## Build

In the dev container (`zephyr_container`, image with Zephyr v4.5.0):

```sh
cd /workspace/projects/peristaltic_pump/firmware
west build -b esp32c3_supermini -d build-esp32c3_supermini .   # the pump
west build -b native_sim        -d build-native_sim .          # simulator
west flash -d build-esp32c3_supermini
```

or with the workspace helper:
`zephyr-project.sh set /workspace/projects/peristaltic_pump/firmware`.

Host test of the controller (no Zephyr needed):

```sh
make -C tests/host            # all scenarios
tests/host/test_control wavy  # trace one scenario
```

## Simulator

```sh
./build-native_sim/zephyr/zephyr.exe -uart_stdinout
```

The reservoir has the CAD volumes (77 mL below L, 65 mL L→H, 81 mL above H)
and the simulated pump moves 85 % of what the firmware believes, as an
uncalibrated V_rev would. Try:

```
pump status
sim inflow 1500        # condensate inflow, mL/h
sim volume 150         # put the level above H
sim dead 1             # hose slipped: motor turns, no water moves
pump params
pump set k_lo 0.85
pump manual 3000 60    # 3 L/h for 60 s, then back to automatic
```

## Before the first run on the pump

- **Sensor polarity [TBC Q2]**: the overlay assumes NPN normally-open
  sensors (wet = low, `GPIO_ACTIVE_LOW`). Check `pump status` with a sensor in
  and out of water; flip the flag in `boards/esp32c3_supermini.overlay` if it
  reads backwards.
- **Direction**: if the rotor turns the wrong way, add `invert-direction;`
  to `pump-motion` in the overlay (or swap one coil pair).
- **V_rev**: run `pump manual <q> <s>` into a measuring cup and set
  `pump set v_rev_ml <measured mL / revolutions>` (`pump status` shows the
  hose revolutions).
- **Current**: set the TMC2209 VREF pot first (electronics.md §7).
