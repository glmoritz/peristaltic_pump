# Quiet Condensate Peristaltic Pump — Design Document

Status: **draft v0.10** (2026-11-27) — requirements, concept, CAD v1 (`cad/`), electronics ([electronics.md](electronics.md)) and assembly instructions ([assembly/](assembly/README.md)).
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
| R7 | Power | laptop power supply, **19 V 2.1 A**, 90 × 37 × 26 mm brick (no plug: screw terminal or soldered jack) |
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

**Rotor:** three 608 bearings (Ø22 × 7 mm, bore 8 mm, **confirmed**) on a
**16 mm pitch radius** → roller outer radius 27 mm. Hose centre line ≈ 27 mm.

Why 16 mm: it is reduced from the 20 mm of v0.3 to cut the torque demand for
the small motor (section 3.3). It is the practical minimum:

- roller-to-roller chord = 16·√3 = 27.7 mm against Ø22 rollers, which leaves
  5.7 mm clearance;
- the M8 axle sits 9.5 mm from the Ø5 shaft.

```
V_rev ≈ A · 2π · r_hose = 12.57 · 2π · 28.3 ≈ 2.2 mL/rev (theoretical, r at mid-gap)
V_rev ≈ 1.8 mL/rev (assumed ~80 % efficiency, to be calibrated)
```

| Flow | Rotor speed | Step rate (200 steps/rev, 16 µsteps) |
|------|-------------|------------------------------|
| 0.3 L/h | 2.9 RPM | 157 Hz |
| 1 L/h | 9.8 RPM | 523 Hz |
| 2 L/h | 19.6 RPM | 1.05 kHz |
| 20 L/h | 196 RPM | 10.5 kHz |

All of these suit **direct drive with no gearbox**. A stepper in TMC2209
StealthChop at 3–20 RPM is essentially silent, and a gearbox would only add
noise. If the TMC2209 is left at its pin-default of **8 µsteps** (MS1/MS2
low), the step rates halve. The chip interpolates to 256 µsteps either way.

### 3.3 Pressure and torque

- 0.4 m of water = 3.9 kPa, which is negligible. The load is dominated by
  **squeezing the hose**, not by the lift.
- Estimated rotor torque for 4×7 silicone hose:
  - r = 31 mm: 0.1–0.25 N·m;
  - **r = 27 mm: ≈ 0.09–0.22 N·m**.

  **This is a rough estimate. Measure it on the track test print.** Torque
  scales roughly with r_hose and very strongly with the occlusion gap.
- **Motor (measured 2026-11-25): 38 × 38 mm frame, 33 mm body.** This is
  neither a NEMA 14 (35.2 mm) nor a NEMA 17 (42.3 mm). It is most likely a
  **39 mm-class ("NEMA 16", e.g. 39BYG) motor**, typically
  **≈ 0.15–0.25 N·m holding torque** at this length.
  - **Mounting holes**: 43.5 mm on the diagonal → **31 mm square pattern**.
    That is the *same pattern as a NEMA 17*.
  - **Output**: a smooth cylindrical hub, **Ø16.8 × 11 mm**, with a radial set
    screw, is fitted on the shaft (section 4.2).
  - **No rating label.** To estimate the current:
    - measure the phase resistance with a multimeter, across the two wires of
      one coil;
    - as a rule of thumb for small motors: R ≈ 1–4 Ω → ~0.8–1.2 A class;
      R ≈ 10–30 Ω → ~0.3–0.5 A class;
    - then tune on the bench: start at **≈ 0.4 A RMS** and raise it until the
      rotor never skips at full speed. Keep the motor case **≤ 50 °C** after
      1 h, since it sits in a closed wall cavity.
- **User decision (2026-11-25):** if this motor proves too weak, buy a
  **NEMA 17**. The design keeps that swap cheap:
  - the same 31 mm hole pattern;
  - a pump base with a motor pocket sized for 42.3 mm;
  - an interchangeable rotor hub insert (section 4.2).
- **Conclusion: the margin is thin.** Mitigations, in order:
  1. Tune the occlusion gap: start loose (2.8 mm) and tighten only until it
     stops slipping against 0.4 m head. The lift is tiny, so full occlusion is
     not needed.
  2. Run current up to the motor rating. StealthChop torque at < 20 RPM is
     close to holding torque.
  3. Use the smaller rotor (already done: r = 27).
  4. Last resort: a 1:2–1:3 GT2 belt reduction. This costs some noise, but
     belts are quiet.
  5. A NEMA 17 motor (≈ R$ 40–60) if all else fails.
