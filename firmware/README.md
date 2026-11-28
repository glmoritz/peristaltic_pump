# Pump firmware

Zephyr v4.5.0 application for the ESP32-C3 SuperMini on the pump perfboard
([../docs/electronics.md](../docs/electronics.md)). The control strategy is
[design.md §6](../docs/design.md#6-control-strategy), and the network side is
[design.md §6.5](../docs/design.md#mqtt--home-assistant-discovery-m2-2026-11-28).

- M1: local control loop, simulator, shell.
- M2 (this): Wi-Fi provisioning, MQTT + Home Assistant discovery,
  Tasmota-style commands, NTP night mode, calibration.
- Next: the TMC2209 UART module (M3).

## Layout

| File | Role |
|------|------|
| `src/control.[ch]` | Level controller: FILL/DRAIN saw-tooth, inflow estimate q̂, escalation, alarms. Plain C, no Zephyr |
| `src/pump.[ch]` | Flow (mL/h) → ramped microstep rate; pumped volume from the real motor position |
| `src/level.[ch]` | 2 s debounce of the two sensors |
| `src/hw.h` | Hardware backends: `motor_stepper.c` + `level_gpio.c` on the device, `sim.c` on native_sim |
| `src/params.[ch]` | Tunables, q̂ and counters in settings/NVS |
| `src/main.c` | 100 ms control loop, manual/calibration, buzzer and button, watchdog, snapshot + event queue for observers |
| `src/app.h` | The only interface between the control loop and everything else |
| `src/pumpnet.[ch]` | Network thread: Wi-Fi, NTP, MQTT session, HA discovery, telemetry |
| `src/cmnd.[ch]` | Tasmota-style command table, shared by MQTT and the shell |
| `src/report.[ch]` | JSON reports (SENSOR, STATE, PARAMS) and a small JSON writer |
| `src/netcfg.[ch]` | MQTT / topic / NTP settings |
| `src/timesync.[ch]` | SNTP, local time, night window |
| `src/app_shell.c` | `pump …`, `cmnd …` and `sim …` shell commands |
| `modules/wifi_prov` | Station + captive portal, adapted from hello_eink (extra fields, timeout) |
| `modules/wifi_prov_sim` | Its native_sim stub (the host's network) |
| `boards/esp32c3_supermini.*` | Pins (design.md §5.2), TMC2209 + step/dir on timer0, Wi-Fi, telnet |
| `tests/host/` | Closed-loop test of `control.c` against a simulated reservoir (gcc only) |

### Threads

| Thread | Priority | Can block on |
|--------|----------|--------------|
| `main`: control loop | 0 | nothing: spinlock copies, queue puts without waiting |
| `net`: Wi-Fi, NTP, MQTT | 10 | sockets, DNS |
| `store` work queue | 13 | flash (q̂, counters), reboot |
| shell (serial, telnet) | lowest | anything |

A broker or Wi-Fi outage stalls `net` and nothing else. Level edges queued
while MQTT is down are dropped (and counted); the full state is republished
on reconnect. The task watchdog resets the chip if the loop stalls for 3 s.

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

If the build stops at *"Blob … missing. Update with: west blobs fetch
hal_espressif"*, the image was built with a blob download dropped (pcut's
network). Rebuild the image (the Dockerfile now fails on that), or fetch the
blob once in the container.

Host test of the controller (no Zephyr needed):

```sh
make -C tests/host            # all scenarios
tests/host/test_control wavy  # trace one scenario
```

## First setup of the pump

1. Power it up. With no Wi-Fi stored it opens an open AP **`Condpump-XXXXXX`**.
   Join it; the setup page opens by itself, or go to http://192.168.4.1.
2. Enter the Wi-Fi network, and the MQTT broker (host or IP, e.g. Home
   Assistant's Mosquitto add-on), port, user, password, and optionally the
   topic. Save.
3. The pump appears in Home Assistant as **Condensate pump**
   (Settings → Devices → MQTT).

To change things later:
- reopen the portal by holding the button 5 s, or with `WifiConfig 2`;
- or use the MQTT commands or the shell (USB, or `telnet <ip>`).

If the stored network is unreachable at boot (the router is still starting
after a power cut), the portal opens for 5 minutes, then the pump retries the
network, and so on forever.

## MQTT

Tasmota layout; `<t>` is the topic, by default `condpump_<last 3 MAC bytes>`.

| Topic | Content |
|-------|---------|
| `tele/<t>/LWT` | `Online` / `Offline` (retained, last will) |
| `tele/<t>/SENSOR` | pump telemetry JSON. Sent on change (at most 1/s), and every `TelePeriod` s |
| `tele/<t>/STATE` | uptime, RSSI, IP, version, time, every `TelePeriod` s |
| `stat/<t>/LOW` `HIGH` `PROBLEM` `SFAULT` `HOSE` `MANUAL` `NIGHT` | `ON` / `OFF`, retained, every edge, in order |
| `stat/<t>/STATE` | `idle` / `fill` / `drain` / `emergency` / `sensor_fault` |
| `stat/<t>/PARAMS` | all tunables, retained |
| `cmnd/<t>/<Command>` | a command; the answer goes to `stat/<t>/RESULT` |

### Commands

Same set over MQTT and in the shell, case-insensitive; no payload = query:

```
mosquitto_pub -t cmnd/condpump_a1b2c3/k_lo -m 0.85
uart:~$ cmnd k_lo 0.85
{"k_lo":0.850}
```

| Command | Payload | |
|---------|---------|---|
| `Status` | | everything (SENSOR + STATE) |
| `Params` | | all tunables |
| *any tunable* | value | e.g. `k_lo 0.85`, `night_cap_mlh 500`, `quiet on` (`pump params` lists them) |
| `TelePeriod` / `Timezone` | s / h | aliases of `tele_period_s` / `tz_h` |
| `Manual` | `<mL/h> [s]` | fixed flow, default 600 s (an emergency still overrides it) |
| `Auto` | | back to automatic (also aborts a calibration) |
| `Purge` | `[s]` | 3 L/h for 60 s |
| `Calibrate` | `[rev]` | turn 20 revolutions into a measuring cup, then stop |
| `CalibrateMl` | `<mL>` | sets `v_rev_ml` from what the cup collected |
| `HoseReplaced` | | reset the hose revolution counter |
| `TestAlarm` | `[s]` | beep |
| `Save` | | store q̂ and the counters now |
| `MqttHost` `MqttPort` `MqttUser` `MqttPassword` `Topic` | value | MQTT reconnects by itself |
| `Discovery` | `0`/`1` | HA discovery on / off (off removes the entities) |
| `DiscoveryPrefix` | | default `homeassistant` |
| `NtpServer` | host | default `pool.ntp.org` |
| `SSId` / `Password` | value | Wi-Fi, applied at the next restart; `Password -` = open network |
| `WifiConfig` | `2` | reboot into the setup portal |
| `Restart` | `1` | reboot |
| `Time` | | local time |
| `Backlog` | `cmd pl; cmd pl; …` | several at once, e.g. `Backlog SSId home; Password x; Restart 1` |

### Home Assistant

60 entities on one device:

- **Graphs**: pump flow, commanded flow, rotor speed, condensate inflow (q̂),
  last measured inflow. All have state class `measurement`, so HA keeps
  long-term statistics too.
- **Level low / Level high**: moisture binary sensors fed by the edge topics,
  so the history bar shows every wet/dry transition.
- **Water pumped** (L, `total_increasing`): can feed HA's water dashboard.
- **Alarms**: problem, sensor fault, hose worn. *Problem* is the trigger for
  the AC shutdown automation.
- **State and modes**: controller state, manual override, night cap active,
  hose revolutions and wear.
- **Buttons**: purge, calibrate, hose replaced, automatic mode, test alarm,
  restart.
- **Configuration**: every tunable (as numbers), and switches for night
  schedule and quiet now.
- **Diagnostics**: RSSI (with the STATE JSON as attributes), uptime, IP,
  boosts, emergencies, and raw sensor inputs (disabled by default).

Night mode: set `night_cap_mlh`, then either turn on *Night schedule* (with
*Night start* / *Night end* local hours, `tz_h` = −3) or toggle *Quiet now*
from an HA automation. The emergency speed is never capped.

### Security

There is no authentication beyond the broker's own: anyone on the LAN can use
the telnet shell and the `cmnd` topics, just as with Tasmota. Give the broker a
user and password, and keep the pump off untrusted networks. MQTT is plain TCP.

## Simulator

```sh
./build-native_sim/zephyr/zephyr.exe -uart_stdinout --flash=sim-flash.bin
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

The network is the host's (offloaded sockets), so MQTT works against any
reachable broker: `cmnd MqttHost <broker>`. How M2 was tested on pcut:

- `eclipse-mosquitto:2` and `home-assistant:stable` ran on a private docker
  network, with the MQTT config entry pre-seeded in `.storage`;
- HA registered all 60 entities;
- its recorder had the level edges, the state transitions and the flow ramps.

## Before the first run on the pump

- **Sensor polarity [TBC Q2]**: the overlay assumes NPN normally-open
  sensors (wet = low, `GPIO_ACTIVE_LOW`). Check `pump status` with a sensor in
  and out of water; flip the flag in `boards/esp32c3_supermini.overlay` if it
  reads backwards.
- **Direction**: if the rotor turns the wrong way, add `invert-direction;`
  to `pump-motion` in the overlay (or swap one coil pair).
- **V_rev**: `Calibrate 20` (HA button) into a measuring cup, then
  `CalibrateMl <mL>` (HA number *Calibration volume*).
- **Current**: set the TMC2209 VREF pot first (electronics.md §7).
- **Not yet run on the C3**: Wi-Fi, the portal and the hardware watchdog are
  built but untested on the chip. The DRAM use is 64 % with Wi-Fi.
