#!/usr/bin/env python3
"""Generate Velo's bicycle icon for both platforms from one pixel grid.

The icon is authored as 1-bit pixel art on a 25x25 lattice, which is the
Pebble menu-icon size. That constraint is the aesthetic: everything is drawn
with Bresenham lines and a midpoint circle so the result sits on the grid
rather than being a smooth vector shape resampled onto it.

Both outputs come from this one grid, so the watch and the phone genuinely
carry the same drawing:

  watchapp/resources/images/menu_icon.png   25x25 black-on-transparent
  android/.../ic_launcher_foreground.xml    the same pixels as vector rects

Run from anywhere:  python3 tools/icon/gen.py
"""

import os
import zlib
import struct

SIZE = 25

# --------------------------------------------------------------------------
# Drawing primitives. Deliberately integer-only, on the lattice.
# --------------------------------------------------------------------------


def blank():
    return [[0] * SIZE for _ in range(SIZE)]


def put(g, x, y):
    if 0 <= x < SIZE and 0 <= y < SIZE:
        g[y][x] = 1


def line(g, x0, y0, x1, y1):
    """Bresenham. Chunky by design -- no antialiasing, no half pixels."""
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        put(g, x0, y0)
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def circle(g, cx, cy, r):
    """Midpoint circle, outline only -- these are wheels, not discs."""
    x, y, err = r, 0, 0
    while x >= y:
        for px, py in ((x, y), (y, x), (-y, x), (-x, y),
                       (-x, -y), (-y, -x), (y, -x), (x, -y)):
            put(g, cx + px, cy + py)
        y += 1
        if err <= 0:
            err += 2 * y + 1
        else:
            x -= 1
            err -= 2 * x + 1


# --------------------------------------------------------------------------
# The bicycle
#
# Side view, cranks forward. Wheel centres sit low so the handlebars and
# saddle have room above without clipping the 25px box.
# --------------------------------------------------------------------------

REAR = (6, 14)
FRONT = (18, 14)
WHEEL_R = 5

BB = (12, 14)      # bottom bracket
SEAT = (8, 7)      # top of seat tube
HEAD = (17, 7)     # top of head tube


def bicycle():
    g = blank()

    circle(g, *REAR, WHEEL_R)
    circle(g, *FRONT, WHEEL_R)

    # The frame triangle is drawn deliberately wide. An anatomically tighter
    # one -- seat and head tubes closer together -- collapses at this size:
    # the three tubes land on adjacent pixels and fill in as a solid blob
    # instead of reading as a frame.
    line(g, *REAR, *BB)          # chainstay
    line(g, *BB, *SEAT)          # seat tube
    line(g, *SEAT, *HEAD)        # top tube
    line(g, *HEAD, *BB)          # down tube
    line(g, *REAR, *SEAT)        # seat stay
    line(g, *HEAD, *FRONT)       # fork

    # Saddle and bars sit a row clear of the top tube, joined by single-pixel
    # posts. Drawn flush against it they merge into one heavy slab across the
    # top of the icon and stop reading as separate parts of a bicycle.
    line(g, 6, 5, 9, 5)          # saddle
    put(g, 8, 6)                 # seat post

    line(g, 15, 5, 19, 5)        # handlebar, side-on
    put(g, 17, 6)                # stem
    put(g, 19, 6)                # the drop, hooking down

    # Chainring: one pixel below the bottom bracket. Any more crank detail
    # than this is indistinguishable from noise at 25 pixels.
    put(g, 12, 15)

    return g


# --------------------------------------------------------------------------
# Output: PNG
# --------------------------------------------------------------------------


def write_png(grid, path, scale=1, rgb=(0, 0, 0)):
    """Minimal RGBA PNG writer. Black pixels, transparent elsewhere.

    Hand-rolled rather than via PIL so the build has no image dependency and
    the output is byte-for-byte reproducible.
    """
    w = h = SIZE * scale
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0 (None) for this scanline
        for x in range(w):
            on = grid[y // scale][x // scale]
            if on:
                raw += bytes((rgb[0], rgb[1], rgb[2], 255))
            else:
                raw += b"\x00\x00\x00\x00"

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    return w, h


# --------------------------------------------------------------------------
# Output: Android vector drawable
# --------------------------------------------------------------------------


def runs(grid):
    """Merge each row's lit pixels into horizontal runs.

    One rect per pixel would be ~180 subpaths; merging runs cuts that to
    around 60 and makes the emitted path readable.
    """
    for y in range(SIZE):
        x = 0
        while x < SIZE:
            if grid[y][x]:
                start = x
                while x < SIZE and grid[y][x]:
                    x += 1
                yield start, y, x - start
            else:
                x += 1


def write_vector(grid, path):
    """Adaptive-icon foreground: 108dp canvas, art inside the 72dp safe zone.

    The grid is scaled by 2.4 (25 * 2.4 = 60dp) and centred, which keeps the
    whole bicycle clear of the circular mask on every launcher shape.
    """
    scale = 2.4
    art = SIZE * scale
    off = (108 - art) / 2

    d = []
    for x, y, n in runs(grid):
        px = off + x * scale
        py = off + y * scale
        d.append("M%.1f,%.1fh%.1fv%.1fh-%.1fz"
                 % (px, py, n * scale, scale, n * scale))

    xml = [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<!--',
        # No "--" anywhere in here: XML forbids a double hyphen inside a
        # comment, and aapt rejects the whole file for it.
        '  Generated by tools/icon/gen.py. Do not hand-edit.',
        '',
        '  The same 25x25 pixel grid the watch menu icon is drawn on, scaled',
        '  onto the adaptive-icon canvas. Keeping the lattice visible is the',
        '  point: it should read as a Pebble icon that happens to be on a',
        '  phone, not as a phone icon shrunk onto a watch.',
        '-->',
        '<vector xmlns:android="http://schemas.android.com/apk/res/android"',
        '    android:width="108dp"',
        '    android:height="108dp"',
        '    android:viewportWidth="108"',
        '    android:viewportHeight="108">',
        '    <path',
        '        android:fillColor="#FF5500"',
        '        android:pathData="%s" />' % "".join(d),
        '</vector>',
        '',
    ]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(xml))
    return len(d)


def ascii_art(grid):
    return "\n".join("".join("##" if c else ". " for c in row) for row in grid)


if __name__ == "__main__":
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    g = bicycle()

    print(ascii_art(g))
    print()

    p1 = os.path.join(root, "watchapp/resources/images/menu_icon.png")
    print("menu_icon.png      %dx%d" % write_png(g, p1))

    p2 = os.path.join(
        root, "android/app/src/main/res/drawable/ic_launcher_foreground.xml")
    print("launcher vector    %d subpaths" % write_vector(g, p2))