- **Torque test** on the first print (track + rotor only):
  - put a lever arm (e.g. 100 mm) on the rotor hub;
  - push it with a kitchen or luggage scale while a hose full of water sits in
    the track;
  - T = F · L, and peak, not average, is what matters;
  - also find the minimum motor current that never skips at 196 RPM.

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

- **Rotor** (as modelled in `cad/pump.py`):
  - two printed discs, Ø57, whose rims act as flanges that keep the hose
    centred;
  - **3 rollers of 2 stacked 608 bearings each (14 mm wide)**, because a single
    608 is 7 mm wide and a flattened 4×7 hose is ~9–10 mm wide. This uses
    **6 of the 10 bearings**;
  - rollers at a **16 mm pitch radius**, each riding on a **printed Ø8 sleeve
    over an M4 countersunk bolt**. The bolt goes through the upper disc into an
    M4 heat-set insert in the lower disc;
  - a sleeve flange at the bottom and a boss under the upper disc clamp only
    the bearings' inner races;
  - three rollers keep at least one roller occluding at all times.
- **Rotor-to-motor interface**: the hub is part of the **lower rotor disc**,
  with two variants generated from the same script. Changing the motor means
  reprinting only this one part (~20 g).
  - **`rotor_lower_stock`**: a Ø17.1 bore that slides over the existing
    Ø16.8 × 11 mm hub on the motor shaft. It is held by **2 radial M3 grub
    screws** with captive nuts, so nothing has to come off the motor.
  - **`rotor_lower_nema17`**: a Ø5 D-shaft bore with 1 grub screw on the flat.
  - The grub screws are reached through a Ø4 access hole in the -y wall: turn
    the rotor until a screw faces it.
- **Track**: a U-shaped track printed into the base, with about 180° plus
  2× 30° of wrap.
  - Track inner radius = roller outer radius + occlusion gap.
  - **Occlusion gap: 2.6 mm nominal** (≈ 87 % of 2× wall thickness = 3.0 mm).
    Because of the motor's torque margin (section 3.3), print test inserts at
    2.4 / 2.6 / 2.8 mm and pick the loosest one that doesn't slip.
  - Make the gap **adjustable** with a replaceable track insert or shims, since
    under-occlusion means slip and over-occlusion wears the hose fast and needs
    more torque.
- **Hose retention**: clamps or "saddles" at the track entry and exit stop the
  hose from creeping along the track (a classic peristaltic failure).
- **Lid**: holds the hose in the track and carries one 608 as a top bearing for
  the rotor, so the motor shaft is not cantilevered. **Decided: 4× M3 screws
  into heat-set inserts.**
- **Motor mount**:
  - **31 mm square hole pattern**, M3 **[TBC thread]**, which fits both the
    current motor and a NEMA 17;
  - a centring pocket of **Ø23 × 2.5 mm**, which is generous: it clears the
    usual Ø22 pilot boss without needing its exact size;
  - the motor body pocket is sized for 42.3 mm.

  The NEMA face mounts on a **rubber NEMA 17 vibration damper** (or TPU
  grommets) so step vibration does not reach the wall. This
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
- The floor is flat with no sump; a sump would cost overflow height. The pickup is a strainer cage in the corner nearest the pump (section 4.7).
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

**Interpretation (confirmed by the user, 2026-11-25).** The drawing shows two
parts:

- the slim in-line **pump body**: 162 × 28 × 29 mm, with Ø7 mm barbs;
- the **square reservoir**: about **87 mm wide × 42 mm tall (48.5 mm including
  the inlet spigot)**. The user's installed reservoir looks like this one.

The drawing is **not to scale** (horizontal and vertical pixel scales differ),
so the depth is not given. Using the horizontal scale of the 87 mm dimension,
the depth comes out at **≈ 75–80 mm**, which matches "square" in plan.

**Decision:** use this square reservoir as the **envelope baseline for our
printed reservoir**, about 87 × 78 × 42–48 mm, until the real recess can be
measured. The user is not opening the wall for now (Q10 deferred).

What this means for the design:

- **Interior footprint** with 2 mm walls: about 83 × 74 mm = **61 cm²**.
  - V_LH = 50 mL → **8 mm** between L and H.
  - Alternatively, keep 14 mm spacing → V_LH ≈ 85 mL. Both are OK for the
    flow estimate.
