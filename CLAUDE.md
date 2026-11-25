# Peristaltic condensate pump — context notes

A quiet, slow, continuously running peristaltic pump that replaces a noisy
Masterflow 20 condensate lift pump. The pump serves a 12k BTU LG split AC in
Curitiba and lifts the water about 40 cm. It is installed in the wall recess
behind the AC indoor unit.

## Where things are

- `first_prompt.md`: the original brief from the user, in their own words.
  Read it for intent.
- `docs/design.md`: **the design document and single source of truth**. It
  holds the requirements, sizing, mechanical, electronics and control strategy,
  the HA entities, and the decided and open questions (section 11). Update it,
  plus its change log, whenever something is decided or measured.
- `sensor_dimensions.png`: drawing of the non-contact capacitive level sensor.
- `pump_dimensions.png`: drawing of the Masterflow 20 (low resolution, values
  read in design.md §4.6).

## State (2026-11-25)

- Phase: **documentation done (v0.2). CAD not started.**
- CAD is blocked on measurements the user will take when they have the parts:
  motor (Q1), sensor variant (Q2), PSU (Q3), bearings (Q6), **wall recess
  envelope (Q10, most important)**, TMC2209 version (Q11).
- Next steps:
  1. Collect the measurements and update design.md.
  2. Write the parametric FreeCAD scripts in `cad/`: pump head first, then a
     test print of the track and rotor for occlusion tuning, then the reservoir,
     then the electronics box.
  3. Firmware, in its own folder or repo.

## Decisions already made by the user (don't re-ask)

- Reservoir: new, 3D printed.
- Hose: the 4×7 silicone hose runs continuously from the pickup to the drain.
- Failure action: notify Home Assistant. Later, HA turns the LG AC off over
  Wi-Fi. No relay.
- Lid: 4× M3 screws into heat-set inserts.
- Firmware: **Zephyr RTOS + MQTT + Home Assistant MQTT discovery**. The user
  has existing projects in this style; ask for the path to one to reuse (Q13).
- PSU: a laptop PSU with no plug. Use a screw terminal or a soldered jack, with
  reverse-polarity protection and a fuse.
- Parts on hand: ESP32-C3 SuperMini, TMC2209 (MakerHero), NEMA stepper (size
  TBC), 10 skateboard bearings (608 TBC), 2 non-contact level sensors
  (AliExpress), 4×7 mm silicone hose.

## Tools and environment

- FreeCAD 1.1.1 AppImage. It runs headless, which has been verified:
  `~/bin/FreeCAD_1.1.1-Linux-x86_64-py311.AppImage freecadcmd script.py`
- KiCad 10 AppImage is also in `~/bin` (for a carrier PCB or schematic later).
- Bambu Studio is installed, so the printer is presumably a Bambu Lab
  (unconfirmed). Material: PETG, plus TPU for the dampers.
- The vendor pages for the Masterflow (eos.com.br), the TMC2209 (makerhero.com)
  and the sensor (AliExpress) block automated fetches. Ask the user for specs
  instead of retrying them.

## Conventions

- CAD is parametric Python. All dimensions live in one params file that
  mirrors design.md, and the scripts export FCStd, STEP and STL.
- The user writes casually in English and Portuguese. Document in English.
