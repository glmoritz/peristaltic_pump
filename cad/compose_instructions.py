#!/usr/bin/env python3
"""Lay out the LEGO-style assembly pages from the FreeCAD renders (plain Python + PIL).

Input:  docs/assembly/img/manifest.json  (written by cad/instructions.py)
Output: docs/assembly/pages/page_XX.png, docs/assembly/assembly_instructions.pdf,
        docs/assembly/README.md
"""
import json
import os
import textwrap
from collections import OrderedDict

from PIL import Image, ImageDraw, ImageFont, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADIR = os.path.join(ROOT, "docs", "assembly")
PAGES = os.path.join(ADIR, "pages")
W, H = 1754, 1240                     # A4 landscape @ 150 dpi
M = 50
FONT_DIR = "/usr/share/fonts/truetype/dejavu"
BLUE = (91, 143, 214)
BLUE_BG = (232, 241, 251)
INK = (30, 30, 34)
GREY = (120, 124, 130)
RED = (200, 30, 30)
DATE = "2026-11-25"


def font(size, bold=False):
    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    return ImageFont.truetype(os.path.join(FONT_DIR, name), size)


def autocrop(img, pad=12):
    bg = Image.new(img.mode, img.size, (255, 255, 255))
    diff = ImageChops.difference(img.convert("RGB"), bg.convert("RGB"))
    bbox = diff.getbbox()
    if not bbox:
        return img
    x0, y0, x1, y1 = bbox
    return img.crop((max(x0 - pad, 0), max(y0 - pad, 0), min(x1 + pad, img.width), min(y1 + pad, img.height)))


def fit(img, max_w, max_h):
    s = min(max_w / img.width, max_h / img.height)
    return img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.LANCZOS)


def load(rel):
    return autocrop(Image.open(os.path.join(ROOT, rel)).convert("RGB"))


def wrap_text(draw, xy, text, fnt, width_px, fill=INK, spacing=8):
    avg = fnt.getlength("abcdefghijklmnopqrstuvwxyz") / 26
    lines = textwrap.wrap(text, width=max(10, int(width_px / avg)))
    x, y = xy
    for ln in lines:
        draw.text((x, y), ln, font=fnt, fill=fill)
        y += fnt.size + spacing
    return y


def header(draw, section, page_no):
    draw.text((M, 22), section.upper(), font=font(22, True), fill=GREY)
    t = str(page_no)
    draw.text((W - M - font(24, True).getlength(t), 22), t, font=font(24, True), fill=GREY)
    draw.line((M, 58, W - M, 58), fill=(225, 228, 232), width=2)


