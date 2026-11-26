"""LEGO-style assembly instructions — scene renderer (runs in the FreeCAD GUI, off-screen).

For every step it renders FreeCAD images: the parts already assembled, plus the
new parts exploded away from their final position with red arrows. It also
renders one thumbnail per part for the parts call-out boxes, then writes a
manifest that `compose_instructions.py` (plain Python + PIL) lays out into
pages and a PDF.

Launched by:  cad/build.sh instructions
"""
import json
import math
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import FreeCAD as App  # noqa: E402

V = App.Vector
ROOT = os.path.dirname(HERE)
IMG = os.path.join(ROOT, "docs", "assembly", "img")
LOG = os.path.join(HERE, "out", ".instructions.log")


def log(msg):
    with open(LOG, "a") as f:
        f.write(msg + "\n")


# --- colours ------------------------------------------------------------------
GREY = (0.80, 0.82, 0.85)
BRASS = (0.86, 0.66, 0.22)
DARK = (0.22, 0.22, 0.24)
STEEL = (0.70, 0.71, 0.74)
WHITE = (0.96, 0.96, 0.96)
BLUE = (0.35, 0.55, 0.85)
ORANGE = (0.95, 0.60, 0.20)
HOSE = (0.90, 0.35, 0.25)
ARROW = (0.85, 0.06, 0.06)
GREEN = (0.30, 0.75, 0.45)
RES = (0.36, 0.36, 0.40)
BLACK = (0.10, 0.10, 0.10)
PCB = (0.20, 0.55, 0.30)
COMP = {"U1": (0.15, 0.15, 0.18), "U3": (0.55, 0.15, 0.55), "U2": (0.12, 0.28, 0.65),
        "C1": (0.12, 0.12, 0.12), "BZ1": (0.12, 0.12, 0.12)}


def moved(shape, off):
    s = shape.copy()
    s.translate(V(*off))
    return s


def rotz(shape, deg):
    s = shape.copy()
    s.rotate(V(0, 0, 0), V(0, 0, 1), deg)
    return s


def _arrow_for(bb, off, d):
    import cadlib as L
    c = bb.Center
    ext = 0.5 * (abs(d.x) * bb.XLength + abs(d.y) * bb.YLength + abs(d.z) * bb.ZLength)
    return L.arrow(c + off - d * (ext + 1.5), c + d * (ext + 1.5))


def explode(items, offset, group=False):
    """items: [(shape, colour)] at their final position -> exploded shapes + arrows.

    group=True: the items form one sub-assembly and get a single arrow.
    """
    out = []
    off = V(*offset)
    d = V(off).normalize()
    for shape, col in items:
        out.append((moved(shape, offset), col, 0))
    boxes = [s.BoundBox for s, _ in items]
    if group:
        bb = boxes[0]
        for b in boxes[1:]:
            bb.add(b)
        boxes = [bb]
    for bb in boxes:
        a = _arrow_for(bb, off, d)
        if a is not None:
            out.append((a, ARROW, 0))
    return out


def fixed(items):
    return [(s, c, 0) for s, c in items]


