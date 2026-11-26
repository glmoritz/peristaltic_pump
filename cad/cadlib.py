"""Small helpers shared by the part scripts (run inside FreeCAD's Python)."""
import math
import os

import FreeCAD as App
import Part
import MeshPart

V = App.Vector
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")


# --------------------------------------------------------------------------
# primitives
# --------------------------------------------------------------------------
def box(x0, x1, y0, y1, z0, z1):
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0))


def cyl(r, z0, z1, x=0.0, y=0.0):
    return Part.makeCylinder(r, z1 - z0, V(x, y, z0), V(0, 0, 1))


def cyl_dir(r, length, base, direction):
    return Part.makeCylinder(r, length, V(*base), V(*direction))


def ring(r_out, r_in, z0, z1, x=0.0, y=0.0):
    return cyl(r_out, z0, z1, x, y).cut(cyl(r_in, z0 - 1, z1 + 1, x, y))


def rounded_box(x0, x1, y0, y1, z0, z1, r):
    """Box with its vertical edges filleted."""
    b = box(x0, x1, y0, y1, z0, z1)
    if r <= 0:
        return b
    edges = [e for e in b.Edges
             if abs(e.Vertexes[0].Point.x - e.Vertexes[1].Point.x) < 1e-6
             and abs(e.Vertexes[0].Point.y - e.Vertexes[1].Point.y) < 1e-6]
    return b.makeFillet(r, edges)


def hexagon_prism(af, z0, z1, x=0.0, y=0.0, rot_deg=0.0):
    """Hexagonal prism with across-flats `af`."""
    rc = af / math.sqrt(3)
    pts = []
    for i in range(7):
        a = math.radians(rot_deg + 60 * i)
        pts.append(V(x + rc * math.cos(a), y + rc * math.sin(a), z0))
    face = Part.Face(Part.makePolygon(pts))
    return face.extrude(V(0, 0, z1 - z0))


def polar(r, deg):
    a = math.radians(deg)
    return r * math.cos(a), r * math.sin(a)


def fuse_all(shapes):
    s = shapes[0]
    for o in shapes[1:]:
        s = s.fuse(o)
    return s.removeSplitter()


# --------------------------------------------------------------------------
# export
# --------------------------------------------------------------------------
def export_part(name, shape, doc=None):
    os.makedirs(OUT_DIR, exist_ok=True)
    stl = os.path.join(OUT_DIR, name + ".stl")
    step = os.path.join(OUT_DIR, name + ".step")
    mesh = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.03,
                                  AngularDeflection=0.15, Relative=False)
    mesh.write(stl)
    shape.exportStep(step)
    if doc is not None:
        obj = doc.addObject("Part::Feature", name)
        obj.Shape = shape
    return stl


def save_doc(doc, name):
    os.makedirs(OUT_DIR, exist_ok=True)
    doc.recompute()
    doc.saveAs(os.path.join(OUT_DIR, name + ".FCStd"))


def report(name, shape):
    bb = shape.BoundBox
    vol_cm3 = shape.Volume / 1000.0
    print(f"  {name:28s} {bb.XLength:6.1f} x {bb.YLength:6.1f} x {bb.ZLength:6.1f} mm"
          f"   {vol_cm3:6.1f} cm3  ~{vol_cm3 * 1.27:5.0f} g PETG solid"
          f"   valid={shape.isValid()} solids={len(shape.Solids)}")


def check_no_overlap(label, a, b, tol=1e-3):
    v = a.common(b).Volume
    ok = v < tol
    print(f"  [{'OK ' if ok else 'FAIL'}] {label}: overlap {v:.3f} mm3")
    return ok