def callout_box(page, draw, x, y, items, thumbs, cols=3, cell=150):
    """items: [(key, qty)] -> LEGO parts box. Returns the box bottom y."""
    if not items:
        return y
    rows = (len(items) + cols - 1) // cols
    n_cols = min(cols, len(items))
    label_h = 46
    bw = n_cols * (cell + 20) + 20
    bh = rows * (cell + label_h + 16) + 16
    draw.rounded_rectangle((x, y, x + bw, y + bh), radius=18, fill=BLUE_BG, outline=BLUE, width=4)
    for i, (key, qty) in enumerate(items):
        r, c = divmod(i, cols)
        cx = x + 20 + c * (cell + 20)
        cy = y + 16 + r * (cell + label_h + 16)
        th = fit(load(thumbs[key]["img"]), cell, cell - 6)
        page.paste(th, (cx + (cell - th.width) // 2, cy + (cell - 6 - th.height) // 2))
        q = "1 run" if key == "hose" else f"{qty}x"
        draw.text((cx, cy + cell - 30), q, font=font(30, True), fill=INK)
        lbl = thumbs[key]["label"]
        wrap_text(draw, (cx, cy + cell + 2), lbl, font(15), cell, fill=GREY, spacing=2)
    return y + bh


def step_page(st, thumbs, page_no):
    page = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(page)
    header(d, st["section"], page_no)
    d.text((M, 70), str(st["n"]), font=font(120, True), fill=INK)
    nx = M + font(120, True).getlength(str(st["n"])) + 28
    title = st["title"] + (f"   ×{st['times']}" if st.get("times", 1) > 1 else "")
    d.text((nx, 118), title, font=font(40, True), fill=INK)
    box_bottom = callout_box(page, d, M, 230, st["callouts"], thumbs)
    # main picture: right of the call-out box, or full width below it if the box is short
    img = load(st["img"])
    left = M + 580
    area = (left, 120, W - M, H - 170)
    if box_bottom < 420:
        area = (M, max(box_bottom + 20, 200), W - M, H - 170)
    pic = fit(img, area[2] - area[0], area[3] - area[1])
    page.paste(pic, (area[0] + (area[2] - area[0] - pic.width) // 2,
                     area[1] + (area[3] - area[1] - pic.height) // 2))
    # note strip
    y0 = H - 150
    d.line((M, y0, W - M, y0), fill=(225, 228, 232), width=2)
    d.ellipse((M, y0 + 22, M + 44, y0 + 66), fill=BLUE)
    d.text((M + 16, y0 + 26), "i", font=font(32, True), fill="white")
    wrap_text(d, (M + 64, y0 + 24), st["note"], font(25), W - 2 * M - 70)
    return page


def cover_page(man):
    page = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(page)
    d.text((M, 50), "Quiet Condensate Pump", font=font(64, True), fill=INK)
    d.text((M, 135), f"Assembly instructions  ·  prototype v1  ·  {DATE}", font=font(28), fill=GREY)
    pic = fit(load(man["cover"]), W - 2 * M, H - 330)
    page.paste(pic, ((W - pic.width) // 2, 200 + (H - 330 - pic.height) // 2))
    secs = OrderedDict()
    for st in man["steps"]:
        secs.setdefault(st["section"], []).append(st["n"])
    txt = "     ".join(f"{k}: steps {v[0]}–{v[-1]}" for k, v in secs.items())
    d.text((M, H - 90), txt, font=font(26, True), fill=INK)
    d.text((M, H - 50), "Generated from the parametric CAD in cad/ (cad/build.sh instructions).",
           font=font(20), fill=GREY)
    return page


def inventory_page(man, page_no):
    totals = OrderedDict()
    for st in man["steps"]:
        for key, q in st["callouts"]:
            if man["thumbs"][key]["inventory"]:
                totals[key] = totals.get(key, 0) + q * st.get("times", 1)
    page = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(page)
    header(d, "Parts", page_no)
    d.text((M, 75), "What you need", font=font(48, True), fill=INK)
    d.text((M, 140), "Printed parts in PETG (reservoir in black). Also print the 2.4 and 2.8 track "
                     "inserts for tuning, and the electronics per docs/electronics.md.",
           font=font(22), fill=GREY)
    cols, cell, lab = 7, 170, 60
    x0, y0 = M, 200
    for i, (key, q) in enumerate(totals.items()):
        r, c = divmod(i, cols)
        cx = x0 + c * (cell + 62)
        cy = y0 + r * (cell + lab + 30)
        d.rounded_rectangle((cx - 8, cy - 8, cx + cell + 8, cy + cell + lab), radius=14,
                            fill=BLUE_BG, outline=BLUE, width=2)
        th = fit(load(man["thumbs"][key]["img"]), cell, cell - 30)
        page.paste(th, (cx + (cell - th.width) // 2, cy + (cell - 30 - th.height) // 2))
        qs = "1 run" if key == "hose" else f"{q}x"
        d.text((cx, cy + cell - 32), qs, font=font(30, True), fill=INK)
        wrap_text(d, (cx, cy + cell + 2), man["thumbs"][key]["label"], font(16), cell, fill=GREY, spacing=2)
    return page


def main():
    with open(os.path.join(ADIR, "img", "manifest.json")) as f:
        man = json.load(f)
    os.makedirs(PAGES, exist_ok=True)
    for old in os.listdir(PAGES):
        os.remove(os.path.join(PAGES, old))
    pages = [cover_page(man), inventory_page(man, 2)]
    for st in man["steps"]:
        pages.append(step_page(st, man["thumbs"], len(pages) + 1))
    paths = []
    for i, p in enumerate(pages, 1):
        path = os.path.join(PAGES, f"page_{i:02d}.png")
        p.save(path, optimize=True)
        paths.append(path)
    pdf = os.path.join(ADIR, "assembly_instructions.pdf")
    pages[0].save(pdf, save_all=True, append_images=pages[1:], resolution=150, quality=88)
    # README index
    lines = ["# Assembly instructions (LEGO style)", "",
             f"**[assembly_instructions.pdf](assembly_instructions.pdf)**, {len(pages)} pages, "
             f"prototype v1 ({DATE}).", "",
             "Generated from the parametric CAD: every picture is rendered by FreeCAD from the same "
             "models that produce the STLs, so the instructions follow the design. To regenerate them:",
             "", "```sh", "cad/build.sh instructions", "```", "",
             "Electronics wiring and the perfboard layout: [../electronics.md](../electronics.md).", ""]
    for i, path in enumerate(paths, 1):
        title = "Cover" if i == 1 else "Parts" if i == 2 else \
            f"Step {man['steps'][i - 3]['n']}: {man['steps'][i - 3]['section']}: {man['steps'][i - 3]['title']}"
        lines += [f"### {title}", "", f"![{title}](pages/{os.path.basename(path)})", ""]
    with open(os.path.join(ADIR, "README.md"), "w") as f:
        f.write("\n".join(lines))
    print(f"  {len(pages)} pages -> docs/assembly/assembly_instructions.pdf "
          f"({os.path.getsize(pdf) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
