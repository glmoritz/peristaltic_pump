"""Peristaltic pump head — parametric FreeCAD model.

Run headless:  cad/build.sh   (or: freecadcmd cad/pump.py)
Outputs:       cad/out/pump_*.stl / .step, cad/out/pump_assembly.FCStd, previews
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import FreeCAD as App  # noqa: E402

import params as P  # noqa: E402
from cadlib import (V, OUT_DIR, box, cyl, cyl_dir, ring, rounded_box, polar,  # noqa: E402
                    fuse_all, export_part, save_doc, report, check_no_overlap, render)

H = P.HOUSING_HALF
ROLLER_ANGLES = [90 + i * 360 / P.N_ROLLERS for i in range(P.N_ROLLERS)]
Z_GRUB = (P.Z_CLAMP_BOT + P.Z_DISC1_BOT) / 2


def cavity(z0, z1):
    """Free space inside the housing: round over the track, square over the legs."""
    return cyl(P.CAVITY_R, z0, z1).fuse(box(-P.CAVITY_R, P.CAVITY_R, -P.CAVITY_Y, 0, z0, z1))


# --------------------------------------------------------------------------
# Base: plate + walls; the motor bolts underneath, the track insert drops in
# --------------------------------------------------------------------------
def make_base():
    s = rounded_box(-H, H, -H, H, 0, P.Z_LID, P.HOUSING_CORNER_R)
    s = s.cut(cavity(P.Z_FLOOR, P.Z_LID + 1))

    # mounting ears on +-x (rubber grommets / screws to the wall-recess floor)
    for sx in (-1, 1):
        x0, x1 = (H - 2, H + P.EAR_L) if sx > 0 else (-H - P.EAR_L, -H + 2)
        ear = rounded_box(x0, x1, -P.EAR_W / 2, P.EAR_W / 2, 0, P.PLATE_T, 4)
        ear = ear.cut(cyl(P.EAR_HOLE / 2, -1, P.PLATE_T + 1, sx * (H + P.EAR_L / 2), 0))
        s = s.fuse(ear)

    # motor interface: hub clearance, pilot pocket, 4x M3 on the 31 mm square
    s = s.cut(cyl(19.5 / 2, -1, P.Z_FLOOR + 1))
    s = s.cut(cyl(P.MOTOR_PILOT_POCKET_D / 2, -1, P.MOTOR_PILOT_POCKET_H))
    hs = P.MOTOR_HOLE_SPACING / 2
    for x in (-hs, hs):
        for y in (-hs, hs):
            s = s.cut(cyl(P.HOLE_M3 / 2, -1, P.Z_FLOOR + 1, x, y))

    # lid screws: heat-set inserts in the wall tops
    for x in (-P.LID_SCREW_XY, P.LID_SCREW_XY):
        for y in (-P.LID_SCREW_XY, P.LID_SCREW_XY):
            s = s.cut(cyl(P.INSERT_M3_D / 2, P.Z_LID - P.INSERT_M3_L, P.Z_LID + 1, x, y))

    # electronics box mount: 2x M3 heat-set inserts in the +y (back) wall
    for sx in (-1, 1):
        s = s.cut(cyl_dir(P.INSERT_M3_D / 2, P.INSERT_M3_L + 0.01,
                          (sx * P.BOX_MOUNT_X, H + 0.01, P.BOX_MOUNT_Z), (0, -1, 0)))

    # hose saddles: round-bottom U-slots through the -y wall, bottom = hose bottom
    r_slot = P.HOSE_SADDLE_W / 2
    z_c = P.Z_HOSE - P.HOSE_OD / 2 + r_slot
    for sx in (-1, 1):
        x = sx * P.HOSE_X
        s = s.cut(box(x - r_slot, x + r_slot, -H - 1, -P.CAVITY_Y + 1, z_c, P.Z_LID + 1))
        s = s.cut(cyl_dir(r_slot, H, (x, -H - 1, z_c), (0, 1, 0)))

    # access hole for the hub-clamp grub screw (turn the rotor so a grub faces -y)
    s = s.cut(cyl_dir(2.0, 12, (0, -H - 1, Z_GRUB), (0, 1, 0)))
    # drain / cleaning hole in the floor of the leg area
    s = s.cut(cyl(1.5, -1, P.Z_FLOOR + 1, 0, -P.CAVITY_Y + 3))
    return s.removeSplitter()


# --------------------------------------------------------------------------
# Track insert: swappable C-band; its inner face is the occlusion surface
# --------------------------------------------------------------------------
def make_track_insert(gap):
    rt = P.ROLLER_R + gap
    ro = P.CAVITY_R - P.CLR
    z0, z1 = P.Z_FLOOR, P.Z_LID - 0.2
    arc = ring(ro, rt, z0, z1).common(box(-50, 50, 0, 50, z0 - 1, z1 + 1))
    legs = [box(rt, ro, -P.CAVITY_Y + P.CLR, 0.01, z0, z1),
            box(-ro, -rt, -P.CAVITY_Y + P.CLR, 0.01, z0, z1)]
    s = fuse_all([arc] + legs)
    # identification dimples on top of the +x leg: 1 = 2.4, 2 = 2.6, 3 = 2.8 ...
    n = 1 + min(range(len(P.GAP_VARIANTS)), key=lambda i: abs(P.GAP_VARIANTS[i] - gap))
    xm = (rt + ro) / 2
    for i in range(n):
        s = s.cut(cyl(0.9, z1 - 0.8, z1 + 1, xm, -6 - 4 * i))
    return s.removeSplitter()


# --------------------------------------------------------------------------
# Rotor
# --------------------------------------------------------------------------
def make_rotor_lower(motor=P.MOTOR):
    s = cyl(P.DISC_R, P.Z_DISC1_BOT, P.Z_DISC1_TOP)
    s = s.fuse(cyl(P.CLAMP_OD / 2, P.Z_CLAMP_BOT, P.Z_DISC1_BOT + 0.01))

    if motor == "stock":
        # clamps over the Ø16.8 x 11 hub already on the motor shaft
        s = s.cut(cyl(P.CLAMP_BORE / 2, P.Z_CLAMP_BOT - 1, P.Z_DISC1_BOT))
        s = s.cut(cyl(3.0, P.Z_CLAMP_BOT, P.Z_DISC1_TOP + 1))      # shaft tip clearance
        r_in = P.CLAMP_BORE / 2
        grub_angles = (0, 90)
    else:
        # NEMA 17: Ø5 D-shaft, grub screw onto the flat
        d = P.NEMA17_SHAFT_D + 0.2
        shaft = cyl(d / 2, P.Z_CLAMP_BOT - 1, P.Z_DISC1_TOP + 1)
        flat_x = P.NEMA17_SHAFT_FLAT - P.NEMA17_SHAFT_D / 2 + 0.1
        shaft = shaft.cut(box(flat_x, 5, -5, 5, P.Z_CLAMP_BOT - 2, P.Z_DISC1_TOP + 2))
        s = s.cut(shaft)
        r_in = flat_x
        grub_angles = (0,)

    # radial M3 grub screws with captive nuts (nut slots open from below)
    for a in grub_angles:
        hole = cyl_dir(1.6, P.CLAMP_OD, (r_in - 0.5, 0, Z_GRUB), (1, 0, 0))
        nut_r0 = max(r_in + 1.2, 9.6)
        nut = box(nut_r0, nut_r0 + P.NUT_M3_T + 0.2, -(P.NUT_M3_AF + 0.3) / 2,
                  (P.NUT_M3_AF + 0.3) / 2, P.Z_CLAMP_BOT - 1, Z_GRUB + (P.NUT_M3_AF + 0.3) / 2)
        cutter = hole.fuse(nut)
        cutter.rotate(V(0, 0, 0), V(0, 0, 1), a)
        s = s.cut(cutter)

    # M4 heat-set inserts for the roller axles
    for a in ROLLER_ANGLES:
        x, y = polar(P.PITCH_R, a)
        s = s.cut(cyl(P.INSERT_M4_D / 2, P.Z_DISC1_TOP - P.INSERT_M4_L, P.Z_DISC1_TOP + 1, x, y))
    return s.removeSplitter()


def make_rotor_upper():
    s = cyl(P.DISC_R, P.Z_DISC2_BOT, P.Z_DISC2_TOP)
    for a in ROLLER_ANGLES:
        x, y = polar(P.PITCH_R, a)
        # boss that clamps only the inner race of the top bearing
        s = s.fuse(cyl(P.BRG_INNER_RACE_D / 2, P.Z_ROLLER_TOP, P.Z_DISC2_BOT + 0.01, x, y))
    for a in ROLLER_ANGLES:
        x, y = polar(P.PITCH_R, a)
        s = s.cut(cyl(P.HOLE_M4 / 2, P.Z_ROLLER_TOP - 1, P.Z_DISC2_TOP + 1, x, y))
        # 90 deg countersink for DIN 7991 M4 (head Ø8)
        r_top = 4.2
        h = r_top - P.HOLE_M4 / 2
        cone = App.Vector(x, y, P.Z_DISC2_TOP - h)
        import Part
        s = s.cut(Part.makeCone(P.HOLE_M4 / 2, r_top, h, cone, V(0, 0, 1)))
        s = s.cut(cyl(r_top, P.Z_DISC2_TOP, P.Z_DISC2_TOP + 1, x, y))
    if P.LID_BEARING:
        s = s.fuse(cyl(P.BRG_ID / 2 - 0.03, P.Z_DISC2_TOP, P.Z_LID + P.BRG_W - 0.5))
    return s.removeSplitter()


def make_axle_sleeve():
    """Printed Ø8 sleeve over the M4 bolt; flange spaces the bottom bearing's inner race."""
    length = P.SPACER_T + P.ROLLER_W - 0.2
    s = cyl(P.BRG_INNER_RACE_D / 2, 0, P.SPACER_T)
    s = s.fuse(cyl(P.AXLE_SLEEVE_OD / 2, 0, length))
    s = s.cut(cyl(P.HOLE_M4 / 2, -1, length + 1))
    return s.removeSplitter()


