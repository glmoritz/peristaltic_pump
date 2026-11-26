#!/usr/bin/env python3
"""Draw the perfboard placement plan from params.PCB_PARTS (plain Python + matplotlib).

Output: docs/electronics/pcb_layout.png (component side, as installed: bottom
edge faces down, USB-C points right). The CAD housing uses the same table, so
the drawing, the box openings and the studs always agree.
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Rectangle, Circle, FancyBboxPatch  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import params as P  # noqa: E402

OUT = os.path.join(os.path.dirname(HERE), "docs", "electronics")
PITCH = 2.54

COLORS = {"U1": "#2b2b30", "U2": "#1f4aa8", "U3": "#8a2a8a", "C1": "#222222", "BZ1": "#333333",
          "J1": "#3c8a3c", "J2": "#e8e4d8", "J3": "#e8e4d8", "J4": "#e8e4d8"}

# header pins (u, v, label) — [TBC] verify against each module's silkscreen
ESP_TOP = ["5V", "GND", "3V3", "IO4", "IO3", "IO2", "IO1", "IO0"]        # from the USB end
ESP_BOT = ["IO5", "IO6", "IO7", "IO8", "IO9", "IO10", "IO20", "IO21"]
TMC_PWR = ["VM", "GND", "2B", "2A", "1A", "1B", "VDD", "GND"]           # left row, top -> bottom
TMC_LOG = ["EN", "MS1", "MS2", "PDN", "UART", "CLK", "STEP", "DIR"]     # right row, top -> bottom
CONN = {"J1": ["+19V", "GND"], "J3": ["5V", "GND", "OUT"], "J2": ["1A", "1B", "2A", "2B"],
        "J4": ["5V", "GND", "OUT"]}
CONN_PITCH = {"J1": 5.08, "J2": 2.5, "J3": 2.5, "J4": 2.5}


def pins():
    out = []
    u0, v0, w, h, _, _ = P.PCB_PARTS["U1"]
    rows_v = (v0 + (h - 15.24) / 2 + 15.24, v0 + (h - 15.24) / 2)
    for row, names in zip(rows_v, (ESP_TOP, ESP_BOT)):
        for k, n in enumerate(names):
            out.append((u0 + w - 3.5 - k * PITCH, row, n, "esp"))
    u0, v0, w, h, _, _ = P.PCB_PARTS["U3"]
    cols_u = (u0 + (w - 12.7) / 2, u0 + (w - 12.7) / 2 + 12.7)
    vtop = v0 + (h - 17.78) / 2 + 17.78
    for col, names in zip(cols_u, (TMC_PWR, TMC_LOG)):
        for k, n in enumerate(names):
            out.append((col, vtop - k * PITCH, n, "tmc"))
    for j, names in CONN.items():
        u0, v0, w, h, _, _ = P.PCB_PARTS[j]
        p = CONN_PITCH[j]
        start = u0 + (w - (len(names) - 1) * p) / 2
        for k, n in enumerate(names):
            out.append((start + k * p, v0 + h / 2, n, "conn"))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    fig, ax = plt.subplots(figsize=(15, 11.5), dpi=130)
    W, H = P.PERF_W, P.PERF_H
    ax.add_patch(FancyBboxPatch((0, 0), W, H, boxstyle="round,pad=0,rounding_size=1.0",
                                fc="#e9d9a6", ec="#8a7a4a", lw=2))
    # 2.54 mm hole grid
    n_u = int((W - 2) / PITCH)
    n_v = int((H - 2) / PITCH)
    off_u = (W - (n_u - 1) * PITCH) / 2
    off_v = (H - (n_v - 1) * PITCH) / 2
    for i in range(n_u):
        for j in range(n_v):
            ax.add_patch(Circle((off_u + i * PITCH, off_v + j * PITCH), 0.35, fc="#b59a55", ec="none"))
    i = P.PERF_HOLE_INSET
    for u in (i, W - i):
        for v in (i, H - i):
            ax.add_patch(Circle((u, v), P.PERF_HOLE_D / 2, fc="white", ec="#555", lw=1.5))
            ax.add_patch(Circle((u, v), P.STUD_D / 2, fc="none", ec="#999", lw=1, ls="--"))
    for name, (u0, v0, w, h, ht, label) in P.PCB_PARTS.items():
        fc = COLORS.get(name, "#6b6b70")
        opt = "opt" in label
        if name in ("C1", "BZ1"):
            ax.add_patch(Circle((u0 + w / 2, v0 + h / 2), min(w, h) / 2, fc=fc, ec="k", lw=1,
                                alpha=0.55 if opt else 0.9, ls="--" if opt else "-"))
        else:
            ax.add_patch(Rectangle((u0, v0), w, h, fc=fc, ec="k", lw=1, alpha=0.55 if opt else 0.85,
                                   ls="--" if opt else "-"))
        light = fc in ("#e8e4d8",)
        ax.text(u0 + w / 2, v0 + h / 2 + (1.2 if name.startswith("J") else 0), label, ha="center",
                va="center", fontsize=7.5 if w < 12 else 9, color="k" if light else "white",
                weight="bold", zorder=5)
    for u, v, n, kind in pins():
        ax.add_patch(Circle((u, v), 0.55, fc="#d8d8d8" if kind != "conn" else "#c9a227", ec="k",
                            lw=0.6, zorder=6))
        if kind == "esp":
            dv = 1.4 if v > P.PCB_PARTS["U1"][1] + 9 else -1.4
            ax.text(u, v + dv, n, ha="center", va="center", fontsize=5.5, color="white", zorder=7,
                    rotation=90)
        elif kind == "tmc":
            left = u < P.PCB_PARTS["U3"][0] + 7
            ax.text(u + (1.0 if left else -1.0), v, n, ha="left" if left else "right", va="center",
                    fontsize=5.5, color="white", zorder=7)
        else:
            ax.text(u, v - 1.9, n, ha="center", va="center", fontsize=5.5, color="k", zorder=7)
    # MP1584 pads
    u0, v0, w, h, _, _ = P.PCB_PARTS["U2"]
    for (u, v, n) in ((u0 + 1.5, v0 + h - 1.5, "IN+"), (u0 + 1.5, v0 + 1.5, "IN−"),
                      (u0 + w - 1.5, v0 + h - 1.5, "OUT+"), (u0 + w - 1.5, v0 + 1.5, "OUT−")):
        ax.add_patch(Circle((u, v), 0.8, fc="#c9a227", ec="k", lw=0.6, zorder=6))
        ax.text(u + (1.3 if u < u0 + 5 else -1.3), v, n, ha="left" if u < u0 + 5 else "right",
                va="center", fontsize=6, color="white", zorder=7)
    ax.annotate("USB-C → side opening", xy=(W + 0.5, P.PCB_PARTS["U1"][1] + 9), xytext=(W + 3, 30),
                fontsize=10, arrowprops=dict(arrowstyle="->"), va="center")
    ax.annotate("cables exit through\nthe bottom of the box", xy=(30, -0.5), xytext=(30, -7),
                ha="center", fontsize=10, arrowprops=dict(arrowstyle="<-"))
    ax.text(W / 2, H + 2.5, f"Perfboard {W:.0f} × {H:.0f} mm, component side, as installed "
            "(bottom edge down)", ha="center", fontsize=13, weight="bold")
    ax.text(W / 2, -12.5, "Dashed circles: M2 studs (Ø6) — keep clear. Dashed parts: optional. "
            "Pin orders marked [TBC] in electronics.md: check the module silkscreens.",
            ha="center", fontsize=9, color="#555")
    ax.set_xlim(-3, W + 18)
    ax.set_ylim(-15, H + 5)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.tight_layout()
    path = os.path.join(OUT, "pcb_layout.png")
    fig.savefig(path)
    print(f"  layout -> {os.path.relpath(path, os.path.dirname(HERE))}")


if __name__ == "__main__":
    main()
