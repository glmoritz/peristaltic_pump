"""Condensate reservoir — parametric FreeCAD model.

Run headless:  cad/build.sh reservoir
Outputs:       cad/out/reservoir_*.stl / .step, cad/out/reservoir_assembly.FCStd, previews

Layout (top view, origin at the box centre):
  +x wall  : H (high) level sensor, clip-mounted, reads through the 2 mm wall
  -y wall  : L (low) level sensor
  (+x,-y)  : pickup corner — hose comes down through the lid into a strainer cage
  lid      : AC drain inlet collar, pickup hose grip collar, vent
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import FreeCAD as App  # noqa: E402

import params as P  # noqa: E402
from cadlib import (V, OUT_DIR, box, cyl, cyl_dir, ring, rounded_box, polar,  # noqa: E402
                    fuse_all, export_part, save_doc, report, check_no_overlap, render)

X2, Y2, H = P.RES_X / 2, P.RES_Y / 2, P.RES_H
W, F = P.RES_WALL, P.RES_FLOOR
IX2, IY2 = X2 - W, Y2 - W                       # inner half sizes
BOSS_R = P.RES_SCREW_BOSS_D / 2
BOSS_XY = (IX2 - 3.0, IY2 - 3.0)                # corner screw bosses (inside)
TOP = H + P.RES_LID_T


def inner_cavity(z0, z1, shrink=0.0):
    return rounded_box(-IX2 + shrink, IX2 - shrink, -IY2 + shrink, IY2 - shrink, z0, z1,
                       max(P.RES_CORNER_R - W - shrink, 0.5))


def corner_xy():
    return [(sx * BOSS_XY[0], sy * BOSS_XY[1]) for sx in (-1, 1) for sy in (-1, 1)]


# --------------------------------------------------------------------------
# Sensor mounting, built on the -y wall then rotated onto the requested face
# --------------------------------------------------------------------------
def sensor_z_center(trip_above_floor):
    bottom = F + trip_above_floor - P.SENSOR_SENSE_FROM_BOTTOM
    return bottom + P.SENSOR_H / 2


def place_on_face(shape, face):
    side, _ = face
    s = shape.copy()
    if side == "+x":
        s.rotate(V(0, 0, 0), V(0, 0, 1), 90)
        s.translate(V(X2 - Y2, 0, 0))
    elif side != "-y":
        raise ValueError(side)
    return s


def sensor_bosses_local(u, zc):
    """Two bosses on the outside of the -y wall, each with a blind M3 heat-set hole."""
    h = P.SENSOR_T - 0.2                               # 0.2 mm preload on the sensor
    out = []
    for du in (-P.SENSOR_CLIP_SCREW_DX, P.SENSOR_CLIP_SCREW_DX):
        b = cyl_dir(4.0, h + 0.5, (u + du, -Y2 + 0.5, zc), (0, -1, 0))
        hole = cyl_dir(P.INSERT_M3_D / 2, P.INSERT_M3_L + 0.01, (u + du, -Y2 - h - 0.01, zc), (0, 1, 0))
        out.append((b, hole))
    return out


def sensor_dummy_local(u, zc):
    return box(u - P.SENSOR_W_EARS / 2, u + P.SENSOR_W_EARS / 2, -Y2 - P.SENSOR_T, -Y2,
               zc - P.SENSOR_H / 2, zc + P.SENSOR_H / 2)


def sensor_clip_local(u, zc):
    """Plate that presses the sensor against the wall; slotted for +-5 mm height adjust."""
    y_in = -Y2 - (P.SENSOR_T - 0.2)            # rests on the boss tips
    t = 2.5
    half_w = P.SENSOR_CLIP_SCREW_DX + 5.0
    s = box(u - half_w, u + half_w, y_in - t, y_in, zc - 15, zc + 15)
    # side ribs locate the sensor sideways
    for su in (-1, 1):
        x0 = u + su * (P.SENSOR_W_EARS / 2 + 0.3)
        x1 = x0 + su * 1.0
        s = s.fuse(box(min(x0, x1), max(x0, x1), y_in, y_in + 3.5, zc - 10, zc + 10))
    # stadium slots
    r = P.HOLE_M3 / 2
    half_slot = P.SENSOR_CLIP_SLOT / 2
    for du in (-P.SENSOR_CLIP_SCREW_DX, P.SENSOR_CLIP_SCREW_DX):
        x = u + du
        slot = box(x - r, x + r, y_in - t - 1, y_in + 1, zc - half_slot, zc + half_slot)
        slot = slot.fuse(cyl_dir(r, t + 2, (x, y_in + 1, zc - half_slot), (0, -1, 0)))
        slot = slot.fuse(cyl_dir(r, t + 2, (x, y_in + 1, zc + half_slot), (0, -1, 0)))
        s = s.cut(slot)
    # cable notch at the bottom centre
    s = s.cut(box(u - 3, u + 3, y_in - t - 1, y_in + 1, zc - 16, zc - 11))
    return s.removeSplitter()


SENSORS = [("L", P.SENSOR_L_FACE, P.SENSOR_L_TRIP_Z), ("H", P.SENSOR_H_FACE, P.SENSOR_H_TRIP_Z)]


# --------------------------------------------------------------------------
def make_body():
    s = rounded_box(-X2, X2, -Y2, Y2, 0, H, P.RES_CORNER_R)
    s = s.cut(inner_cavity(F, H + 1))
    for x, y in corner_xy():
        s = s.fuse(cyl(BOSS_R, F - 0.01, H, x, y))
    for x, y in corner_xy():
        s = s.cut(cyl(P.INSERT_M3_D / 2, H - P.INSERT_M3_L, H + 1, x, y))
    # locator ring for the strainer cage
    px, py = P.PICKUP_XY
    s = s.fuse(ring(P.STRAINER_OD / 2 + 1.5, P.STRAINER_OD / 2 + 0.3, F - 0.01, F + 1.5, px, py))
    # sensor clip bosses
    for _, face, trip in SENSORS:
        zc = sensor_z_center(trip)
        for b, hole in sensor_bosses_local(face[1], zc):
            s = s.fuse(place_on_face(b, face))
            s = s.cut(place_on_face(hole, face))
    return s.removeSplitter()


def make_lid():
    s = rounded_box(-X2, X2, -Y2, Y2, H, TOP, P.RES_CORNER_R)
    if P.RES_LID_LIP_H > 0:
        c = 0.3
        lip = inner_cavity(H - P.RES_LID_LIP_H, H + 0.01, c).cut(
            inner_cavity(H - P.RES_LID_LIP_H - 1, H + 1, c + 1.6))
        for x, y in corner_xy():
            lip = lip.cut(cyl(BOSS_R + 0.4, H - 5, H + 1, x, y))
        s = s.fuse(lip)
    for x, y in corner_xy():
        s = s.cut(cyl(P.HOLE_M3 / 2, H - 1, TOP + 1, x, y))
    # AC drain inlet collar (the AC hose just drops in) [TBC Q14]
    ax, ay = P.AC_INLET_XY
    s = s.fuse(ring(P.AC_INLET_D / 2 + 2, P.AC_INLET_D / 2, TOP - 0.01, TOP + P.AC_INLET_COLLAR_H, ax, ay))
    s = s.cut(cyl(P.AC_INLET_D / 2, H - 1, TOP + P.AC_INLET_COLLAR_H + 1, ax, ay))
    # pickup hose grip collar (Ø6.8 grips the 7 mm hose)
    px, py = P.PICKUP_XY
    s = s.fuse(ring(6.0, 3.4, TOP - 0.01, TOP + 6, px, py))
    s = s.cut(cyl(3.4, H - 1, TOP + 7, px, py))
    s = s.cut(cyl(P.VENT_D / 2, H - 1, TOP + 1, 0, IY2 - 8))
    return s.removeSplitter()


def make_strainer():
    """Cage at the bottom of the pickup hose: hose plugs into the top, water enters via slots."""
    r = P.STRAINER_OD / 2
    h = P.STRAINER_H
    socket_depth = 6.0
    s = cyl(r, 0, h)
    s = s.cut(cyl(P.HOSE_OD / 2, h - socket_depth, h + 1))           # hose socket
    s = s.cut(cyl(2.5, 1.2, h - socket_depth + 0.01))                 # bore, hose stops on its rim
    for k in range(6):            # 6 slots: 8 would meet at the centre and cut off the floor
        sl = box(2.0, r + 1, -0.75, 0.75, -1, h - socket_depth - 1.5)
        sl.rotate(V(0, 0, 0), V(0, 0, 1), k * 60)
        s = s.cut(sl)
    return s.removeSplitter()


def water_volume(body, z0, z1):
    cav = inner_cavity(z0, z1)
    return cav.cut(body).Volume / 1000.0          # mL


# --------------------------------------------------------------------------
def main():
    print("== Reservoir ==")
    doc = App.newDocument("reservoir_assembly")
    body = make_body()
    lid = make_lid()
    strainer = make_strainer()
    clips = {}
    sensors = {}
    for name, face, trip in SENSORS:
        zc = sensor_z_center(trip)
        clips[name] = place_on_face(sensor_clip_local(face[1], zc), face)
        sensors[name] = place_on_face(sensor_dummy_local(face[1], zc), face)
    clip_print = sensor_clip_local(0, 0)

    for name, s in [("reservoir_body", body), ("reservoir_lid", lid),
                    ("reservoir_strainer", strainer), ("reservoir_sensor_clip", clip_print)]:
        report(name, s)
        export_part(name, s, None)

    strainer_placed = strainer.copy()
    strainer_placed.translate(V(P.PICKUP_XY[0], P.PICKUP_XY[1], F))
    px, py = P.PICKUP_XY
    hose = cyl(P.HOSE_OD / 2, F + P.STRAINER_H - 6, TOP + 30, px, py).cut(
        cyl(P.HOSE_ID / 2, F, TOP + 31, px, py))
    for n, s in [("reservoir_body", body), ("reservoir_lid", lid), ("strainer", strainer_placed),
                 ("sensor_clip_L", clips["L"]), ("sensor_clip_H", clips["H"]),
                 ("ref_sensor_L", sensors["L"]), ("ref_sensor_H", sensors["H"]), ("ref_hose", hose)]:
        doc.addObject("Part::Feature", n).Shape = s
    save_doc(doc, "reservoir_assembly")

    print("== Checks ==")
    ok = True
    ok &= check_no_overlap("lid vs body", lid, body)
    ok &= check_no_overlap("strainer vs body", strainer_placed, body)
    for n in ("L", "H"):
        ok &= check_no_overlap(f"sensor {n} vs body (must touch, not overlap)", sensors[n], body)
        ok &= check_no_overlap(f"clip {n} vs body", clips[n], body)
        v = clips[n].common(sensors[n]).Volume
        print(f"  [info] clip {n} preload on sensor: {v:.1f} mm3 overlap (intended 0.2 mm squeeze)")
    # sensing face must be flat thin wall: nothing inside within the sensor footprint
    print(f"  ALL CHECKS {'PASSED' if ok else 'FAILED'}")

    print("== Levels (inside floor = 0) ==")
    zL, zH = F + P.SENSOR_L_TRIP_Z, F + P.SENSOR_H_TRIP_Z
    v_low = water_volume(body, F, zL)
    v_lh = water_volume(body, zL, zH)
    v_head = water_volume(body, zH, H - P.RES_LID_LIP_H)
    print(f"  L trip {P.SENSOR_L_TRIP_Z} mm, H trip {P.SENSOR_H_TRIP_Z} mm, rim {H - F:.1f} mm")
    print(f"  below L (always wet): {v_low:5.1f} mL")
    print(f"  L -> H (measuring cup): {v_lh:5.1f} mL")
    print(f"  H -> lid underside (overflow margin): {v_head:5.1f} mL"
          f"  = {v_head / 1500 * 60:.1f} min at 1.5 L/h with a dead pump")
    for q in (0.5, 1.0, 1.5):
        print(f"  at {q} L/h inflow and +-20 % pump offset: half-cycle "
              f"{v_lh / (0.2 * q * 1000) * 60:.0f} min")

    # --- previews
    grey, blue, dark, red, green = (0.55, 0.57, 0.6), (0.35, 0.55, 0.85), (0.25, 0.25, 0.28), \
        (0.9, 0.4, 0.3), (0.3, 0.75, 0.45)
    lvl_L = inner_cavity(zL, zL + 0.3, 0.05)
    lvl_H = inner_cavity(zH, zH + 0.3, 0.05)
    items = [(body, grey, 1.0), (strainer_placed, green, 1.0), (hose, red, 1.0),
             (clips["L"], blue, 1.0), (clips["H"], blue, 1.0),
             (sensors["L"], dark, 1.0), (sensors["H"], dark, 1.0)]
    render(os.path.join(OUT_DIR, "reservoir_assembly.png"), items + [(lid, grey, 0.25)],
           elev=24, azim=-52, title="Reservoir (lid translucent)")
    # section through the pickup, looking from -y
    keep = box(-60, 60, py, 60, -5, 80)
    sec = [(s.common(keep), c, a) for s, c, a in items + [(lid, grey, 1.0)]]
    sec += [(lvl_L.common(keep), (0.2, 0.5, 1.0), 0.45), (lvl_H.common(keep), (1.0, 0.3, 0.3), 0.45)]
    render(os.path.join(OUT_DIR, "reservoir_section.png"), sec, elev=8, azim=-90,
           title="Section at the pickup: blue = L trip, red = H trip")
    return ok


if __name__ == "__main__":
    main()
