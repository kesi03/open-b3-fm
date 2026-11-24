#!/usr/bin/env python3
"""Draw the OPEN B3 tile as a 960x540 PNG using only the Python stdlib.

Run from the repository root:  python tools/make_icon.py
Writes firmware/b3/icon.png. The motif is a Leslie speaker: two rotor horns
above a vented cabinet, in the firmware's purple palette.
"""

import math
import struct
import zlib
from pathlib import Path

W, H = 960, 540
SS = 2  # supersampling for smooth edges
SW, SH = W * SS, H * SS

BG = (16, 10, 24)
PANEL = (30, 18, 48)
PANEL2 = (48, 30, 75)
LINE = (91, 58, 128)
PURPLE = (180, 92, 255)
LILAC = (230, 198, 255)
CREAM = (245, 235, 255)
MUTED = (186, 159, 209)
PINK = (224, 106, 143)
DARK = (8, 5, 14)


def blank():
    px = bytearray(SW * SH * 3)
    for y in range(SH):
        t = y / SH
        base = (BG[0] + int(8 * t), BG[1] + int(4 * t), BG[2] + int(12 * t))
        px[y * SW * 3:(y + 1) * SW * 3] = bytes(base) * SW
    return px


def put(px, x, y, color):
    if 0 <= x < SW and 0 <= y < SH:
        o = (y * SW + x) * 3
        px[o:o + 3] = bytes(color)


def rect(px, x, y, w, h, color):
    x0, x1 = max(0, int(x * SS)), min(SW, int((x + w) * SS))
    y0, y1 = max(0, int(y * SS)), min(SH, int((y + h) * SS))
    if x1 <= x0 or y1 <= y0:
        return
    row = bytes(color) * (x1 - x0)
    for yy in range(y0, y1):
        o = (yy * SW + x0) * 3
        px[o:o + len(row)] = row


def roundrect(px, x, y, w, h, r, color):
    rect(px, x + r, y, w - 2 * r, h, color)
    rect(px, x, y + r, w, h - 2 * r, color)
    for cx, cy in ((x + r, y + r), (x + w - r, y + r),
                   (x + r, y + h - r), (x + w - r, y + h - r)):
        circle(px, cx, cy, r, color)


def ellipse(px, cx, cy, rx, ry, color, angle=0.0, inner=None, inner_angle=None):
    cx, cy, rx, ry = cx * SS, cy * SS, rx * SS, ry * SS
    a = angle if inner_angle is None else inner_angle
    ca, sa = math.cos(a), math.sin(a)
    r2 = (rx * rx, ry * ry)
    irx = iry = 0
    if inner is not None:
        irx, iry = rx * inner[0], ry * inner[1]
    reach = max(rx, ry) + 2
    x0, x1 = max(0, int(cx - reach)), min(SW, int(cx + reach + 1))
    y0, y1 = max(0, int(cy - reach)), min(SH, int(cy + reach + 1))
    for y in range(y0, y1):
        dy = y - cy
        for x in range(x0, x1):
            dx = x - cx
            u, v = dx * ca + dy * sa, -dx * sa + dy * ca
            if (u / r2[0]) ** 2 + (v / r2[1]) ** 2 <= 1.0:
                col = color
                if inner is not None:
                    ia = a if inner_angle is None else inner_angle
                    ic, isd = math.cos(ia), math.sin(ia)
                    iu, iv = dx * ic + dy * isd, -dx * isd + dy * ic
                    if (iu / irx) ** 2 + (iv / iry) ** 2 < 1.0:
                        col = inner[2]
                put(px, x, y, col)


def circle(px, cx, cy, r, color, inner=None):
    ellipse(px, cx, cy, r, r, color, inner=inner)


def ring(px, cx, cy, r, thick, color):
    cx, cy, r, thick = cx * SS, cy * SS, r * SS, thick * SS
    lo, hi = (r - thick) ** 2, (r + thick) ** 2
    for y in range(max(0, int(cy - r - thick)), min(SH, int(cy + r + thick + 1))):
        dy = y - cy
        for x in range(max(0, int(cx - r - thick)), min(SW, int(cx + r + thick + 1))):
            dx = x - cx
            if lo <= dx * dx + dy * dy <= hi:
                put(px, x, y, color)


