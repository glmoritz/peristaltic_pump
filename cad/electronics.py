"""Electronics housing for the perfboard — bolts to the pump head's +y (back) wall.

Run headless:  cad/build.sh electronics
Outputs:       cad/out/electronics_*.stl / .step, electronics_assembly.FCStd, preview

Pump coordinates are used throughout. The box's back face touches the pump's
+y outer face (y = HOUSING_HALF). The board stands vertically (u along x, v
along z) with its components facing +y, towards the removable lid. All openings
are on the bottom (cables) and the +x side (USB-C), never on top, so drips
can't get in.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import FreeCAD as App  # noqa: E402

import params as P  # noqa: E402
from cadlib import (V, OUT_DIR, box, cyl, cyl_dir, rounded_box, fuse_all,  # noqa: E402
                    export_part, save_doc, report, check_no_overlap, render)

# --- derived geometry -------------------------------------------------------
Y_BACK = P.HOUSING_HALF                         # box back face = pump back face
Y_IN = Y_BACK + P.BOX_BACK                      # inner face of the back wall
Y_BOARD = Y_IN + P.STUD_H                       # board rear face
Y_BOARD_F = Y_BOARD + P.PERF_T                  # board component face
Y_LID = Y_BOARD_F + P.PERF_COMP_MAX_H + 2.0     # lid inner face
Y_FRONT = Y_LID + P.BOX_LID_T
XI = P.PERF_W / 2 + P.BOX_SIDE_GAP              # inner half-width
XO = XI + P.BOX_WALL
Z_TOP = P.Z_TOP                                 # flush with the pump lid top
Z_BOT = Z_TOP - (P.PERF_H + 2 * P.BOX_CLR + 2 * P.BOX_WALL)
ZI0, ZI1 = Z_BOT + P.BOX_WALL, Z_TOP - P.BOX_WALL
Z_BOARD0 = ZI0 + P.BOX_CLR                      # board bottom edge (v = 0)
PILLAR_X = XI - 3.5                             # lid-screw pillar centre


def uv(u, v):
    """Board (u, v) -> pump (x, z)."""
    return u - P.PERF_W / 2, Z_BOARD0 + v


def board_holes():
    i = P.PERF_HOLE_INSET
    return [uv(u, v) for u in (i, P.PERF_W - i) for v in (i, P.PERF_H - i)]


def part_center_u(name):
    u0, v0, w, h, _, _ = P.PCB_PARTS[name]
    return u0 + w / 2, v0 + h / 2


# --------------------------------------------------------------------------
def make_box():
    s = rounded_box(-XO, XO, Y_BACK, Y_LID, Z_BOT, Z_TOP, 0)      # y-extent box
    s = s.cut(box(-XI, XI, Y_IN, Y_LID + 1, ZI0, ZI1))
    # soften the 4 front-to-back edges
    # lid-screw pillars in the side gaps (clear of the board, which slides in from the front)
    for sx in (-1, 1):
        for z in P.BOX_LID_SCREW_Z:
            x0, x1 = sorted((sx * (PILLAR_X - 3.5), sx * XI))
            pil = box(x0, x1, Y_IN - 0.01, Y_LID, z - 3.5, z + 3.5)
            pil = pil.fuse(cyl_dir(3.5, Y_LID - Y_IN, (sx * PILLAR_X, Y_IN, z), (0, 1, 0)))
            s = s.fuse(pil)
            s = s.cut(cyl_dir(P.INSERT_M3_D / 2, P.INSERT_M3_L + 0.01,
                              (sx * PILLAR_X, Y_LID + 0.01, z), (0, -1, 0)))
    # board studs
    for x, z in board_holes():
        s = s.fuse(cyl_dir(P.STUD_D / 2, P.STUD_H + 0.01, (x, Y_IN - 0.01, z), (0, 1, 0)))
        s = s.cut(cyl_dir(P.STUD_PILOT / 2, P.STUD_H + 2, (x, Y_BOARD + 0.01, z), (0, -1, 0)))
    # mounting holes to the pump's +y wall (screw heads sit behind the board)
    for sx in (-1, 1):
        s = s.cut(cyl_dir(P.HOLE_M3 / 2, P.BOX_BACK + 2, (sx * P.BOX_MOUNT_X, Y_BACK - 1,
                                                           P.BOX_MOUNT_Z), (0, 1, 0)))
    # cable holes in the bottom wall, under each connector
    y_cable = Y_BOARD_F + 6.0
    for name, d in (("J1", 6.5), ("J3", 5.0), ("J2", 6.0), ("J4", 5.0)):
        u, _ = part_center_u(name)
        x, _ = uv(u, 0)
        s = s.cut(cyl(d / 2, Z_BOT - 1, ZI0 + 1, x, y_cable))
    # USB-C opening in the +x wall, in front of the ESP32's port
    _, vc = part_center_u("U1")
    _, zc = uv(0, vc)
    y_usb = Y_BOARD_F + 9.0
    s = s.cut(box(XI - 1, XO + 1, y_usb - 4.5, y_usb + 4.5, zc - 6.5, zc + 6.5))
    return s.removeSplitter()


def make_lid():
    s = box(-XO, XO, Y_LID, Y_FRONT, Z_BOT, Z_TOP)
    for sx in (-1, 1):
        for z in P.BOX_LID_SCREW_Z:
            s = s.cut(cyl_dir(P.HOLE_M3 / 2, 10, (sx * PILLAR_X, Y_LID - 1, z), (0, 1, 0)))
    # vent slots in front of the TMC2209 (front face: drips run past, not in)
    u, v = part_center_u("U3")
    x, z = uv(u, v)
    for k in range(-3, 4):
        s = s.cut(box(x - 8, x + 8, Y_LID - 1, Y_FRONT + 1, z + k * 3.5 - 0.9, z + k * 3.5 + 0.9))
    return s.removeSplitter()


def make_board_dummy():
    """Perfboard with block stand-ins for the components (reference only)."""
    x0, z0 = uv(0, 0)
    pb = box(x0, x0 + P.PERF_W, Y_BOARD, Y_BOARD_F, z0, z0 + P.PERF_H)
    for x, z in board_holes():
        pb = pb.cut(cyl_dir(P.PERF_HOLE_D / 2, 5, (x, Y_BOARD - 1, z), (0, 1, 0)))
    comps = {}
    for name, (u0, v0, w, h, ht, _) in P.PCB_PARTS.items():
        bx, bz = uv(u0, v0)
        if name in ("C1", "BZ1"):
            r = min(w, h) / 2
            c = cyl_dir(r, ht, (bx + w / 2, Y_BOARD_F, bz + h / 2), (0, 1, 0))
        else:
            c = box(bx, bx + w, Y_BOARD_F, Y_BOARD_F + ht, bz, bz + h)
        comps[name] = c
    return pb, comps


def make_mount_screws():
    """M3 x 8 screws from inside the box into the pump's inserts (reference)."""
    out = []
    for sx in (-1, 1):
        sh = cyl_dir(1.5, 8, (sx * P.BOX_MOUNT_X, Y_IN, P.BOX_MOUNT_Z), (0, -1, 0))
        hd = cyl_dir(2.75, 3, (sx * P.BOX_MOUNT_X, Y_IN, P.BOX_MOUNT_Z), (0, 1, 0))
        out.append(sh.fuse(hd))
    return out


