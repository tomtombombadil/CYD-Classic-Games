#!/usr/bin/env python3
"""Chess King, Queen, Bishop and Pawn glyphs drawn for CYD Classic Games.

Tom (2026-10-03): DejaVu's King and Queen were too alike and its Pawn
looked poor; its Knight and Rook look great and stay. This draws the other
four as simple silhouettes with clearly different outlines:
  King   - tallest, narrow top with a cross
  Queen  - a wide crown of five points, each with a ball
  Bishop - a pointed mitre with a slit and a small ball on top
  Pawn   - short: a round head on a collar
Each piece is three glyphs: at the Unicode chess code points like DejaVu,
U+265A.. the solid silhouette (body colour) and U+2654.. its dark outline
plus inner lines (white pieces); at U+E000 + (U+2654 offset) thinner light
lines for black pieces (DejaVu's full outline made small black pieces look
light).

Writes assets/chess/CYDChessPieces.ttf (same em and baseline as DejaVu
Sans). Then build the LVGL fonts (Knight/Rook from DejaVu):
  lv_font_conv --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
      -r 0x2656,0x2658,0x265C,0x265E \
      --font assets/chess/CYDChessPieces.ttf \
      -r 0x2654,0x2655,0x2657,0x2659,0x265A,0x265B,0x265D,0x265F,0xE000,0xE001,0xE003,0xE005 \
      --size 33 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
      --lv-font-name chess_font_33 -o src/games/chess/chess_font_33.c
(and the same with 46). Needs: fonttools, shapely. Pass --png to also
render a check sheet.
"""
import sys
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from shapely.affinity import scale
from shapely.geometry import LineString, Point, Polygon, box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

EM, ASC, DESC, ADV = 2048, 1901, -483, 1836   # DejaVu Sans
CX = ADV / 2
T = 70             # white pieces: outline / detail line width (~1.1 px at 33 px)
TB = 45            # black pieces: a thinner light edge and detail lines


def rect(x0, y0, x1, y1, r=0):
    b = box(x0, y0, x1, y1)
    return b.buffer(-r).buffer(r, quad_segs=8) if r else b


def ellipse(cx, cy, rx, ry):
    return scale(Point(cx, cy).buffer(1, quad_segs=24), rx, ry)


def trap(y0, w0, y1, w1, bow=0):
    """Body centred on CX: half-width w0 at y0, w1 at y1; sides bent inward
    by `bow` (a concave, Staunton-like waist)."""
    n = 16
    right = []
    for i in range(n + 1):
        t = i / n
        x = w0 + (w1 - w0) * t - bow * 4 * t * (1 - t)
        right.append((CX + x, y0 + (y1 - y0) * t))
    left = [(2 * CX - x, y) for x, y in reversed(right)]
    return Polygon(right + left)


def hline(y, half):
    return LineString([(CX - half, y), (CX + half, y)])


def base(half=560, top=290):
    """Two-step foot: a wide plinth and a narrower step on it."""
    return unary_union([rect(CX - half, 0, CX + half, 150, 45),
                        rect(CX - half + 80, 120, CX + half - 80, top, 40)])


def base_lines(half=560, top=290):
    return [hline(150, half - 60)]


# ---- The pieces: (silhouette, detail lines) ---------------------------------
def king():
    body = trap(260, 420, 860, 250, bow=100)
    collar = rect(CX - 380, 840, CX + 380, 990, 50)
    dome = ellipse(CX, 990, 260, 130)
    cross_v = rect(CX - 115, 1040, CX + 115, 1496, 30)
    cross_h = rect(CX - 270, 1190, CX + 270, 1400, 30)
    sil = unary_union([base(), body, collar, dome, cross_v, cross_h])
    lines = base_lines() + [hline(840, 400), hline(990, 400)]
    return sil, lines


def queen():
    body = trap(260, 420, 740, 240, bow=100)
    band = rect(CX - 340, 720, CX + 340, 860, 50)
    tips = [(-570, 1240), (-290, 1320), (0, 1350), (290, 1320), (570, 1240)]
    valleys = [(-400, 1030), (-140, 1070), (140, 1070), (400, 1030)]
    pts = [(CX - 330, 850)]
    for k, (x, y) in enumerate(tips):
        pts.append((CX + x, y))
        if k < len(valleys):
            pts.append((CX + valleys[k][0], valleys[k][1]))
    pts.append((CX + 330, 850))
    crown = Polygon(pts)
    balls = [Point(CX + x, y).buffer(110, quad_segs=12) for x, y in tips]
    sil = unary_union([base(), body, band, crown] + balls)
    lines = base_lines() + [hline(720, 360), hline(860, 360)]
    return sil, lines


def bishop():
    collar = rect(CX - 330, 260, CX + 330, 400, 50)
    mitre = unary_union([ellipse(CX, 770, 330, 400),
                         Polygon([(CX - 240, 950), (CX + 240, 950), (CX, 1250)])])
    neck = rect(CX - 70, 1190, CX + 70, 1270)
    ball = Point(CX, 1330).buffer(125, quad_segs=12)
    sil = unary_union([base(480), collar, mitre, neck, ball])
    lines = base_lines(480) + [hline(400, 340),
                               LineString([(CX - 60, 830), (CX + 230, 1080)])]   # the slit
    return sil, lines