def main():
    import cadlib as L
    import params as P
    import pump
    import reservoir as R
    import electronics as E
    from guiutil import render_scene, ISO, BACK

    os.makedirs(IMG, exist_ok=True)

    # ---------------------------------------------------------------- pump parts
    H = P.HOUSING_HALF
    base = pump.make_base()
    motor = pump.make_motor_dummy()
    rl = pump.make_rotor_lower("stock")
    ru = pump.make_rotor_upper()
    sleeve0 = pump.make_axle_sleeve()
    track = pump.make_track_insert(P.OCCLUSION_GAP)
    hose = pump.make_hose_visual()
    plid = pump.make_lid()
    roller_pos = [L.polar(P.PITCH_R, a) for a in pump.ROLLER_ANGLES]

    ins_top = [L.heat_insert((sx * P.LID_SCREW_XY, sy * P.LID_SCREW_XY, P.Z_LID), (0, 0, -1),
                             P.INSERT_M3_D, 5.7) for sx in (-1, 1) for sy in (-1, 1)]
    ins_back = [L.heat_insert((sx * P.BOX_MOUNT_X, H, P.BOX_MOUNT_Z), (0, -1, 0), P.INSERT_M3_D, 5.7)
                for sx in (-1, 1)]
    hs = P.MOTOR_HOLE_SPACING / 2
    motor_screws = [L.screw((x, y, P.Z_FLOOR), (0, 0, -1), 3, 8) for x in (-hs, hs) for y in (-hs, hs)]
    ins_m4 = [L.heat_insert((x, y, P.Z_DISC1_TOP), (0, 0, -1), P.INSERT_M4_D, P.INSERT_M4_L)
              for x, y in roller_pos]
    csk = [L.screw((x, y, P.Z_DISC2_TOP), (0, 0, -1), 4, 25, head_d=8, head_h=1.9, countersunk=True)
           for x, y in roller_pos]
    lid_screws = [L.screw((sx * P.LID_SCREW_XY, sy * P.LID_SCREW_XY, P.Z_TOP), (0, 0, -1), 3, 10)
                  for sx in (-1, 1) for sy in (-1, 1)]

    def roller_at(x, y, z):
        parts = [(moved(sleeve0, (x, y, z)), WHITE)]
        for k in range(P.ROLLER_STACK):
            z0 = z + P.SPACER_T + k * P.BRG_W
            parts.append((L.ring(P.BRG_OD / 2, P.BRG_ID / 2, z0, z0 + P.BRG_W, x, y), STEEL))
        return parts

    rollers = [p for x, y in roller_pos for p in roller_at(x, y, P.Z_DISC1_TOP)]
    rotor_parts = [(rl, BLUE), (ru, BLUE)] + rollers + [(s, BRASS) for s in ins_m4] + \
                  [(s, STEEL) for s in csk]
    # turn the rotor so one grub screw faces the access hole in the -y wall
    rot = -90
    rotor_parts_r = [(rotz(s, rot), c) for s, c in rotor_parts]
    z_g = (P.Z_CLAMP_BOT + P.Z_DISC1_BOT) / 2
    grubs = []
    for a in (0 + rot, 90 + rot):
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        grubs.append(L.grub((15.5 * ca, 15.5 * sa, z_g), (-ca, -sa, 0)))

    pump_done = [(base, GREY), (motor, DARK)] + [(s, BRASS) for s in ins_top + ins_back] + \
                [(s, STEEL) for s in motor_screws] + rotor_parts_r + [(g, STEEL) for g in grubs] + \
                [(track, ORANGE), (hose, HOSE), (plid, GREY)] + [(s, STEEL) for s in lid_screws]

    # ---------------------------------------------------------------- reservoir parts
    rb = R.make_body()
    rlid = R.make_lid()
    strainer = moved(R.make_strainer(), (P.PICKUP_XY[0], P.PICKUP_XY[1], P.RES_FLOOR))
    res_ins_top = [L.heat_insert((x, y, P.RES_H), (0, 0, -1), P.INSERT_M3_D, 5.7) for x, y in R.corner_xy()]
    sens, clips, sens_ins, clip_screws = {}, {}, {}, {}
    for name, face, trip in R.SENSORS:
        zc = R.sensor_z_center(trip)
        u = face[1]
        h = P.SENSOR_T - 0.2
        sens[name] = R.place_on_face(R.sensor_dummy_local(u, zc), face)
        clips[name] = R.place_on_face(R.sensor_clip_local(u, zc), face)
        sens_ins[name] = [R.place_on_face(L.heat_insert((u + du, -R.Y2 - h, zc), (0, 1, 0), P.INSERT_M3_D, 5.7), face)
                          for du in (-P.SENSOR_CLIP_SCREW_DX, P.SENSOR_CLIP_SCREW_DX)]
        clip_screws[name] = [R.place_on_face(L.screw((u + du, -R.Y2 - h - 2.5, zc), (0, 1, 0), 3, 8), face)
                             for du in (-P.SENSOR_CLIP_SCREW_DX, P.SENSOR_CLIP_SCREW_DX)]
    out_dir = {"L": (0, -1, 0), "H": (1, 0, 0)}
    res_lid_screws = [L.screw((x, y, R.TOP), (0, 0, -1), 3, 8) for x, y in R.corner_xy()]
    px, py = P.PICKUP_XY
    res_hose = L.cyl(P.HOSE_OD / 2, P.RES_FLOOR + P.STRAINER_H - 6, R.TOP + 25, px, py).cut(
        L.cyl(P.HOSE_ID / 2, 0, R.TOP + 30, px, py))
    res_done = [(rb, RES), (strainer, GREEN), (rlid, RES), (res_hose, HOSE)] + \
               [(s, BRASS) for s in res_ins_top] + [(s, STEEL) for s in res_lid_screws]
    for n in ("L", "H"):
        res_done += [(sens[n], BLACK), (clips[n], BLUE)] + [(s, BRASS) for s in sens_ins[n]] + \
                    [(s, STEEL) for s in clip_screws[n]]

    # ---------------------------------------------------------------- electronics parts
    ebox = E.make_box()
    elid = E.make_lid()
    board, comps = E.make_board_dummy()
    board_items = [(board, PCB)] + [(c, COMP.get(k, (0.93, 0.93, 0.9) if k.startswith("J") else DARK))
                                    for k, c in comps.items()]
    mount_screws = E.make_mount_screws()
    m2 = [L.screw((x, E.Y_BOARD_F, z), (0, -1, 0), 2, 6) for x, z in E.board_holes()]
    elid_screws = [L.screw((sx * E.PILLAR_X, E.Y_FRONT, z), (0, -1, 0), 3, 10)
                   for sx in (-1, 1) for z in P.BOX_LID_SCREW_Z]

    # ---------------------------------------------------------------- thumbnails
    thumbs = {
        "base": ("Pump base", [(base, GREY)]),
        "insert_m3": ("M3 heat-set insert", [(L.heat_insert((0, 0, 0), (0, 0, -1), 4.0, 5.7), BRASS)]),
        "motor": ("Stepper motor", [(motor, DARK)]),
        "m3x8": ("M3 × 8 screw", [(L.screw((0, 0, 0), (0, 0, -1), 3, 8), STEEL)]),
        "m3x10": ("M3 × 10 screw", [(L.screw((0, 0, 0), (0, 0, -1), 3, 10), STEEL)]),
        "sleeve": ("Axle sleeve (printed)", [(sleeve0, WHITE)]),
        "bearing": ("608 bearing", [(L.ring(11, 4, 0, 7), STEEL)]),
        "rotor_lower": ("Lower rotor (printed)", [(rl, BLUE)]),
        "insert_m4": ("M4 heat-set insert", [(L.heat_insert((0, 0, 0), (0, 0, -1), 5.6, 5), BRASS)]),
        "roller": ("Roller from step 3", roller_at(0, 0, 0)),
        "rotor_upper": ("Upper rotor (printed)", [(ru, BLUE)]),
        "m4x25": ("M4 × 25 countersunk", [(L.screw((0, 0, 0), (0, 0, -1), 4, 25, 8, 1.9, True), STEEL)]),
        "nut_m3": ("M3 nut", [(L.nut((0, 0, 0)), STEEL)]),
        "grub_m3": ("M3 × 8 grub screw", [(L.grub((0, 0, 0), (0, 0, 1)), STEEL)]),
        "rotor_asm": ("Rotor from step 6", [(s, c) for s, c in rotor_parts]),
        "track": ("Track insert 2.6 (2 dimples)", [(track, ORANGE)]),
        "hose": ("Silicone hose 4 × 7", [(L.cyl_dir(3.5, 60, (0, 0, 0), (1, 0.3, 0.2)).cut(
            L.cyl_dir(2, 62, (-1, -0.3, -0.2), (1, 0.3, 0.2))), HOSE)]),
        "pump_lid": ("Pump lid (printed)", [(plid, GREY)]),
        "res_body": ("Reservoir (printed)", [(rb, RES)]),
        "strainer": ("Strainer (printed)", [(R.make_strainer(), GREEN)]),
        "sensor": ("Level sensor", [(R.sensor_dummy_local(0, 0), BLACK)]),
        "clip": ("Sensor clip (printed)", [(R.sensor_clip_local(0, 0), BLUE)]),
        "res_lid": ("Reservoir lid (printed)", [(rlid, RES)]),
        "board": ("Soldered perfboard", board_items),
        "ebox": ("Electronics box (printed)", [(ebox, BLUE)]),
        "elid": ("Box lid (printed)", [(elid, BLUE)]),
        "m2x6": ("M2 × 6 self-tapping", [(L.screw((0, 0, 0), (0, 0, -1), 2, 6), STEEL)]),
    }
    # sub-assemblies are not counted in the parts inventory
    SUB = {"roller", "rotor_asm", "board", "base", "motor"}

    # ---------------------------------------------------------------- steps
    S = []

    def step(section, title, existing, new, callouts, note, look=ISO, times=1):
        # copy: later steps extend their own lists and must not leak into this one
        S.append(dict(section=section, title=title, existing=list(existing), new=list(new),
                      callouts=callouts, note=note, look=look, times=times))

    pump_s = "Pump head"
    step(pump_s, "Heat-set inserts", [(base, GREY)],
         [((0, 0, 28), [(s, BRASS) for s in ins_top]), ((0, 28, 0), [(s, BRASS) for s in ins_back])],
         [("base", 1), ("insert_m3", 6)],
         "Soldering iron at ~220 °C, press each insert until it is flush. "
         "4 in the wall tops (lid), 2 in the back wall (electronics box).", look=(-0.9, -1.0, -0.9))
    step(pump_s, "Motor", [(base, GREY)] + [(s, BRASS) for s in ins_top + ins_back],
         [((0, 0, -45), [(motor, DARK)]), ((0, 0, 32), [(s, STEEL) for s in motor_screws])],
         [("motor", 1), ("m3x8", 4)],
         "Motor boss into the round pocket under the base. Cable facing the back (+y), "
         "where the electronics box will be.", look=(-1.0, 1.0, -0.55))
    step(pump_s, "Rollers — build 3", [(sleeve0, WHITE)],
         [((0, 0, 22), [roller_at(0, 0, 0)[1]]), ((0, 0, 40), [roller_at(0, 0, 0)[2]])],
         [("sleeve", 1), ("bearing", 2)],
         "Slide 2 bearings onto each printed sleeve, flange at the bottom. Make 3.", times=3)
    step(pump_s, "Lower rotor inserts", [(rl, BLUE)],
         [((0, 0, 22), [(s, BRASS) for s in ins_m4])],
         [("rotor_lower", 1), ("insert_m4", 3)],
         "Press the M4 inserts into the flat top face (the face without the clamp).")
    step(pump_s, "Rollers on the rotor", [(rl, BLUE)] + [(s, BRASS) for s in ins_m4],
         [((0, 0, 30), rollers[0:3], "group"), ((0, 0, 30), rollers[3:6], "group"),
          ((0, 0, 30), rollers[6:9], "group")], [("roller", 3)],
         "Stand each roller over an insert.")
    step(pump_s, "Upper rotor", [(rl, BLUE)] + [(s, BRASS) for s in ins_m4] + rollers,
         [((0, 0, 28), [(ru, BLUE)]), ((0, 0, 58), [(s, STEEL) for s in csk])],
         [("rotor_upper", 1), ("m4x25", 3)],
         "Bosses down onto the bearings. Tighten until the rollers spin freely with no up-down play.")
    pump_base_asm = [(base, GREY), (motor, DARK)] + [(s, BRASS) for s in ins_top + ins_back] + \
                    [(s, STEEL) for s in motor_screws]
    step(pump_s, "Rotor onto the motor", pump_base_asm,
         [((0, 0, 60), rotor_parts_r, "group")] +
         [((25 * math.cos(math.radians(a)), 25 * math.sin(math.radians(a)), 0), [(g, STEEL)])
          for g, a in zip(grubs, (0 + rot, 90 + rot))],
         [("rotor_asm", 1), ("nut_m3", 2), ("grub_m3", 2)],
         "First drop 2 M3 nuts into the slots under the clamp. Slide the rotor over the motor hub. "
         "Tighten each grub screw through the hole in the front wall (1.5 mm hex key), "
         "turning the rotor between them.")
    asm = pump_base_asm + rotor_parts_r + [(g, STEEL) for g in grubs]
    step(pump_s, "Track insert", asm, [((0, 0, 50), [(track, ORANGE)])], [("track", 1)],
         "Start with the 2.6 mm insert (2 dimples). The 2.4 and 2.8 inserts are for tuning: "
         "use the loosest one that still pumps.")
    asm = asm + [(track, ORANGE)]
    step(pump_s, "Hose", asm, [((0, 0, 35), [(hose, HOSE)], "group")], [("hose", 1)],
         "Push the hose between the rollers and the track while turning the rotor by hand. "
         "Both legs go into the U-slots in the front wall.")
    asm = asm + [(hose, HOSE)]
    step(pump_s, "Lid", asm,
         [((0, 0, 30), [(plid, GREY)]), ((0, 0, 58), [(s, STEEL) for s in lid_screws])],
         [("pump_lid", 1), ("m3x10", 4)],
         "The tabs under the lid press the hose into the U-slots so it cannot creep.")

    res_s = "Reservoir"
    ins_items = [((0, 0, 25), [(s, BRASS) for s in res_ins_top])] + \
                [(tuple(25 * c for c in out_dir[n]), [(s, BRASS) for s in sens_ins[n]]) for n in ("L", "H")]
    step(res_s, "Heat-set inserts", [(rb, RES)], ins_items, [("res_body", 1), ("insert_m3", 8)],
         "4 in the corner columns (lid), 2 in each pair of sensor bosses. The bosses are blind: "
         "the wall stays watertight. Test-fill the box with water before going on.")
    r_asm = [(rb, RES)] + [(s, BRASS) for s in res_ins_top + sens_ins["L"] + sens_ins["H"]]
    step(res_s, "Strainer", r_asm, [((0, 0, 40), [(strainer, GREEN)])], [("strainer", 1)],
         "Into the ring on the floor, in the corner nearest the pump.")
    r_asm = r_asm + [(strainer, GREEN)]
    new = []
    for n in ("L", "H"):
        dv = out_dir[n]
        new += [(tuple(18 * c for c in dv), [(sens[n], BLACK)]),
                (tuple(34 * c for c in dv), [(clips[n], BLUE)]),
                (tuple(52 * c for c in dv), [(s, STEEL) for s in clip_screws[n]])]
    step(res_s, "Level sensors", r_asm, new, [("sensor", 2), ("clip", 2), ("m3x8", 4)],
         "L sensor on the front wall, H sensor on the right wall. Sensing face against the wall, "
         "cable pointing down. Leave the screws slightly loose: the slots adjust the height.")
    for n in ("L", "H"):
        r_asm += [(sens[n], BLACK), (clips[n], BLUE)] + [(s, STEEL) for s in clip_screws[n]]
    step(res_s, "Lid and pickup hose", r_asm,
         [((0, 0, 28), [(rlid, RES)]), ((0, 0, 50), [(s, STEEL) for s in res_lid_screws]),
          ((0, 0, 75), [(res_hose, HOSE)])],
         [("res_lid", 1), ("m3x8", 4), ("hose", 1)],
         "Feed the pump's inlet hose through the small collar and push it into the strainer socket. "
         "The AC drain hose just drops into the big collar.")

    el_s = "Electronics"
    step(el_s, "Solder the board", [], [((0, 0, 0), board_items)], [("board", 1)],
         "Solder by following docs/electronics.md. Set the MP1584 to 5.0 V BEFORE fitting the ESP32 "
         "and the sensors.", look=(-0.25, -1.0, -0.35))
    pump_full = [(base, GREY), (motor, DARK), (plid, GREY)] + [(s, STEEL) for s in lid_screws]
    step(el_s, "Box on the pump", pump_full,
         [((0, 40, 0), [(ebox, BLUE)]), ((0, 75, 0), [(s, STEEL) for s in mount_screws])],
         [("ebox", 1), ("m3x8", 2)],
         "From inside the box, screw it to the 2 inserts in the pump's back wall.", look=BACK)
    e_asm = pump_full + [(ebox, BLUE)] + [(s, STEEL) for s in mount_screws]
    step(el_s, "Board into the box", e_asm,
         [((0, 45, 0), board_items, "group"), ((0, 75, 0), [(s, STEEL) for s in m2])],
         [("board", 1), ("m2x6", 4)],
         "Components facing out, ESP32 USB-C towards the side opening.", look=BACK)
    e_asm = e_asm + board_items + [(s, STEEL) for s in m2]
    step(el_s, "Cables and lid", e_asm,
         [((0, 30, 0), [(elid, BLUE)]), ((0, 58, 0), [(s, STEEL) for s in elid_screws])],
         [("elid", 1), ("m3x10", 4)],
         "Cables come in through the bottom holes: J1 power, J3 sensor L, J2 motor, J4 sensor H. "
         "Leave a drip loop in each cable before closing the lid.", look=BACK)

    # ---------------------------------------------------------------- render
    manifest = {"steps": [], "thumbs": {}}
    for key, (label, items) in thumbs.items():
        path = os.path.join(IMG, f"part_{key}.png")
        render_scene(path, fixed(items), ISO, size=(500, 500))
        manifest["thumbs"][key] = {"img": os.path.relpath(path, ROOT), "label": label,
                                   "inventory": key not in SUB}
    log(f"  thumbnails: {len(thumbs)}")
    for i, st in enumerate(S, 1):
        items = fixed(st["existing"])
        for entry in st["new"]:
            off, parts = entry[0], entry[1]
            group = len(entry) > 2 and entry[2] == "group"
            items += explode(parts, off, group) if any(off) else fixed(parts)
        path = os.path.join(IMG, f"step_{i:02d}.png")
        render_scene(path, items, st["look"], size=(1600, 1150))
        manifest["steps"].append(dict(n=i, section=st["section"], title=st["title"], note=st["note"],
                                      times=st["times"],
                                      img=os.path.relpath(path, ROOT),
                                      callouts=[[k, q] for k, q in st["callouts"]]))
        log(f"  step {i:2d}: {st['section']} / {st['title']}")
    # cover: everything assembled, reservoir beside the pump, electronics box behind it
    res_off = (-150, -10, -P.MOTOR_LEN)
    cover = fixed(pump_done) + fixed([(ebox, BLUE), (elid, BLUE)] + board_items) + \
        [(moved(s, res_off), c, 0) for s, c in res_done]
    render_scene(os.path.join(IMG, "cover.png"), cover, (-0.55, 1.0, -0.6), size=(1800, 1200),
                 ortho=False)
    manifest["cover"] = "docs/assembly/img/cover.png"
    with open(os.path.join(IMG, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
    log("  manifest written")


try:
    if os.path.exists(LOG):
        os.remove(LOG)
    main()
except Exception:
    log(traceback.format_exc())
finally:
    os._exit(0)