# --------------------------------------------------------------------------
# Lid: 4x M3, tabs press the hose into the saddles (stops hose creep)
# --------------------------------------------------------------------------
def make_lid():
    s = rounded_box(-H, H, -H, H, P.Z_LID, P.Z_TOP, P.HOUSING_CORNER_R)
    for x in (-P.LID_SCREW_XY, P.LID_SCREW_XY):
        for y in (-P.LID_SCREW_XY, P.LID_SCREW_XY):
            s = s.cut(cyl(P.HOLE_M3 / 2, P.Z_LID - 1, P.Z_TOP + 1, x, y))
    z_tab = P.Z_HOSE + P.HOSE_OD / 2 - P.HOSE_SQUEEZE
    w = P.HOSE_SADDLE_W / 2 - 0.1
    for sx in (-1, 1):
        x = sx * P.HOSE_X
        s = s.fuse(box(x - w, x + w, -H, -P.CAVITY_Y, z_tab, P.Z_LID + 0.01))
    if P.LID_BEARING:
        boss_h = P.BRG_W + 2
        s = s.fuse(cyl(P.BRG_OD / 2 + 4, P.Z_TOP - 0.01, P.Z_LID + boss_h))
        s = s.cut(cyl(P.BRG_OD / 2 + 0.05, P.Z_LID - 1, P.Z_LID + P.BRG_W))
        s = s.cut(cyl(P.BRG_ID / 2 + 1, P.Z_LID, P.Z_LID + boss_h + 1))
    return s.removeSplitter()


