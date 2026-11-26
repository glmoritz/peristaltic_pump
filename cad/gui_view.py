"""Give the generated assemblies colours and a saved camera.

freecadcmd saves documents without GUI data (no colours, no camera), so they
open in FreeCAD looking at nothing. This script runs inside the FreeCAD *GUI*
(off-screen, launched by build.sh), styles every object, sets an isometric
fit-all camera and saves the document again.
"""
import os
import traceback

import FreeCAD as App
import FreeCADGui as Gui

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
# In GUI mode print() goes to the report view, so build.sh reads this log instead.
LOG = os.path.join(OUT, ".gui_view.log")


def log(msg):
    with open(LOG, "a") as f:
        f.write(msg + "\n")

# (name prefix, RGB 0..1, transparency %)  — first match wins
STYLE = [
    ("pump_lid", (0.75, 0.77, 0.80), 75),
    ("reservoir_lid", (0.30, 0.30, 0.33), 75),
    ("pump_base", (0.75, 0.77, 0.80), 0),
    ("pump_track_insert", (0.95, 0.60, 0.20), 0),
    ("pump_rotor", (0.35, 0.55, 0.85), 0),
    ("pump_axle_sleeve", (0.95, 0.95, 0.95), 0),
    ("reservoir_body", (0.30, 0.30, 0.33), 35),
    ("strainer", (0.30, 0.75, 0.45), 0),
    ("sensor_clip", (0.35, 0.55, 0.85), 0),
    ("ref_sensor", (0.10, 0.10, 0.10), 0),
    ("ref_motor", (0.20, 0.20, 0.22), 0),
    ("ref_bearing", (0.65, 0.65, 0.68), 0),
    ("ref_sleeve", (0.95, 0.95, 0.95), 0),
    ("ref_hose", (0.90, 0.35, 0.25), 0),
]


def style(doc):
    for obj in doc.Objects:
        vo = obj.ViewObject
        if vo is None:
            continue
        for prefix, rgb, transp in STYLE:
            if obj.Name.startswith(prefix):
                vo.ShapeColor = rgb
                vo.Transparency = transp
                break
        vo.Visibility = True


def main():
    for name in ("pump_assembly", "reservoir_assembly"):
        path = os.path.join(OUT, name + ".FCStd")
        if not os.path.exists(path):
            continue
        doc = App.openDocument(path)
        Gui.updateGui()
        style(doc)
        view = Gui.getDocument(doc.Name).activeView()
        view.setCameraType("Perspective")
        view.viewIsometric()
        view.fitAll()
        Gui.updateGui()
        try:   # FreeCAD's own render of the saved camera, to verify what opens
            view.saveImage(os.path.join(OUT, name + "_freecad.png"), 1200, 900, "White")
        except Exception as e:  # off-screen GL may be unavailable
            log(f"  (no screenshot: {e})")
        doc.save()
        App.closeDocument(doc.Name)
        log(f"  styled + camera saved -> cad/out/{name}.FCStd")


try:
    main()
except Exception:
    log(traceback.format_exc())
finally:
    os._exit(0)