- **Height budget is tight** (42 mm):
  - floor and pickup: 0–5 mm;
  - L trip point: ≥ 15 mm;
  - H trip point: ~23–29 mm;
  - only **~10–15 mm of headroom above H**, which is ≈ 60–90 mL. With the pump
    dead at 1.5 L/h inflow, that is **≈ 2.5–3.5 min to overflow**. So the
    failure alarm must be fast (shorten `T_fail` accordingly). If the recess
    allows, make the printed reservoir **taller** than the Masterflow one,
    since height is the cheapest overflow margin.
- **Sensor placement**: each sensor is 24 mm tall, so L and H go on
  **different faces**, with their trip lines offset vertically.
- **Pump head**: with the 38 mm motor and a Ø54 mm rotor (track outer
  Ø ≈ 70 mm), the pump head is roughly **70 × 70 × 65 mm**. That is still much
  bulkier than the slim Masterflow pump body (162 × 28 × 29 mm). Until the recess is measured, design the pump head as a **separate
  block joined only by the hose**. It can then go wherever there is room: in
  the recess next to the reservoir, or outside behind the AC cover.

If the space is tight, the options are:

1. Place the reservoir in the recess and the pump head somewhere else, joined
   only by hose.
2. Use a smaller rotor (r ≈ 20 mm) or a NEMA 14 with a belt reduction.
3. Use a pancake NEMA 17 (≈ 20–25 mm long), if the torque is enough.

### 4.7 Reservoir as modelled (v1, `cad/reservoir.py`)

- **Box**: 87 × 78 × 40 mm with 2 mm walls and a 2.4 mm floor. The lid is a
  flat 3 mm plate with no lip, so it prints flat with its collars up. It is
  located and held by 4× M3 into heat-set inserts in the inside corner
  columns.
- **Outside size**: sensor bosses and clips add ~9.5 mm on the -y and +x
  faces, so the overall size is about 94 × 85 × 53 mm including the lid
  collars.
- **Pickup**: the silicone hose comes **down through a grip collar in the
  lid** (Ø6.8 on a 7 mm hose). It plugs into a **printed strainer cage**
  (Ø16 × 14, 6 slots of 1.5 mm, open at floor level) that sits in a locator
  ring in the (+x, -y) corner, nearest the pump.
  - There is **no fitting below the water line, so there is no leak path**.
- **AC inlet**: a Ø20 hole with an 8 mm collar in the lid. The AC drain hose
  just drops in (Q14).
- **Vent**: Ø3 in the lid.
- **Sensors**:
  - **L on the -y wall, H on the +x wall**, each pressed flat against the
    2 mm wall by a **printed clip**;
  - each clip has 2× M3 screws into heat-set inserts in **blind** bosses, so
    the wall is not pierced;
  - the clips have vertical slots for **±5 mm of trip-height adjustment**;
  - the trip lines are at **13 mm (L)** and **24 mm (H)** above the inside
    floor.
- **Volumes**, computed from the model:

| Zone | Volume | Meaning |
|------|--------|---------|
| floor → L | 77 mL | always wet. It is renewed continuously by the inflow, but it is the algae zone, so clean it periodically |
| L → H | **65 mL** | the measuring cup: a half cycle takes 13 min at 1.5 L/h, 20 min at 1 L/h, 39 min at 0.5 L/h (±20 % offset) |
| H → lid | **81 mL** | **overflow margin ≈ 3.2 min at 1.5 L/h with a dead pump** → `T_fail` must be short, and a taller box helps if the recess allows |

## 5. Electronics

> **Build details:** the perfboard layout, connections, BOM, housing and bring-up are
> in **[electronics.md](electronics.md)**. The perfboard uses through-hole parts:
> a **P6KE24A** TVS instead of the SMBJ22A, a **PTC 1.1 A** resettable fuse,
> a **1N5822** Schottky for reverse polarity, and an R2 10 k pull-up on EN.

### 5.1 Block diagram

```
Laptop PSU (19 V 2.1 A, bare wires) ─► screw terminal ─► fuse 1 A ─► reverse-polarity
                                                                  protection (P-MOSFET or SS34)
                                                                  + TVS (SMBJ22A)
                     ┌────────────────────────────────────────────────┘
                     ├──► TMC2209 VM (+100 µF/35 V electrolytic at the module)
                     └──► buck 5 V (MP1584) ──► ESP32-C3 SuperMini 5V pin
                                                     └──► level sensors V+ (if 5 V-capable)
ESP32-C3 3V3 ──► TMC2209 VIO
```

