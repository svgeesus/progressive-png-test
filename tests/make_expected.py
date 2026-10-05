#!/usr/bin/env python3
"""Generate tests/expected/<name>-passN.png from the non-interlaced sources.

This is a reference implementation of progressive rendering that does not
depend on adam7split or libpng.  After pass N, the pixel at (x, y) shows the
source pixel at the top-left corner of the block containing (x, y), with
blocks of 8x8, 4x8, 4x4, 2x4, 2x2, 1x2 and 1x1 after passes 1 to 7.

Expected images keep the source's mode (palette, 1-bit, 16-bit, ...), so
compare them by pixel values rather than by bytes or format.
"""
import glob
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "source")
EXPECTED = os.path.join(HERE, "expected")

BLOCK_W = [8, 4, 4, 2, 2, 1, 1]
BLOCK_H = [8, 8, 4, 4, 2, 2, 1]


def render(src, pass_index):
    """Return the image as displayed after pass_index (0-based)."""
    bw, bh = BLOCK_W[pass_index], BLOCK_H[pass_index]
    out = src.copy()
    sp, op = src.load(), out.load()
    w, h = src.size
    for y in range(h):
        for x in range(w):
            op[x, y] = sp[x & ~(bw - 1), y & ~(bh - 1)]
    return out


def main():
    os.makedirs(EXPECTED, exist_ok=True)
    for path in sorted(glob.glob(os.path.join(SOURCE, "*.png"))):
        name = os.path.splitext(os.path.basename(path))[0]
        src = Image.open(path)
        src.load()
        for p in range(7):
            out = render(src, p)
            kw = {}
            if "transparency" in src.info:
                kw["transparency"] = src.info["transparency"]
            out.save(os.path.join(EXPECTED, f"{name}-pass{p + 1}.png"), **kw)


if __name__ == "__main__":
    main()
