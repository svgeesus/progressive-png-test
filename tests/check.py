#!/usr/bin/env python3
"""Run adam7split on every tests/input/*.png and verify the results.

Usage: check.py [path/to/adam7split]

For each input it checks that:
  - seven non-interlaced outputs are written;
  - every output's pixels match tests/expected/<name>-passN.png;
  - gAMA, and for *-hdr inputs cICP, mDCV and cLLI, are copied
    byte for byte and placed before IDAT;
  - each -m mask is transparent exactly at the pixel positions received
    by the end of that pass, and opaque elsewhere.
Exits with status 1 if anything fails.
"""
import glob
import os
import subprocess
import sys
import tempfile

from PIL import Image

from add_hdr_chunks import HDR_CHUNKS, read_chunks

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT = os.path.join(HERE, "input")
EXPECTED = os.path.join(HERE, "expected")

# Adam7 pass layout, as tabulated in the PNG specification.
STARTING_ROW = [0, 0, 4, 0, 2, 0, 1]
STARTING_COL = [0, 4, 0, 2, 0, 1, 0]
ROW_INCREMENT = [8, 8, 8, 4, 4, 2, 2]
COL_INCREMENT = [8, 8, 4, 4, 2, 2, 1]


def received_mask(width, height, n):
    """Alpha values a correct mask has after pass n: 0 where received."""
    alpha = [255] * (width * height)
    for p in range(n):
        for y in range(STARTING_ROW[p], height, ROW_INCREMENT[p]):
            for x in range(STARTING_COL[p], width, COL_INCREMENT[p]):
                alpha[y * width + x] = 0
    return alpha


def pixels(path):
    """Pixel values normalised so differently-encoded images compare."""
    im = Image.open(path)
    im = im.convert("I") if im.mode.startswith("I") else im.convert("RGBA")
    return im.size, list(im.getdata())


def check_chunks(path, wanted):
    with open(path, "rb") as f:
        chunks = list(read_chunks(f.read()))
    types = [t for t, _ in chunks]
    found = dict(chunks)
    problems = []
    if found[b"IHDR"][12] != 0:
        problems.append("output is interlaced")
    for ctype, payload in wanted:
        if ctype not in found:
            problems.append(f"{ctype.decode()} missing")
        elif found[ctype] != payload:
            problems.append(f"{ctype.decode()} payload differs")
        elif types.index(ctype) > types.index(b"IDAT"):
            problems.append(f"{ctype.decode()} after IDAT")
    return problems


def main():
    prog = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./adam7split")
    inputs = sorted(glob.glob(os.path.join(INPUT, "*.png")))
    failures = checked = 0

    with tempfile.TemporaryDirectory() as tmp:
        for path in inputs:
            name = os.path.splitext(os.path.basename(path))[0]
            ref_name = name.removesuffix("-hdr")
            prefix = os.path.join(tmp, name)

            with open(path, "rb") as f:
                in_chunks = dict(read_chunks(f.read()))
            wanted = [(b"gAMA", in_chunks[b"gAMA"])]
            if name.endswith("-hdr"):
                wanted += HDR_CHUNKS

            run = subprocess.run([prog, "-m", path, prefix],
                                 capture_output=True, text=True)
            if run.returncode != 0:
                print(f"FAIL {name}: exit {run.returncode}: {run.stderr.strip()}")
                failures += 1
                continue

            for p in range(1, 8):
                out = f"{prefix}-pass{p}.png"
                exp = os.path.join(EXPECTED, f"{ref_name}-pass{p}.png")
                checked += 1
                if not os.path.exists(out):
                    problems = ["not written"]
                else:
                    problems = check_chunks(out, wanted)
                    if pixels(out) != pixels(exp):
                        problems.append("pixels differ from expected")
                mask = f"{prefix}-mask{p}.png"
                if not os.path.exists(mask):
                    problems.append("mask not written")
                else:
                    im = Image.open(mask).convert("RGBA")
                    if (list(im.getchannel("A").getdata()) !=
                            received_mask(*im.size, p) or
                            im.size != Image.open(exp).size):
                        problems.append("mask is wrong")
                if problems:
                    print(f"FAIL {name} pass {p}: {'; '.join(problems)}")
                    failures += 1

    print(f"{len(inputs)} inputs, {checked} outputs checked, "
          f"{failures} failures")
    return 1 if failures or not inputs else 0


if __name__ == "__main__":
    sys.exit(main())
