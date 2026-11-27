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

## State (2026-11-27)

- Phase: **prototype v1 fully designed, nothing printed yet; firmware bootstrap starting**. Design doc
  v0.10.
  - CAD (`cad/`): pump head, reservoir, and the electronics housing on the
    pump's back wall.
  - Electronics: `docs/electronics.md`, a 5 × 7 cm perfboard with its layout
    in `PCB_PARTS`.
  - LEGO-style assembly instructions: `docs/assembly/`, as a PDF and PNG
    pages.
- Build everything with `cad/build.sh` (headless FreeCAD, ~2 min). It
  regenerates the STL/STEP/FCStd files and previews, runs the interference
  checks, makes the perfboard drawing, re-renders the instructions and rebuilds
  the PDF.
  - Edit dimensions only in `cad/params.py`. The perfboard placement is
    `PCB_PARTS` there, and the housing openings follow it.
- GUI-only work (colours, saved camera, instruction renders) runs the FreeCAD
  GUI off-screen with `QT_QPA_PLATFORM=offscreen`. In GUI mode `print()` is
  invisible, so those scripts log to `cad/out/.*.log`.
- Camera direction: set it through `guiutil.set_view` (Coin orientation).
  `setViewDirection` does not work there because of the view animation.
- CAD facts that are easy to forget:
  - each roller = **2 stacked 608** (14 mm), because the hose is wider than
    one bearing;
  - the motor interface is part of the lower rotor disc, with `stock` and
    `nema17` variants;
  - there are 3 track inserts (gaps 2.4 / 2.6 / 2.8);
  - the reservoir has a dip-tube pickup through the lid, a strainer cage,
    clip-mounted sensors at 13 / 24 mm, and **~3 min overflow margin**.
- CAD assumptions to verify on the bench: `STOCK_HUB_Z0` (measure motor face
  → top of hub; 12.0 is assumed), the heat-set insert hole sizes, and the
  sensor trip line.
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
  (Q2), driver pin labels (Q11).
- **The wall stays closed for now** (Q10 deferred). The envelope baseline is
  the Masterflow **square reservoir**, about 87 × ~78 × 42 mm (48.5 mm with
  the spigot); see design.md §4.6. The pump head is a separate block joined
  only by the hose.
- Firmware: the bootstrap starts now, on **Zephyr v4.5.0** (upgraded
  2026-11-27). The MQTT + HA discovery layer is written new.
- Next steps:
  1. The user prints and tests the pump head (torque test, choose the gap
     insert) and the reservoir (watertightness). Feed the results back into
     `params.py` and design.md.
  2. The user solders the perfboard (docs/electronics.md). Verify the [TBC]
     pin orders against the module silkscreens and the perfboard hole
     inset/diameter.
  3. Firmware: bootstrap `firmware/` (hello_eink-style app, `native_sim` tank
     simulation, then the MQTT/HA layer).

## Decisions already made by the user (don't re-ask)

- Reservoir: new, 3D printed.
- Hose: the 4×7 silicone hose runs continuously from the pickup to the drain.
- Failure action: notify Home Assistant. Later, HA turns the LG AC off over
  Wi-Fi. No relay.
- Lid: 4× M3 screws into heat-set inserts.
- Firmware: **Zephyr RTOS + MQTT + Home Assistant MQTT discovery**.

## Firmware environment (updated 2026-11-27; details in design.md §6.5)

- **This repo is the inner project** of `zephyr_container`
  (`glmoritz/zephyr_container`), at
  `zephyr_container/workspace/projects/peristaltic_pump`. The outer repo
  gitignores it. The firmware goes in `firmware/` (Q13 decided).
  - On pccasa (this desktop) the repo physically lives in
    `~/00_tmp/zephyr/introduction-to-zephyr/workspace/projects/peristaltic_pump`,
    and `~/00_tmp/peristaltic_pump` is a symlink to it. The container mounts
    only `workspace/`.
  - **Heavy builds run on pcut** (`ssh moritz@10.99.0.3`, fast VLAN;
    pccasa's disk is full). There the outer repo is
    `~/00_tmp/zephyr_container` (branch `esp32c6-support`, which has the
    RISC-V helper/OpenOCD work that `main` lacks), with a clone of this repo in
    `workspace/projects/`.
- Container: `Dockerfile.moritz` is pinned to **Zephyr v4.5.0 @ `41d10b59`**,
  SDK 1.0.1, built on pcut as `zephyr-moritz:4.5.0`. The old 4.5.0-dev
  (`fbfc5f19`) images on pccasa were deleted.
- Verified in the v4.5.0 tree:
  - the in-tree `esp32c3_supermini` board;
  - the `adi,tmc2209` driver, still **GPIO-only, no UART** → phase 1
    standalone, phase 2 our own UART register module. It uses the split
    stepper API (`stepper.h` + `stepper_ctrl.h`);
  - the MQTT library.
- The reference app is `workspace/projects/hello_eink`
  (repo `glmoritz/zephyr_eink_waveshare`, on pccasa only). Reuse from it:
  - `modules/wifi_prov` (captive-portal provisioning);
  - net/sys watchdogs, settings on NVS, the telnet shell and NTP;
  - the `native_sim` dual target;
  - its Zephyr patches. On v4.5.0, 4 still apply. `ipv6_nbr_ns_pending_leak`
    is fixed upstream and `tls_session_export` is superseded; both were
    deleted from hello_eink.

  It has **no MQTT/HA code**.
- `zephyr-project.sh` patches the shared `/opt/toolchains/zephyr` in place
  for the container's lifetime, so one project's patches leak into the next
  build in the same container.
- PSU: a laptop PSU, **19 V 2.1 A, brick 90 × 37 × 26 mm**, with no plug. Use
  a screw terminal or a soldered jack, with reverse-polarity protection, a 1 A
  fuse, an SMBJ22A TVS, and an MP1584 buck to 5 V.
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