- **PSU input**: the PSU has no plug. Use a screw terminal, or optionally solder
  a DC jack later. Because bare wires can be swapped by mistake, the input has
  **reverse-polarity protection, a fuse and a TVS**.
- **PSU (measured 2026-11-25): 19 V, 2.1 A (40 W), brick 90 × 37 × 26 mm.**
  - 19 V is well inside the TMC2209 VM range (4.75–29 V).
  - The load is < 6 W, so the brick runs at about 15 % load: cool and silent.
  - **Buck**: use an **MP1584** (28 V max input). Avoid the mini-360: its
    23 V max input leaves too little margin above 19 V.
  - **TVS**: **SMBJ22A** (22 V standoff, 24.4 V breakdown). It sits above
    19 V and below the TMC2209's 29 V absolute maximum for normal transients.
  - **Fuse**: 1 A, slow-blow.
  - **Brick placement**: it needs a spot in or near the recess. Its 90 × 37 ×
    26 mm and its mains cord are part of the envelope (Q10). Keep it apart
    from the reservoir, and **above** any possible water.
- **TMC2209 mode, in two phases** (see section 6.5 for why):
  - **Phase 1, standalone.** The upstream Zephyr driver is step/dir +
    enable + microstep GPIOs only.
    - Set the current with the module's VREF **micropot**. On a typical
      0.11 Ω-sense module, I_RMS ≈ 0.71 × VREF, measured between the pot wiper
      and GND. Set it to ≈ 70–90 % of the motor's rated current.
    - MS1/MS2 have internal pull-downs, so if left open → **8 µsteps** (256
      interpolated), which is fine. Strap MS2 high, MS1 high for 16.
    - StealthChop is the standalone default when SPREAD is low **[TBC on the
      module, Q11]**.
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
| 10 | TMC2209 DIAG, **only if the module exposes it**. The 16-pin StepStick header doesn't carry DIAG; some modules have it on an extra top pin or pad |
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

**Decision (2026-11-27):** the container is upgraded to the **Zephyr v4.5.0
release** (`41d10b59`, 2026-10-28; SDK 1.0.1 unchanged), before any firmware
is written. The previous pin was 4.5.0-dev @ `fbfc5f19`. The image is built on
the lab workstation (pcut) as `zephyr-moritz:4.5.0`, because the home machine's
disk is full.

- **Dev container**: `.devcontainer` builds `Dockerfile.moritz`. The v4.5.0
  tree was checked on 2026-11-27 and has:
  - the in-tree board **`esp32c3_supermini`** (`boards/others/`);
  - the **`adi,tmc2209`** driver (`drivers/stepper/adi_tmc/tmc22xx`), still
    **GPIO-only, no UART** → the two-phase plan below stands. It uses the
    split stepper API (`stepper.h` + `stepper_ctrl.h`);
  - the **MQTT** client library (`subsys/net/lib/mqtt`);
  - the **`riscv64-zephyr-elf`** toolchain comes from SDK 1.0.1, as before.
- **Effect on `hello_eink`** (it shares the image): 4 of its 6 Zephyr patches
  still apply. The other 2 are obsolete and were removed:
  `ipv6_nbr_ns_pending_leak` is fixed upstream (v4.5.0 has the same
  `net_pkt_unref`), and `tls_session_export` had already been replaced by the
  upstream `CONFIG_NET_SOCKETS_TLS_SESSION_CACHE_PERSISTENT`.