# --------------------------------------------------------------------------
# Reference (non-printed) parts for the assembly and interference checks
# --------------------------------------------------------------------------
def make_motor_dummy():
    m = rounded_box(-P.MOTOR_FRAME / 2, P.MOTOR_FRAME / 2, -P.MOTOR_FRAME / 2,
                    P.MOTOR_FRAME / 2, -P.MOTOR_LEN, 0, 3)
    m = m.fuse(cyl(11, 0, 2))                                      # pilot boss (assumed)
    m = m.fuse(cyl(2.5, 0, P.STOCK_HUB_Z0 + 0.01))                 # shaft stub
    m = m.fuse(cyl(P.STOCK_HUB_D / 2, P.STOCK_HUB_Z0, P.Z_HUB_TOP))  # hub
    return m.removeSplitter()


def make_bearings():
    parts = []
    for a in ROLLER_ANGLES:
        x, y = polar(P.PITCH_R, a)
        for k in range(P.ROLLER_STACK):
            z0 = P.Z_ROLLER_BOT + k * P.BRG_W
            parts.append(ring(P.BRG_OD / 2, P.BRG_ID / 2, z0, z0 + P.BRG_W, x, y))
    return parts


def make_hose_visual():
    """Uncompressed hose path (visual only — in reality the rollers flatten it)."""
    import Part
    rc = P.TRACK_R - P.HOSE_OD / 2
    torus = Part.makeTorus(rc, P.HOSE_OD / 2, V(0, 0, P.Z_HOSE), V(0, 0, 1))
    torus_in = Part.makeTorus(rc, P.HOSE_ID / 2, V(0, 0, P.Z_HOSE), V(0, 0, 1))
    half = box(-60, 60, 0, 60, 0, 60)
    s = torus.cut(torus_in).common(half)
    leg_len = H + 25
    for sx in (-1, 1):
        o = cyl_dir(P.HOSE_OD / 2, leg_len, (sx * rc, 0.01, P.Z_HOSE), (0, -1, 0))
        i = cyl_dir(P.HOSE_ID / 2, leg_len + 2, (sx * rc, 1, P.Z_HOSE), (0, -1, 0))
        s = s.fuse(o.cut(i))
    return s