# --------------------------------------------------------------------------
def main():
    import pump
    print("== Electronics housing ==")
    print(f"  box outer {2 * XO:.1f} (x) x {Y_FRONT - Y_BACK:.1f} (y, depth) x {Z_TOP - Z_BOT:.1f} (z) mm;"
          f" z {Z_BOT:.1f}..{Z_TOP:.1f} (pump top {P.Z_TOP:.1f}, motor bottom {-P.MOTOR_LEN:.1f})")
    doc = App.newDocument("electronics_assembly")
    bx = make_box()
    lid = make_lid()
    board, comps = make_board_dummy()
    screws = make_mount_screws()
    for name, s in [("electronics_box", bx), ("electronics_lid", lid)]:
        report(name, s)
        export_part(name, s, doc)
    pbase = pump.make_base()
    plid = pump.make_lid()
    motor = pump.make_motor_dummy()
    for n, s in [("ref_pump_base", pbase), ("ref_pump_lid", plid), ("ref_motor", motor),
                 ("ref_perfboard", board)] + [(f"ref_comp_{k}", c) for k, c in comps.items()] \
            + [(f"ref_screw_{i}", s) for i, s in enumerate(screws)]:
        doc.addObject("Part::Feature", n).Shape = s
    save_doc(doc, "electronics_assembly")

    print("== Checks ==")
    ok = True
    allc = fuse_all(list(comps.values()))
    ok &= check_no_overlap("box vs pump base", bx, pbase)
    ok &= check_no_overlap("box vs pump lid", bx, plid)
    ok &= check_no_overlap("box vs motor", bx, motor)
    ok &= check_no_overlap("lid vs pump lid", lid, plid)
    ok &= check_no_overlap("box vs perfboard", bx, board)
    ok &= check_no_overlap("box vs components", bx, allc)
    ok &= check_no_overlap("lid vs components", lid, allc)
    ok &= check_no_overlap("lid vs box", lid, bx)
    ok &= check_no_overlap("mount screws vs perfboard", fuse_all(screws), board)
    # the board must slide in from the front past the lid-screw pillars
    sweep = box(-P.PERF_W / 2, P.PERF_W / 2, Y_BOARD, Y_LID + 5, Z_BOARD0, Z_BOARD0 + P.PERF_H)
    ok &= check_no_overlap("board insertion path vs box", sweep, bx)
    print(f"  ALL CHECKS {'PASSED' if ok else 'FAILED'}")

    grey, blue, green, dark = (0.75, 0.77, 0.8), (0.35, 0.55, 0.85), (0.25, 0.6, 0.35), (0.2, 0.2, 0.22)
    items = [(pbase, grey, 1.0), (plid, grey, 1.0), (motor, dark, 1.0), (bx, blue, 1.0),
             (board, green, 1.0)] + [(c, (0.15, 0.15, 0.18), 1.0) for c in comps.values()]
    render(os.path.join(OUT_DIR, "electronics_assembly.png"), items, elev=22, azim=60,
           title="Electronics box on the pump's back wall (lid off)")
    return ok