- **Workspace convention** (from the repo's `workspace/AGENTS.md`):
  - product apps live in `/workspace/projects/<name>`, each its own git repo;
  - shared manifest `/workspace/west.yml`;
  - the helper `/workspace/scripts/zephyr-project.sh set|build|flash|openocd`;
  - a per-project `.board` file and optional `debug.conf`;
  - app-owned `boards/`, `modules/` and `patches/`.
- On `main`, `zephyr-project.sh` **hardcodes the Xtensa ESP32-S3 gdb/gcc
  symlinks**. The `esp32c6-support` branch of `zephyr_container` (checked out
  on pcut) already generalises it for RISC-V (ESP32-C6). Merge that branch
  into `main` and reuse it for the C3.
- **Where the firmware lives (decided 2026-11-27, Q13):** in **this repo**,
  under `firmware/`. The repo is the inner project at
  `zephyr_container/workspace/projects/peristaltic_pump`, gitignored by the
  outer repo the same way as `hello_eink`.
  - On the home machine the repo was moved there, and
    `~/00_tmp/peristaltic_pump` is a symlink to it. The container only mounts
    `workspace/`, so a symlink in the other direction would not work.
  - On pcut it is a clone in `~/00_tmp/zephyr_container/workspace/projects/`.

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
- **Long-uptime network fixes**: the pump runs 24/7 on MQTT, so it carries
  `hello_eink`'s still-needed Zephyr patches. On v4.5.0 these are the
  sockets recvq close-race leak, the Ethernet mcast NULL guard, and the telnet
  stale-client fix; the mbedTLS session-tickets one is only needed for TLS.
  The IPv6 NS leak is fixed upstream in v4.5.0. Copy the patches into
  `firmware/patches/`. Note that `zephyr-project.sh` applies them to the shared
  `/opt/toolchains/zephyr` for the container's lifetime.
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
  and track are exposed, and the rotor pulls off after loosening the hub
  insert's pinch bolt.
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
- Parts and print/assembly instructions: see **`cad/README.md`**.
- Order of work:
  1. Pump head, **done (v1, 2026-11-25)**. First print: base, rotor, and the
     3 track inserts, to tune occlusion and torque.
  2. Reservoir, **done (v1, 2026-11-25)**.
  3. Electronics enclosure, **done (v1, 2026-11-25)**.
- **Electronics housing (v1, 2026-11-25)**: `cad/electronics.py`.
  - A 91 × 35 × 59 mm box bolted to the pump head's **+y (back) wall** with
    2× M3 into heat-set inserts (added to `pump_base`).
  - It holds a 5 × 7 cm perfboard on 4 printed M2 studs.
  - Cable holes are in the bottom, USB-C is on the side, and the vents are in
    the front lid. There are no openings on top.
- **Assembly instructions (LEGO style)**: `cad/instructions.py` renders every
  step in FreeCAD from the same models, and `cad/compose_instructions.py`
  lays out the pages. The result is
  **[assembly/assembly_instructions.pdf](assembly/assembly_instructions.pdf)**:
  a cover, a parts inventory and 18 steps.
- **Perfboard layout drawing**: `cad/pcb_layout.py` →
  `docs/electronics/pcb_layout.png`, drawn from the same `PCB_PARTS` table as
  the housing.
- **CAD assumptions to verify on the bench** (all are parameters in
  `cad/params.py`):
  - `STOCK_HUB_Z0` = 1.0: gap between the motor face and the bottom of the
    Ø16.8 hub. **Measure motor face → top of hub**; the model assumes 12.0.
  - Heat-set insert hole sizes (M3 Ø4.0 × 6, M4 Ø5.6 × 5): check the brand of
    the inserts.
  - `SENSOR_SENSE_FROM_BOTTOM` = 12: where the sensor's trip line actually is.
    The clip slots give ±5 mm of adjustment anyway.

## 11. Open questions

### Decided

| # | Question | Decision (2026-11-25) |
|---|----------|----------------------|
| Q4 | Reservoir | **New, printed** |
| Q5 | Hose | 4×7 silicone hose runs **continuously to the drain** |
| Q7 | Failure action | **Notify Home Assistant**. Later: HA automation turns the LG AC off via its Wi-Fi |
| Q8 | Lid fixing | **4× M3 screws** into heat-set inserts |
| Q9 | Firmware | **Zephyr + MQTT + HA MQTT discovery**, built in the existing `zephyr_container` dev container, **upgraded to Zephyr v4.5.0** (2026-11-27) |
| Q13 | Firmware location | **`firmware/` in this repo**. The repo is the inner project at `zephyr_container/workspace/projects/peristaltic_pump` (2026-11-27) |
| Q13a | MQTT/HA code | **Written new** for this project (no existing code to reuse). Deferred, not now |
| Q3 | PSU | **19 V, 2.1 A**, brick 90 × 37 × 26 mm → MP1584 buck, SMBJ22A TVS, 1 A fuse |
| Q6 | Bearings | **608 confirmed** (22 × 7 × 8 mm), 10 pcs |
| Q10a | Reservoir envelope | Baseline = the Masterflow **square reservoir**, ≈ 87 × ~78 × 42 (48.5 with spigot) mm. The wall stays closed for now |

### Still open (needs measuring — parts are not at hand yet)

| # | Question | Why it matters |
|---|----------|----------------|
| Q1 | **Motor** (frame 38 × 38, body 33 mm, holes 31 mm square, hub Ø16.8 × 11 with set screw, **no rating label**; all measured). Still useful: **coil resistance** (to estimate rated current), mount screw thread (M3?), pilot boss Ø | VREF setting; the CAD uses clearances meanwhile |
| Q2 | **Sensor**: output variant (NPN/PNP/V/RS485), supply range, model number on the label; caliper check of the drawing | Interface circuit, wall thickness |
| Q10 | **Wall recess** (deferred, the wall is not being opened now): W × H × D, where the AC drain hose enters, where the drain outlet is, how it is accessed for maintenance, and whether a **taller reservoir** fits | Final envelope, overflow margin, pump head placement |
| Q11 | **TMC2209 module** (16-pin StepStick with micropot, known). Still needed: the pin labels printed on it (is it `PDN`/`UART` or `RX`/`TX`?), any extra pins on top (DIAG/INDEX), the brand/version printed on it, and the sense resistor marking (R100/R110) | VREF formula, phase 2 UART wiring, DIAG availability |
| Q12 | Add a 3rd sensor or a float switch as an independent overflow input? | No hardware AC cut-off exists, so an overflow spills inside the wall |
| Q14 | AC drain hose: ID/OD where it enters the reservoir lid | Inlet fitting |
| — | Masterflow 20: caliper-confirm the values in section 4.6 | Reference envelope |

## 12. Change log

| Date | Version | Change |
|------|---------|--------|
| 2026-11-25 | 0.1 | First draft: requirements, sizing, concept, control strategy, open questions |
| 2026-11-25 | 0.2 | User decisions recorded (printed reservoir, continuous hose, HA notification, M3 lid, Zephyr/MQTT/HA). Unit mounts in the wall recess behind the AC → envelope and overflow concerns. PSU input protection. Masterflow dimensions read from `pump_dimensions.png`. HA entity list |
| 2026-11-25 | 0.3 | Surveyed the user's Zephyr environment (`introduction-to-zephyr`, `hello_eink`). Verified the C3 toolchain, board and TMC2209 driver in the dev container image. The TMC2209 driver is GPIO-only → two-phase plan (standalone, then own UART module). Reuse list (wifi_prov, watchdogs, native_sim tank simulation, leak patches) |
| 2026-11-25 | 0.4 | Masterflow square reservoir (≈ 87 × ~78 × 42 mm) adopted as the envelope baseline. Wall recess deferred. Height/overflow budget computed. Pump head becomes a separate block. Zephyr to be upgraded. MQTT/HA layer to be written new, deferred |
| 2026-11-25 | 0.5 | Measured: motor 38 × 38 × 33 mm (39 mm class, thin torque margin), bearings 608 confirmed, driver is a 16-pin StepStick with micropot. Rotor pitch radius reduced 20 → 16 mm (V_rev ≈ 1.7 mL/rev). Torque mitigations and test procedure. VREF/microstep notes for standalone mode. Pump head ≈ 70 × 70 × 65 mm |
| 2026-11-25 | 0.6 | Motor: 31 mm hole pattern (NEMA 17-compatible), Ø16.8 × 11 mm hub on the shaft, no rating (estimate from coil resistance plus bench tuning). Interchangeable clamp hub insert; mount and base sized so a NEMA 17 can drop in if needed (user decision). CAD unblocked |
| 2026-11-25 | 0.7 | PSU: 19 V 2.1 A, 90 × 37 × 26 mm brick. Buck fixed to MP1584, TVS changed to SMBJ22A, brick added to the envelope |
| 2026-11-25 | 0.8 | **CAD v1**: parametric FreeCAD scripts for the pump head and reservoir (`cad/`). Rollers = 2 stacked 608 each (hose wider than one bearing). Motor interface integrated into the lower rotor disc (stock / NEMA 17 variants). Reservoir: flat floor + strainer cage, dip-tube pickup through the lid, clip-mounted sensors at 13 / 24 mm, volumes computed |
| 2026-11-25 | 0.9 | Electronics: 5 × 7 cm perfboard layout and connections, through-hole protection parts, sensor input network for NPN/push-pull/PNP, bring-up procedure (`electronics.md`). Printed housing on the pump's back wall (`cad/electronics.py`). LEGO-style assembly instructions rendered from the CAD (`docs/assembly/`) |
| 2026-11-27 | 0.10 | Firmware bootstrap prep. The repo becomes the inner project in `zephyr_container/workspace/projects/` (Q13 decided: `firmware/` here). The container is upgraded to Zephyr **v4.5.0** (`41d10b59`) and the image is built on pcut. C3 board, `adi,tmc2209` (still GPIO-only) and MQTT re-verified on v4.5.0. 2 of `hello_eink`'s patches are obsolete (one fixed upstream, one replaced by an upstream feature) |
