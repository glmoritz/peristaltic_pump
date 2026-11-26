"""Helpers that need the FreeCAD GUI (used by gui_view.py and instructions.py)."""
import FreeCAD as App
import FreeCADGui as Gui

ISO = (-1.0, 1.0, -0.9)          # look direction: from +x, -y, +z (front of the pump)
BACK = (-0.7, -1.0, -0.55)       # from behind the pump (+y), where the electronics box is


def set_view(view, look_dir, up=(0, 0, 1)):
    """Point the camera along look_dir with world +z up (no animation involved)."""
    from pivy import coin
    d = App.Vector(*look_dir).normalize()
    z_cam = -d                                         # camera looks down its -z
    y_cam = App.Vector(*up) - z_cam * App.Vector(*up).dot(z_cam)
    y_cam.normalize()
    x_cam = y_cam.cross(z_cam)
    m = App.Matrix(x_cam.x, y_cam.x, z_cam.x, 0,
                   x_cam.y, y_cam.y, z_cam.y, 0,
                   x_cam.z, y_cam.z, z_cam.z, 0,
                   0, 0, 0, 1)
    q = App.Rotation(m).Q
    view.getCameraNode().orientation.setValue(coin.SbRotation(q[0], q[1], q[2], q[3]))


def render_scene(path, items, look=ISO, size=(1400, 1000), ortho=True):
    """items: (shape, rgb, transparency%) -> PNG rendered by FreeCAD itself."""
    doc = App.newDocument("scene")
    objs = []
    for i, (shape, rgb, transp) in enumerate(items):
        o = doc.addObject("Part::Feature", f"o{i}")
        o.Shape = shape
        objs.append((o, rgb, transp))
    doc.recompute()
    Gui.updateGui()
    for o, rgb, transp in objs:
        vo = o.ViewObject
        vo.ShapeColor = rgb
        vo.Transparency = transp
        vo.LineWidth = 1.5
    view = Gui.getDocument(doc.Name).activeView()
    view.setCameraType("Orthographic" if ortho else "Perspective")
    set_view(view, look)
    view.fitAll()
    Gui.updateGui()
    view.saveImage(path, size[0], size[1], "White")
    App.closeDocument(doc.Name)