def rotated(shape, deg):
    c = shape.copy()
    c.rotate(V(0, 0, 0), V(0, 0, 1), deg)
    return c


# --------------------------------------------------------------------------
def main():
    print("== Pump head ==")
    print(f"  z stack: floor {P.Z_FLOOR}  clamp {P.Z_CLAMP_BOT}  disc1 {P.Z_DISC1_BOT}-{P.Z_DISC1_TOP}"
          f"  rollers {P.Z_ROLLER_BOT}-{P.Z_ROLLER_TOP}  disc2 {P.Z_DISC2_BOT}-{P.Z_DISC2_TOP}"
          f"  hose {P.Z_HOSE:.1f}  lid {P.Z_LID}-{P.Z_TOP}")
    a_bore = math.pi * (P.HOSE_ID / 2) ** 2
    r_mid = P.ROLLER_R + P.OCCLUSION_GAP / 2
    v_th = a_bore * 2 * math.pi * r_mid / 1000
    print(f"  roller R {P.ROLLER_R}  track R {P.TRACK_R}  hose x {P.HOSE_X:.2f}"
          f"  V_rev theoretical {v_th:.2f} mL  (~{0.8 * v_th:.2f} mL at 80 %)")

    doc = App.newDocument("pump_assembly")
    base = make_base()
    inserts = {g: make_track_insert(g) for g in P.GAP_VARIANTS}
    rl = make_rotor_lower("stock")
    rl17 = make_rotor_lower("nema17")
    ru = make_rotor_upper()
    sleeve = make_axle_sleeve()
    lid = make_lid()

    parts = [("pump_base", base), ("pump_lid", lid), ("pump_rotor_lower_stock", rl),
             ("pump_rotor_lower_nema17", rl17), ("pump_rotor_upper", ru),
             ("pump_axle_sleeve", sleeve)]
    parts += [(f"pump_track_insert_gap{g:.1f}".replace(".", "p"), s) for g, s in inserts.items()]
    for name, s in parts:
        report(name, s)
        export_part(name, s, doc if "nema17" not in name and "gap" not in name
                    or name.endswith("2p6") else None)

    # --- assembly reference parts
    motor = make_motor_dummy()
    bearings = make_bearings()
    sleeves = []
    for a in ROLLER_ANGLES:
        x, y = polar(P.PITCH_R, a)
        sl = sleeve.copy()
        sl.translate(V(x, y, P.Z_DISC1_TOP))
        sleeves.append(sl)
    hose = make_hose_visual()
    for name, s in [("ref_motor", motor), ("ref_hose", hose)]:
        doc.addObject("Part::Feature", name).Shape = s
    for i, b in enumerate(bearings):
        doc.addObject("Part::Feature", f"ref_bearing_{i}").Shape = b
    for i, sl in enumerate(sleeves):
        doc.addObject("Part::Feature", f"ref_sleeve_{i}").Shape = sl
    save_doc(doc, "pump_assembly")

    # --- interference checks
    print("== Checks ==")
    ok = True
    rotor = fuse_all([rl, ru] + sleeves + bearings)
    for ang in (0, 37, 60):
        r = rotated(rotor, ang)
        ok &= check_no_overlap(f"rotor@{ang}deg vs base", r, base)
        ok &= check_no_overlap(f"rotor@{ang}deg vs lid", r, lid)
        for g, ins in inserts.items():
            ok &= check_no_overlap(f"rotor@{ang}deg vs insert gap {g}", r, ins)
    ok &= check_no_overlap("motor vs base", motor, base)
    ok &= check_no_overlap("motor vs rotor lower (stock)", motor, rl)
    for g, ins in inserts.items():
        ok &= check_no_overlap(f"insert gap {g} vs base", ins, base)
        ok &= check_no_overlap(f"insert gap {g} vs lid", ins, lid)
    ok &= check_no_overlap("lid vs base", lid, base)
    # NEMA 17 drop-in: 42.3 mm body under the 86 mm plate, same holes
    n17 = rounded_box(-P.NEMA17_FRAME / 2, P.NEMA17_FRAME / 2, -P.NEMA17_FRAME / 2,
                      P.NEMA17_FRAME / 2, -40, 0, 3).fuse(cyl(11, 0, 2)).fuse(
        cyl(P.NEMA17_SHAFT_D / 2, 0, P.NEMA17_SHAFT_L))
    ok &= check_no_overlap("NEMA17 dummy vs base", n17, base)
    # roller-to-track gap (analytic) and disc-to-track clearance
    for g in P.GAP_VARIANTS:
        print(f"  gap {g}: roller->track {g:.2f} mm, disc rim->track {P.ROLLER_R + g - P.DISC_R:.2f} mm")
    print(f"  ALL CHECKS {'PASSED' if ok else 'FAILED'}")

    # --- previews
    grey, blue, steel, red = (0.7, 0.72, 0.75), (0.35, 0.55, 0.85), (0.6, 0.6, 0.62), (0.9, 0.4, 0.3)
    orange = (0.95, 0.6, 0.2)
    ins = inserts[P.OCCLUSION_GAP]
    asm = [(base, grey, 1.0), (ins, orange, 1.0), (rl, blue, 1.0), (ru, blue, 0.35),
           (hose, red, 1.0), (motor, (0.3, 0.3, 0.32), 1.0)] + [(b, steel, 1.0) for b in bearings]
    render(os.path.join(OUT_DIR, "pump_assembly.png"), asm,
           title="Pump head (lid off, upper disc translucent)")
    render(os.path.join(OUT_DIR, "pump_top.png"),
           [(ins, orange, 1.0), (rl, blue, 1.0), (hose, red, 1.0)] + [(b, steel, 1.0) for b in bearings]
           + [(base, grey, 1.0)], elev=90, azim=-90, title="Top view: track, rollers, hose")
    exploded = []
    for i, (s, c) in enumerate([(motor, (0.3, 0.3, 0.32)), (base, grey), (ins, orange), (rl, blue),
                                (fuse_all(bearings + sleeves), steel), (ru, blue), (lid, grey)]):
        e = s.copy()
        e.translate(V(0, 0, i * 22))
        exploded.append((e, c, 1.0))
    render(os.path.join(OUT_DIR, "pump_exploded.png"), exploded, elev=18, azim=-60,
           title="Exploded: motor, base, track insert, lower rotor, rollers, upper rotor, lid",
           size=(8, 11))
    return ok


if __name__ == "__main__":
    main()