# --------------------------------------------------------------------------
# preview rendering (matplotlib, no GUI needed)
# --------------------------------------------------------------------------
def render(path, items, elev=28, azim=-58, title=None, size=(9, 8), deflection=0.4):
    """items: list of (shape, rgb tuple, alpha)."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    import numpy as np

    fig = plt.figure(figsize=size, dpi=110)
    ax = fig.add_subplot(111, projection="3d")
    light = np.array([0.4, -0.6, 0.85])
    light /= np.linalg.norm(light)
    lo = np.array([1e9] * 3)
    hi = -lo
    all_tri, all_col = [], []
    for shape, rgb, alpha in items:
        pts, tris = shape.tessellate(deflection)
        if not tris:
            continue
        P = np.array([[p.x, p.y, p.z] for p in pts])
        T = np.array(tris)
        tri = P[T]
        n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        nn = np.linalg.norm(n, axis=1)
        nn[nn == 0] = 1
        n /= nn[:, None]
        shade = 0.35 + 0.65 * np.abs(n @ light)
        cols = np.clip(np.array(rgb)[None, :] * shade[:, None], 0, 1)
        cols = np.hstack([cols, np.full((len(cols), 1), alpha)])
        all_tri.append(tri)
        all_col.append(cols)
        lo = np.minimum(lo, P.min(axis=0))
        hi = np.maximum(hi, P.max(axis=0))
    # one collection so matplotlib depth-sorts triangles across all parts
    pc = Poly3DCollection(np.concatenate(all_tri), facecolors=np.concatenate(all_col),
                          edgecolors="none")
    ax.add_collection3d(pc)
    ctr = (lo + hi) / 2
    span = (hi - lo).max() / 2
    ax.set_xlim(ctr[0] - span, ctr[0] + span)
    ax.set_ylim(ctr[1] - span, ctr[1] + span)
    ax.set_zlim(ctr[2] - span, ctr[2] + span)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=elev, azim=azim)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_zlabel("z")
    if title:
        ax.set_title(title)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)
    print(f"  preview -> {os.path.relpath(path)}")


# --------------------------------------------------------------------------
# fastener stand-ins (assembly instructions only)
# --------------------------------------------------------------------------
def screw(base, direction, d, length, head_d=None, head_h=None, countersunk=False):
    """Screw whose head sits at `base` and whose shank runs along `direction`."""
    dv = V(*direction).normalize()
    b = V(*base)
    head_d = head_d or 1.8 * d
    head_h = head_h or (0.6 * d if countersunk else 0.9 * d)
    shank = Part.makeCylinder(d / 2, length, b, dv)
    if countersunk:
        head = Part.makeCone(head_d / 2, d / 2, head_h, b - dv * 0.0, dv)
    else:
        head = Part.makeCylinder(head_d / 2, head_h, b - dv * head_h, dv)
    return shank.fuse(head)


def grub(base, direction, d=3.0, length=8.0):
    return Part.makeCylinder(d / 2, length, V(*base), V(*direction).normalize())


def heat_insert(top, direction, d, length):
    """Knurl-ish insert: top face at `top`, body runs along `direction` into the part."""
    dv = V(*direction).normalize()
    body = Part.makeCylinder(d / 2, length, V(*top), dv)
    bore = Part.makeCylinder(d / 2 - 0.7, length + 2, V(*top) - dv, dv)
    return body.cut(bore)


def nut(center, af=5.5, t=2.4):
    return hexagon_prism(af, center[2] - t / 2, center[2] + t / 2, center[0], center[1]).cut(
        Part.makeCylinder(1.5, t + 2, V(center[0], center[1], center[2] - t / 2 - 1), V(0, 0, 1)))


def arrow(start, end, r=0.9, head_r=2.6, head_l=6.0):
    """Solid arrow from start to end (for exploded-view instructions)."""
    s, e = V(*start), V(*end)
    d = e - s
    L = d.Length
    if L < head_l + 2:
        return None
    u = V(d).normalize()
    shaft = Part.makeCylinder(r, L - head_l, s, u)
    head = Part.makeCone(head_r, 0.01, head_l, s + u * (L - head_l), u)
    return shaft.fuse(head)
