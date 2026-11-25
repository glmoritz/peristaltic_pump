# Quiet Condensate Peristaltic Pump — Design Document

Status: **draft v0.3** (2026-11-25) — requirements and concept, before CAD.
This file is the single source of truth for the project and will evolve until
the working prototype. Values marked **[TBC]** are assumptions waiting for a
measurement or confirmation; see [Open questions](#11-open-questions).

---

## 1. Problem

- A 12 000 BTU/h LG split AC (Curitiba, Brazil — humid subtropical climate)
  drains condensate to an outlet that is **~40 cm higher** than the AC's drain.
- The current lift pump (Masterflow 20, 20 L/h, up to 45 000 BTU) is too loud
  to sleep with.
- Goal: replace it with a pump that runs **continuously and as slowly as
  possible**, matching the AC's actual condensate production, so it is
  effectively silent.
- The pump is installed **inside the wall recess behind the AC indoor unit**,
  in the same place as the current pump.

## 2. Requirements

| ID | Requirement | Target |
|----|-------------|--------|
| R1 | Lift | ≥ 0.4 m head (design for 1 m) |
| R2 | Nominal flow range (normal control) | 0.1 – 2 L/h |
| R3 | Emergency flow | ≥ 10 L/h (Masterflow does 20 L/h) |
| R4 | Noise | inaudible at night in the bedroom: no on/off cycling, no gurgling, no vibration transmitted into the wall |
| R5 | Maintenance | open with 4× M3 screws; clean algae, swap hose, oil bearings |
| R6 | Fail-safe | detect pump failure / overflow risk and **notify Home Assistant**. A future HA automation turns the LG AC off (via its Wi-Fi) |
| R7 | Power | laptop power supply (no plug: screw terminal or soldered jack) |
| R8 | Envelope | must fit the wall recess behind the AC **[TBC: measure recess]**, reference is the Masterflow 20 footprint (section 4.6) |
| R9 | Use parts already bought | 608-type skateboard bearings (10 pcs), TMC2209, NEMA stepper, ESP32-C3 SuperMini, 2× non-contact level sensors, silicone hose 4 × 7 mm |
| R10 | Firmware | **Zephyr RTOS**, MQTT, Home Assistant MQTT discovery (same pattern as the user's existing projects) |
| R11 | Reservoir | **new, 3D printed** |

## 3. Sizing

### 3.1 Condensate production

12 000 BTU/h = 3.52 kW cooling. In humid conditions the latent (moisture) part
is about 20–30 % of the load, so 0.7–1.05 kW goes into condensing water:

```
m_dot = P_latent / h_fg = 0.7…1.05 kW / 2450 kJ/kg ≈ 0.29…0.43 g/s ≈ 1.0…1.5 L/h
```

| Case | Flow |
|------|------|
| Typical (inverter at part load) | 0.3 – 1.0 L/h |
| Full load, humid day | 1.0 – 1.5 L/h |
| Design maximum for normal control | 2 L/h |
| Emergency | ≥ 10 L/h |

The control loop measures the real value (section 6), so these numbers only set
the speed range. The firmware also logs the real production to HA.

### 3.2 Pump displacement

Silicone hose: ID 4 mm, OD 7 mm, wall 1.5 mm → bore area A = π·2² = 12.57 mm².

Proposed rotor (section 4.2): three 608 bearings (Ø22 mm, **[TBC]**) on a 20 mm
pitch radius → roller outer radius 31 mm. Hose centre line ≈ 31 mm radius.

```
V_rev ≈ A · 2π · r_hose = 12.57 · 2π · 31 ≈ 2.45 mL/rev (theoretical)
V_rev ≈ 2.0 mL/rev (assumed ~80 % efficiency, to be calibrated)
```

| Flow | Rotor speed | Step rate (200 steps/rev, 16 µsteps) |
|------|-------------|------------------------------|
| 0.3 L/h | 2.5 RPM | 133 Hz |
| 1 L/h | 8.3 RPM | 444 Hz |
| 2 L/h | 16.7 RPM | 889 Hz |
| 20 L/h | 167 RPM | 8.9 kHz |

All of these suit **direct drive with no gearbox**. A stepper in TMC2209
StealthChop at 2–20 RPM is essentially silent, and a gearbox would only add
noise.

If the envelope (R8) forces a smaller rotor, displacement scales with
r_hose. For example, r = 20 mm gives ≈ 1.3 mL/rev, which is still fine
(≈ 13 RPM at 1 L/h).

### 3.3 Pressure and torque

- 0.4 m of water = 3.9 kPa, which is negligible. The load is dominated by
  **squeezing the hose**, not by the lift.
- Estimated rotor torque for 4×7 silicone hose at r = 31 mm: **0.1–0.25 N·m
  [estimate, measure on the prototype]**.
- NEMA 17, 40 mm long: ≈ 0.4 N·m holding torque, which is fine. NEMA 17, 34 mm
  long: ≈ 0.28 N·m, which is OK. **NEMA 14, 35 mm long: ≈ 0.12–0.18 N·m, which
  is marginal** and would need a smaller rotor or a 1:3 belt. → Q1.

### 3.4 Power budget

| Load | Typical |
|------|---------|
| Stepper (run current ~0.4–0.6 A RMS, StealthChop, low speed) | 2–5 W |
| ESP32-C3 with Wi-Fi | 0.2–0.5 W |
| 2× level sensors | < 0.1 W |
| **Total** | **< 6 W** (laptop PSU is massively oversized, which is good: cool and silent) |

## 4. Mechanical concept

### 4.1 Layout

```
        AC drain hose (gravity)
                │
                ▼
   ┌─────────────────────────┐      ┌──────────────────────────────┐
   │  RESERVOIR (printed,    │      │  PUMP HEAD                   │
   │  dark/opaque)           │      │   ┌───────────────┐          │
   │  [H sensor] ── high ────┤      │   │  rotor 3×608  │ ← lid,   │
   │                         │      │   │  in U-track   │   4× M3  │
   │  [L sensor] ── low ─────┤      │   └───────────────┘          │
   │                         │      │   NEMA motor behind,         │
   │   ┌─ pickup (strainer)  │─────▶│   on rubber dampers          │──▶ elevated drain (+40 cm)
   └───┴─────────────────────┘ hose └──────────────────────────────┘
                         one continuous 4×7 silicone hose ──────────────────────▶
```

- The **pickup is at the bottom of the reservoir**, behind a coarse strainer
  that stops algae flakes and debris.
- **Decided:** the 4×7 silicone hose is **one continuous piece** from the
  pickup, through the pump track, all the way to the elevated drain. It has no
  joints that can leak inside the wall, and replacing the hose is one job.
- Peristaltic pumps always keep at least one roller occluding the hose, so
  **nothing can siphon back** and no check valve is needed.
- The sensors are **non-contact capacitive** and read *through* the reservoir
  wall. Nothing gets wet, algae cannot foul them, and they mount with 2× M3 on
  the outside.
- Reservoir and pump head can be **one assembly** (pump head bolted to the
  reservoir side) or two separate blocks joined only by the hose. The choice
  depends on the shape of the wall recess (Q10).

### 4.2 Pump head

- **Rotor**: two printed discs that sandwich **3× 608 bearings** (8×22×7
  **[TBC]**) on M8 bolts, or on M4 bolts with printed Ø8 sleeves. Hub on the
  5 mm D shaft with an M3 grub screw and captive nut. Three rollers keep at
  least one roller occluding at all times. We have 10 bearings: 3 rollers +
  1 lid support bearing + 6 spares.
- **Track**: a U-shaped track printed into the base, with about 180° plus
  2× 30° of wrap.
  - Track inner radius = roller outer radius + occlusion gap.
  - **Occlusion gap: start at 2.6 mm** (≈ 87 % of 2× wall thickness = 3.0 mm).
  - Make the gap **adjustable** with a replaceable track insert or shims, since
    under-occlusion means slip and over-occlusion wears the hose fast and needs
    more torque.
- **Hose retention**: clamps or "saddles" at the track entry and exit stop the
  hose from creeping along the track (a classic peristaltic failure).
- **Lid**: holds the hose in the track and carries one 608 as a top bearing for
  the rotor, so the motor shaft is not cantilevered. **Decided: 4× M3 screws
  into heat-set inserts.**
- **Motor mount**: the NEMA face mounts on a **rubber NEMA 17 vibration
  damper** (or TPU grommets) so step vibration does not reach the wall. This
  matters even more because the unit lives *inside the wall*, and the wall
  cavity acts as a sounding board.

### 4.3 Reservoir (new, printed)

- **Dark / opaque PETG**, because light causes algae (the recess is dark
  anyway).
- The lid is removable for cleaning (M3 screws or a snap fit). It has the AC
  drain hose inlet and a vent hole.
- Sensor faces need **flat walls about 2 mm thick**.
  - Mount L and H on **different faces**, or **offset sideways**. Each sensor is
    24 mm tall, so they cannot be stacked closer than that on the same line.
- The sloped floor drains to the pickup, so no stagnant puddles grow algae.
- **Overflow margin is critical.** There is no hardware AC cut-off for now, only
  an HA notification plus a future HA automation, and an overflow would spill
  *inside the wall*. Keep as much volume as possible **above H**. Consider a
  3rd sensor or a cheap float switch as an independent "overflow imminent"
  input (spare GPIO21) → Q12.

### 4.4 Level sensor geometry (from `sensor_dimensions.png`)

| Dimension | Value |
|-----------|-------|
| Body width / width over ears | 13.2 / 21 mm |
| Height | 24.1 mm (22.2 mm to the ear line — **verify with a caliper**) |
| Thickness | 7 mm (5.5 mm body) |
| Mounting | 2× Ø3 holes |
| Sensing face | the flat face against the wall |
| Cable | exits at the bottom |

### 4.5 Volume between sensors

The volume between L and H (V_LH) is the "measuring cup" for the flow
estimate:

- **Too small** and the timing resolution is poor and the sensor hysteresis
  dominates.
- **Too big** and water stagnates longer, and the control reacts slowly.

**Target V_LH ≈ 50 mL.** At a ±20 % speed offset around 1 L/h, the net flow is
0.2 L/h, so each half cycle takes ≈ 15 min. Example reservoir: 70 × 50 mm
inside → 35 cm² → **14 mm** vertical spacing between L and H. Place L at least
**15 mm above the pickup** so the inlet stays submerged and the pump never
draws air. Air in the line causes the gurgling at the drain end.

### 4.6 Reference: current Masterflow 20 (from `pump_dimensions.png`)

The drawing is low resolution. The values below were read from it and **must
be confirmed with a caliper**.

| View | Value read |
|------|-----------|
| Top view: body length (barb to barb) | 162 mm |
| Top view: body width | 28 mm |
| Front view: body height | 29 mm |
| End view: overall width | 87 mm |
| End view: height | 42 mm |
| Side view: overall height | 48.5 mm |
| Hose barbs | Ø7 mm |

The Masterflow is a slim in-line unit, about 162 × 28 × 29 mm for the main
body. A NEMA 17 pump head with a Ø62 mm rotor is roughly **85 × 85 × 90 mm**,
much bulkier. **The recess dimensions (Q10) are the #1 input for the CAD.**

If the space is tight, the options are:

1. Place the reservoir in the recess and the pump head somewhere else, joined
   only by hose.
2. Use a smaller rotor (r ≈ 20 mm) or a NEMA 14 with a belt reduction.
3. Use a pancake NEMA 17 (≈ 20–25 mm long), if the torque is enough.

## 5. Electronics

### 5.1 Block diagram

```
Laptop PSU (19–20 V, bare wires) ─► screw terminal ─► fuse 1 A ─► reverse-polarity
                                                                  protection (P-MOSFET or SS34)
                                                                  + TVS (SMBJ24A)
                     ┌────────────────────────────────────────────────┘
                     ├──► TMC2209 VM (+100 µF/35 V electrolytic at the module)
                     └──► buck 5 V (MP1584 / mini-360) ──► ESP32-C3 SuperMini 5V pin
                                                     └──► level sensors V+ (if 5 V-capable)
ESP32-C3 3V3 ──► TMC2209 VIO
```

- **PSU input**: the PSU has no plug. Use a screw terminal, or optionally solder
  a DC jack later. Because bare wires can be swapped by mistake, the input has
  **reverse-polarity protection, a fuse and a TVS**.
- **PSU voltage [TBC Q3]**: the TMC2209 accepts VM 4.75–29 V, so a 19–20 V
  laptop PSU is fine.
- **TMC2209 mode, in two phases** (see section 6.5 for why):
  - **Phase 1, standalone.** The upstream Zephyr driver is step/dir +
    enable + microstep GPIOs only. Set the current with the module's VREF pot,
    and strap MS1/MS2 for 16 µsteps (256 interpolated). StealthChop is the
    standalone default when SPREAD is low **[TBC on the module, Q11]**.
  - **Phase 2, UART.** Our own small register module adds software current
    control, StealthChop tuning and diagnostics: overtemperature, open load,
    and StallGuard (only works at higher speed). Wire PDN_UART to GPIO6/7 from
    the start so phase 2 needs no rework.
- The electronics are **inside a wall recess next to water**:
  - put the electronics box **above** the reservoir, never below it;
  - route cables with drip loops;
  - conformal coat the boards (optional).

### 5.2 ESP32-C3 SuperMini pin plan (draft)

The pin plan avoids the strapping pins GPIO2, 8 and 9, the onboard LED on
GPIO8, and the USB pins GPIO18/19.

| GPIO | Function |
|------|----------|
| 0 | Level sensor LOW (input, RC filtered) |
| 1 | Level sensor HIGH (input, RC filtered) |
| 3 | TMC2209 STEP |
| 4 | TMC2209 DIR |
| 5 | TMC2209 EN (active low) |
| 6 | TMC2209 UART TX → 1 kΩ → PDN_UART (used from phase 2) |
| 7 | TMC2209 UART RX ← PDN_UART (used from phase 2) |
| 10 | TMC2209 DIAG |
| 20 | Local buzzer (optional, alarm also goes to HA) |
| 21 | Spare: 3rd "overflow" sensor (Q12) or push button (manual purge / test) |

### 5.3 Sensor interface

The interface depends on the output variant of the sensor **[TBC Q2]**:

- **NPN / open collector**: pull up to 3.3 V, then wire directly to the GPIO.
  This is the simplest option.
- **"V" / push-pull (output = supply)**: power at 5 V and use a divider of
  10 k + 20 k to bring the output to 3.3 V logic.
- **PNP**: use a divider to GND.

Add 100 nF + 10 k RC filtering, plus a software debounce of about 2 s, because
ripples on the water surface cause chatter.

## 6. Control strategy

### 6.1 Signals and states

L and H each read *wet* or *dry*.

| L | H | Meaning |
|---|---|---------|
| dry | dry | below low level |
| wet | dry | normal band |
| wet | wet | high level |
| dry | wet | **impossible: sensor fault** → alarm, run at a safe fixed speed |

`q̂_in` is the estimated inflow, a running average saved to flash (Zephyr
settings/NVS). `q_p` is the commanded pump flow, which is RPM × `V_rev`,
calibrated.

### 6.2 Normal loop: "slow saw-tooth" between L and H

The pump **never stops while there is water**. It only switches between two
speeds a little below and a little above the estimated inflow. The level drifts
slowly up and down between the two sensors, and **every crossing produces a new
measurement of the inflow**.

```
FILL   : q_p = k_lo · q̂_in   (k_lo ≈ 0.8)   level rises  L→H in t_up
         ⇒ q_in = V_LH / t_up + q_p
DRAIN  : q_p = k_hi · q̂_in   (k_hi ≈ 1.25)  level falls  H→L in t_down
         ⇒ q_in = q_p − V_LH / t_down
```

- Update `q̂_in` with an exponential moving average after every crossing.
- Change speed with a gentle ramp over 10–30 s, so the motor sound never
  changes abruptly.
- `k_lo` and `k_hi` trade off how often the level cycles against how far the
  speed swings. ±20 % is a starting point, and both are exposed to HA as
  `number` entities for tuning.

### 6.3 Exceptions

| Situation | Detection | Action |
|-----------|-----------|--------|
| Inflow rose (pump too slow) | in DRAIN, H still wet after `T_drain` (≈ 2× expected t_down, minimum 10 min) | raise `q̂_in` ×1.5 and stay in DRAIN; repeat |
| **Emergency** | H wet and not clearing after `T_emerg` (≈ 10 min at the raised speed), or H wet while already at max normal speed | **full speed** until L dry, then go back to normal with `q̂_in` = the measured value; HA event `emergency` |
| **Pump failure** (hose burst or slipped, motor stall, clog) | still H wet after `T_fail` (≈ 5 min) **at full speed** | **ALARM → HA `problem` binary sensor ON** (+ local buzzer). Future: HA automation turns the LG AC off |
| AC switched off / inflow dropped | in FILL, L goes dry, or L stays dry for `T_idle` | **stop the motor** (no water = no reason to run, avoids gurgling); lower `q̂_in`; restart at `k_lo · q̂_in` when L is wet again |
| Sensor fault | L dry + H wet, or a sensor stuck for days while the AC runs | alarm, run at a conservative fixed speed (e.g. 3 L/h) |
| Driver fault | TMC2209 UART reports overtemperature, short or open load | alarm |
| Lost Wi-Fi / MQTT | — | the control loop is **fully local** and never depends on the network; it reconnects in the background |

### 6.4 Extras

- **Night mode**: cap the normal speed (not the emergency speed) during sleep
  hours. The schedule is set from HA.
- **Hose wear counter**: count rotor revolutions × 3 rollers and warn when it is
  time to replace the hose. The interval is learned from the first hose.
- **Calibration routine**: an HA button runs N revolutions into a measuring cup,
  then you enter the mL to correct `V_rev`.
- **Periodic exercise**: if the pump has been stopped for more than 24 h, turn
  it a quarter turn now and then so the hose doesn't take a permanent set under
  a roller.

### 6.5 Firmware: Zephyr + MQTT + Home Assistant discovery

#### Toolchain and workspace

The firmware reuses the user's Zephyr environment: the course container repo
at `~/00_tmp/zephyr/introduction-to-zephyr`, which was surveyed on 2026-11-25.

- **Dev container**: `.devcontainer` builds `Dockerfile.moritz`, which pins
  **Zephyr 4.5.0-dev @ `fbfc5f19`** with SDK 1.0.1. The newest local image,
  `vsc-introduction-to-zephyr-a5b93…` from 2026-05-28, was verified to have:
  - the **`riscv64-zephyr-elf`** toolchain, which the ESP32-C3 needs because it
    is RISC-V, not Xtensa;
  - the in-tree board **`esp32c3_supermini`** (`boards/others/`);
  - the **`adi,tmc2209`** stepper driver under the new split API
    (`stepper.h` + `stepper_ctrl.h`, step/dir in `drivers/stepper/step_dir`);
  - the **MQTT** client library (`subsys/net/lib/mqtt`).

  The older `env-zephyr-espressif:latest` image is Zephyr 4.3.0, so don't use
  it.
- **Workspace convention** (from the repo's `workspace/AGENTS.md`):
  - product apps live in `/workspace/projects/<name>`, each its own git repo;
  - shared manifest `/workspace/west.yml`;
  - the helper `/workspace/scripts/zephyr-project.sh set|build|flash|openocd`;
  - a per-project `.board` file and optional `debug.conf`;
  - app-owned `boards/`, `modules/` and `patches/`.
- `zephyr-project.sh` **hardcodes the Xtensa ESP32-S3 gdb/gcc symlinks** and
  will need to be generalised for RISC-V (C3) gdb and OpenOCD.
- **Where the firmware lives [TBC Q13]**: the container only mounts
  `workspace/`. Recommendation: keep the firmware in this repo under
  `firmware/`, and clone this repo to `workspace/projects/peristaltic_pump`
  (or add a bind mount).

#### Reuse from `hello_eink`

`hello_eink` (`workspace/projects/hello_eink`, repo
`glmoritz/zephyr_eink_waveshare`) is the reference app:

- **`modules/wifi_prov`**: Wi-Fi provisioning by captive portal, with
  credentials in NVS. API: `wifi_prov_init(cb)` / `start()` / `get_state()`.
  Reuse it as-is.
- `net_watchdog` (escalating self-heal for connectivity) and `sys_watchdog`.
  Use the same pattern for MQTT connectivity.
- Settings on NVS, the telnet shell backend (debugging without a USB cable),
  and NTP sync (needed for night mode).
- **Dual target `native_sim`**: the same `src/` builds for Linux. For the
  pump this is ideal: a simulated tank (inflow profile + pump + two
  thresholds) can exercise the whole control state machine on the host before
  any hardware exists.
- **Long-uptime network fixes**: the repo carries patches for socket recvq
  close-race and IPv6 NS netbuf leaks (`sockets_inet_recvq_close_race_leak.patch`,
  `pr_*.md`). The pump runs 24/7 on MQTT, so check whether they are upstream at
  the pinned commit and carry them if not.
- **No MQTT / HA-discovery code exists there.** `hello_eink` talks HTTP to
  LLSS. The MQTT + HA discovery layer is new work, unless the user points to
  another project (Q13).

#### Stepper

The upstream `adi,tmc2209` driver is **GPIO-only**: enable, step, dir, and
m0/m1 microstep pins. It has **no UART register access**. Hence the two-phase
plan in section 5.1:

- **Phase 1**: VREF pot + strapped microsteps.
- **Phase 2**: a small app-owned module that sends TMC2209 UART datagrams
  (single-wire, 8-byte write / 4+8-byte read, CRC8) for IRUN/IHOLD, CHOPCONF,
  PWMCONF, DRV_STATUS, SG_RESULT.

**Step timing**: use the step_dir **counter-based timing source** (ESP32-C3
timer group) rather than work-queue timing. Speeds are < 10 kHz, but jitter at
2–20 RPM is audible.

#### MQTT + Home Assistant discovery

- Discovery publishes retained configs to
  `homeassistant/<component>/<node_id>/<object_id>/config`.
- The availability topic uses LWT (`online` / `offline`).
- State is published as JSON on a single state topic. Commands come from
  `.../set` topics.

Proposed entities:

| Entity | HA type | Notes |
|--------|---------|-------|
| Level low / Level high | binary_sensor (moisture) | |
| Pump state | sensor (enum) | idle / fill / drain / emergency / fault |
| Inflow estimate | sensor, L/h | the AC condensate production |
| Pump flow, pump speed | sensor, L/h and RPM | |
| Water pumped total | sensor, L, `total_increasing` | |
| Problem | binary_sensor (problem) | triggers the future AC shutdown automation |
| Hose revolutions / hose wear % | sensor | button "hose replaced" resets it |
| k_lo, k_hi, V_rev, night cap, timeouts | number | tuning, persisted in NVS |
| Calibrate, purge, test alarm | button | |
| TMC2209 status | sensor (diagnostic) | |

## 7. Noise checklist

1. TMC2209 **StealthChop**, 256 µstep interpolation, and the lowest run current
   that still reliably occludes the hose (tune this).
2. Motor on **rubber dampers**, and the whole unit on soft (TPU) feet. Nothing
   rigid touches the wall, because the recess acts as a resonator.
3. No speed jumps: use ramps, and step timing without jitter.
4. **No air in the line**, because air is what gurgles. The pickup must always
   be submerged, and the pump stops when L is dry.
5. At the drain end, terminate the hose **below the water line** of the drain
   trap or pipe, or let it run down the pipe wall, so it doesn't drip audibly.
6. Smooth rollers: 608 bearings, lightly oiled, and a track surface printed
   without seams where the rollers run.

## 8. Maintenance

- **Pump head**: remove the 4× M3 screws and lift off the lid. Hose, rollers
  and track are exposed, and the rotor pulls off the shaft after loosening the
  grub screw.
- **Reservoir**: remove the lid, then clean or brush. The sensors stay on the
  outside, so there is nothing to disconnect.
- **Access**: the unit sits in the wall behind the AC. How we get to it (remove
  the AC unit? a cover?) decides whether it needs to slide out as one block on
  its hoses and cables → Q10.
- Optional: a periodic dose of a chlorine tablet or vinegar in the AC drain
  pan to reduce algae.
- Spares to keep: 4×7 silicone hose (enough length for the full run to the
  drain), 608 bearings (we have 6 spare).

## 9. Materials and fabrication

- Printer: Bambu Lab (Bambu Studio found on this PC) **[TBC]**.
- Material: **PETG** (or ASA). Avoid PLA, because a warm, humid spot creeps it
  and it would deform the track. Print the reservoir in **black or dark** for
  algae, and **TPU** for the damper and feet if no commercial damper is used.
- Hardware: M3 heat-set inserts, M3 screws, M8 or M4 roller axles, M3 grub
  screw.

## 10. CAD plan (FreeCAD)

- FreeCAD 1.1.1 AppImage at `~/bin/FreeCAD_1.1.1-Linux-x86_64-py311.AppImage`
  runs headless:
  ```
  ~/bin/FreeCAD_1.1.1-Linux-x86_64-py311.AppImage freecadcmd script.py
  ```
- The models will be built from a **parametric Python script** (`cad/*.py`,
  with one `params.py`) that generates the `.FCStd` files plus STEP and STL
  exports. Every dimension in this document becomes a named parameter, so
  changing the hose, motor, bearing or occlusion gap is a one-line edit.
- Parts: `base_track`, `rotor_disc` ×2, `rotor_hub`, `lid`, `hose_clamp` ×2,
  `reservoir`, `reservoir_lid`, `sensor_bracket` (if needed), `electronics_box`.
- Order of work:
  1. Pump head plus a test print of only the track and rotor to tune occlusion
     and torque.
  2. Reservoir.
  3. Electronics enclosure.
- **CAD is blocked on measurements**: motor (Q1), bearings (Q6), recess
  envelope (Q10). The sensor and hose dimensions are already known.

## 11. Open questions

### Decided

| # | Question | Decision (2026-11-25) |
|---|----------|----------------------|
| Q4 | Reservoir | **New, printed** |
| Q5 | Hose | 4×7 silicone hose runs **continuously to the drain** |
| Q7 | Failure action | **Notify Home Assistant**. Later: HA automation turns the LG AC off via its Wi-Fi |
| Q8 | Lid fixing | **4× M3 screws** into heat-set inserts |
| Q9 | Firmware | **Zephyr + MQTT + HA MQTT discovery**, built in the existing `introduction-to-zephyr` dev container (Zephyr 4.5.0-dev @ fbfc5f19) |

### Still open (needs measuring — parts are not at hand yet)

| # | Question | Why it matters |
|---|----------|----------------|
| Q1 | **Motor**: frame size (42 mm square = NEMA 17, 35 mm = NEMA 14), body length, shaft Ø and D-cut, label/model, rated current | Torque margin, rotor size, mount pattern |
| Q2 | **Sensor**: output variant (NPN/PNP/V/RS485), supply range, model number on the label; caliper check of the drawing | Interface circuit, wall thickness |
| Q3 | **Laptop PSU**: label voltage and current | Input protection rating, TMC VM |
| Q6 | **Bearings**: confirm 608 (8×22×7) | Rotor design |
| Q10 | **Wall recess**: W × H × D, where the AC drain hose enters, where the drain outlet is, how it is accessed for maintenance | Overall envelope, #1 CAD input |
| Q11 | **TMC2209 module**: version (BTT v1.3/v2/v3/generic), is UART pre-bridged, how SPREAD and MS1/MS2 are strapped on the board | Phase 1 standalone strapping, phase 2 UART wiring |
| Q12 | Add a 3rd sensor or a float switch as an independent overflow input? | No hardware AC cut-off exists, so an overflow spills inside the wall |
| Q13 | Is there an existing project with **MQTT + HA discovery** code? (`hello_eink` has none; it uses HTTP/LLSS.) And where should the firmware live: `firmware/` in this repo, cloned into `workspace/projects/`, or a separate repo? | Reuse vs new MQTT/HA module; container mount |
| Q14 | AC drain hose: ID/OD where it enters the reservoir lid | Inlet fitting |
| — | Masterflow 20: caliper-confirm the values in section 4.6 | Reference envelope |

## 12. Change log

| Date | Version | Change |
|------|---------|--------|
| 2026-11-25 | 0.1 | First draft: requirements, sizing, concept, control strategy, open questions |
| 2026-11-25 | 0.2 | User decisions recorded (printed reservoir, continuous hose, HA notification, M3 lid, Zephyr/MQTT/HA). Unit mounts in the wall recess behind the AC → envelope and overflow concerns. PSU input protection. Masterflow dimensions read from `pump_dimensions.png`. HA entity list |
| 2026-11-25 | 0.3 | Surveyed the user's Zephyr environment (`introduction-to-zephyr`, `hello_eink`). Verified the C3 toolchain, board and TMC2209 driver in the dev container image. The TMC2209 driver is GPIO-only → two-phase plan (standalone, then own UART module). Reuse list (wifi_prov, watchdogs, native_sim tank simulation, leak patches) |