def line(px, x0, y0, x1, y1, width, color):
    x0, y0, x1, y1 = x0 * SS, y0 * SS, x1 * SS, y1 * SS
    w = width * SS
    dx, dy = x1 - x0, y1 - y0
    ln2 = dx * dx + dy * dy
    for y in range(max(0, int(min(y0, y1) - w - 2)), min(SH, int(max(y0, y1) + w + 3))):
        for x in range(max(0, int(min(x0, x1) - w - 2)), min(SW, int(max(x0, x1) + w + 3))):
            t = 0.0 if ln2 == 0 else max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / ln2))
            qx, qy = x0 + t * dx, y0 + t * dy
            if (x - qx) ** 2 + (y - qy) ** 2 <= w * w:
                put(px, x, y, color)


def downsample(px):
    out = bytearray(W * H * 3)
    n = SS * SS
    for y in range(H):
        for x in range(W):
            r = g = b = 0
            for yy in range(SS):
                row = (y * SS + yy) * SW
                for xx in range(SS):
                    o = (row + x * SS + xx) * 3
                    r += px[o]; g += px[o + 1]; b += px[o + 2]
            o = (y * W + x) * 3
            out[o] = r // n; out[o + 1] = g // n; out[o + 2] = b // n
    return out


def png(rows):
    raw = b''.join(b'\x00' + bytes(row) for row in rows)

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    px = blank()

    # Edge stripes and a faint halo behind the cabinet.
    for y, h, col in ((0, 26, PANEL), (H - 30, 30, PANEL),
                      (74, 6, PANEL2), (H - 88, 6, PANEL2)):
        for yy in range(y * SS, (y + h) * SS):
            px[yy * SW * 3:(yy + 1) * SW * 3] = bytes(col) * SW
    cx, cy = 400, 270
    ring(px, cx, cy, 206, 3, (60, 38, 95))
    ring(px, cx, cy, 184, 2, (54, 33, 86))
    ring(px, cx, cy, 162, 2, (42, 25, 68))

    # Feet, then the two-tier Leslie cabinet.
    roundrect(px, 250, 116, 300, 344, 14, PURPLE)
    roundrect(px, 258, 124, 284, 328, 10, PANEL2)
    rect(px, 268, 134, 264, 122, DARK)
    rect(px, 268, 268, 264, 176, DARK)
    rect(px, 278, 454, 36, 18, LILAC)
    rect(px, 486, 454, 36, 18, LILAC)

    # Top: the rotating horns, frozen mid-spin so the tile reads at any size.
    ellipse(px, 334, 186, 62, 24, LILAC, angle=-0.30)
    ellipse(px, 334, 186, 54, 18, PURPLE, angle=-0.30)
    ellipse(px, 466, 186, 62, 24, LILAC, angle=0.30)
    ellipse(px, 466, 186, 54, 18, PURPLE, angle=0.30)
    line(px, 334, 190, 400, 186, 5, LILAC)
    line(px, 466, 190, 400, 186, 5, LILAC)
    circle(px, 400, 186, 19, CREAM, inner=(0.55, 0.55, PURPLE))
    circle(px, 400, 186, 6, DARK)

    # Bottom: two woofer cones seen through horizontal louvers.
    for sx in (336, 464):
        circle(px, sx, 352, 64, PANEL2)
        circle(px, sx, 352, 48, LINE)
        circle(px, sx, 352, 32, PANEL2)
        circle(px, sx, 352, 12, PURPLE)
    for yy in range(282, 438, 22):
        rect(px, 276, yy, 248, 10, PANEL2)
        rect(px, 276, yy, 248, 3, LINE)

    # A few accent dots, echoing the tonewheel tabs.
    circle(px, 790, 184, 22, LILAC)
    circle(px, 848, 270, 13, PINK)
    circle(px, 790, 356, 19, PURPLE)
    ring(px, 848, 270, 34, 3, LINE)

    small = downsample(px)
    rows = [small[y * W * 3:(y + 1) * W * 3] for y in range(H)]
    out = Path(__file__).resolve().parent.parent / 'firmware' / 'b3' / 'icon.png'
    out.write_bytes(png(rows))
    print(f'wrote {out} ({out.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
