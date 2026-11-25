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
- Measured so far:
  - **motor 38 × 38 × 33 mm**: 39 mm class, so the torque margin is thin
    → the rotor pitch radius was reduced to 16 mm; see design.md §3.3;
  - **bearings 608 confirmed**;
  - **driver: 16-pin StepStick with micropot**.
- Motor details:
  - **31 mm square hole pattern** (43.5 mm diagonal), which is
    NEMA 17-compatible;
  - a **Ø16.8 × 11 mm hub with a set screw** on the shaft → the rotor uses an
    interchangeable clamp hub insert;
  - **no current rating** → estimate it from the coil resistance and tune on
    the bench.
- If the motor is too weak, **the user will buy a NEMA 17**. Keep the mount,
  the base pocket (42.3 mm) and the hub insert swappable.
- **The CAD is unblocked.** Nice to have: coil resistance (Q1), sensor variant
  (Q2), PSU label (Q3), driver pin labels (Q11).
- **The wall stays closed for now** (Q10 deferred). The envelope baseline is
  the Masterflow **square reservoir**, about 87 × ~78 × 42 mm (48.5 mm with
  the spigot); see design.md §4.6. The pump head is a separate block joined
  only by the hose.
- Firmware is **deferred**:
  - the MQTT + HA discovery layer will be written new;
  - the user will **upgrade Zephyr** (it is not pinned at 4.5.0-dev), so
    re-verify the board, the TMC2209 driver and the stepper API on the new
    version.
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
- Firmware: **Zephyr RTOS + MQTT + Home Assistant MQTT discovery**.

## Firmware environment (surveyed 2026-11-25; details in design.md §6.5)

- The Zephyr environment is
  `~/00_tmp/zephyr/introduction-to-zephyr` (repo `glmoritz/zephyr_container`).
  - Its dev container builds `Dockerfile.moritz`: Zephyr 4.5.0-dev @
    `fbfc5f19`, SDK 1.0.1.
  - The current image is `vsc-introduction-to-zephyr-a5b93…`. Don't use
    `env-zephyr-espressif:latest`, which is the old 4.3.0.
  - The container mounts only `workspace/`. Product apps go in
    `/workspace/projects/<name>`, each its own repo. Read
    `workspace/AGENTS.md` there.
- Verified in the image:
  - the `riscv64-zephyr-elf` toolchain;
  - the in-tree `esp32c3_supermini` board;
  - the `adi,tmc2209` driver, which is **GPIO-only, no UART** → phase 1
    standalone, phase 2 our own UART register module;
  - the MQTT library.
- The reference app is `workspace/projects/hello_eink`. Reuse from it:
  - `modules/wifi_prov` (captive-portal provisioning);
  - net/sys watchdogs, settings on NVS, the telnet shell and NTP;
  - the `native_sim` dual target, to simulate the tank and control loop on the
    host;
  - the leak patches.

  It has **no MQTT/HA code**, so that layer is new work unless the user points
  to another project (Q13).
- `workspace/scripts/zephyr-project.sh` hardcodes the Xtensa S3 gdb/gcc links,
  so it needs to be generalised for the RISC-V C3.
- Open: whether the firmware lives in `firmware/` in this repo (then
  clone the repo into `workspace/projects/`) or in a separate repo (Q13).
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