def pawn():
    body = trap(260, 380, 640, 180, bow=80)
    collar = rect(CX - 300, 610, CX + 300, 740, 50)
    head = Point(CX, 940).buffer(250, quad_segs=16)
    sil = unary_union([base(480), body, collar, head])
    lines = base_lines(480) + [hline(610, 300), hline(740, 300)]
    return sil, lines


PIECES = {  # outline code point: maker; solid = outline + 6
    0x2654: king, 0x2655: queen, 0x2657: bishop, 0x2659: pawn,
}


def outline(sil, lines, t=T):
    ring = sil.difference(sil.buffer(-t, quad_segs=8))
    detail = unary_union([l.buffer(t * 0.65, cap_style="flat") for l in lines]).intersection(sil)
    return unary_union([ring, detail])


def polys(geom):
    if geom.is_empty:
        return []
    return list(geom.geoms) if hasattr(geom, "geoms") else [geom]


def draw(geom):
    pen = TTGlyphPen(None)
    for p in polys(geom.simplify(4)):
        if p.geom_type != "Polygon":
            continue
        p = orient(p, sign=-1.0)          # TrueType: outer contours clockwise
        for ring in [p.exterior] + list(p.interiors):
            pts = [(round(x), round(y)) for x, y in list(ring.coords)[:-1]]
            pen.moveTo(pts[0])
            for q in pts[1:]:
                pen.lineTo(q)
            pen.closePath()
    return pen.glyph()


def main():
    root = Path(__file__).resolve().parent.parent
    names, glyphs, cmap = [".notdef"], {".notdef": TTGlyphPen(None).glyph()}, {}
    shapes = {}
    for cp, make in PIECES.items():
        sil, lines = make()
        shapes[cp] = (sil, outline(sil, lines))
        # U+2654.. white lines, U+265A.. solid, U+E000 + (cp - 0x2654): the
        # black piece's thinner light lines
        for code, geom in ((cp, shapes[cp][1]), (cp + 6, sil),
                           (0xE000 + cp - 0x2654, outline(sil, lines, TB))):
            n = "uni%04X" % code
            names.append(n)
            glyphs[n] = draw(geom)
            cmap[code] = n
    fb = FontBuilder(EM, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    # Left side bearing = each glyph's xMin (0 here shifted the pieces left
    # in lv_font_conv's output)
    glyf = fb.font["glyf"]
    lsb = {}
    for n in names:
        g = glyf[n]
        g.recalcBounds(glyf)
        lsb[n] = getattr(g, "xMin", 0)
    fb.setupHorizontalMetrics({n: (ADV, lsb[n]) for n in names})
    fb.setupHorizontalHeader(ascent=ASC, descent=DESC)
    fb.setupNameTable({"familyName": "CYD Chess Pieces", "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=ASC, sTypoDescender=DESC, usWinAscent=ASC, usWinDescent=-DESC)
    fb.setupPost()
    out = root / "assets/chess/CYDChessPieces.ttf"
    fb.save(str(out))
    print("wrote", out)
    if "--png" in sys.argv:
        sheet(shapes, root)


def sheet(shapes, root):
    """Both colours at 30 and 42 px, next to DejaVu's Knight and Rook."""
    from PIL import Image, ImageDraw, ImageFont
    dv = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 30)
    mine = ImageFont.truetype(str(root / "assets/chess/CYDChessPieces.ttf"), 30)
    order = [0x2654, 0x2655, 0x2656, 0x2657, 0x2658, 0x2659]
    out = []
    for size in (30, 42, 120):
        d = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", size)
        m = ImageFont.truetype(str(root / "assets/chess/CYDChessPieces.ttf"), size)
        img = Image.new("RGB", (size * 6 + 10, size * 2 + 10), (0xEA, 0xDC, 0xBA))
        g = ImageDraw.Draw(img)
        for row, (body, line) in enumerate((((0xF7, 0xF2, 0xE4), (0x1B, 0x22, 0x33)),
                                            ((0x1B, 0x22, 0x33), (0xF7, 0xF2, 0xE4)))):
            for k, cp in enumerate(order):
                f = m if cp in shapes else d
                if (k + row) % 2:
                    g.rectangle([5 + k * size, 5 + row * size, 4 + (k + 1) * size, 4 + (row + 1) * size],
                                fill=(0xA3, 0x6E, 0x50))
                x, y = 5 + k * size, 5 + row * size - size * 0.1
                g.text((x, y), chr(cp + 6), font=f, fill=body)
                g.text((x, y), chr(cp), font=f, fill=line)
        out.append(img)
    w = max(i.width for i in out)
    sheet_img = Image.new("RGB", (w, sum(i.height for i in out)), (128, 128, 128))
    y = 0
    for i in out:
        sheet_img.paste(i, (0, y))
        y += i.height
    p = root / "assets/chess/pieces_check.png"
    sheet_img.save(p)
    print("wrote", p)


if __name__ == "__main__":
    main()
