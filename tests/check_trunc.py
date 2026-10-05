#!/usr/bin/env python3
"""Check adam7trunc against adam7split on every tests/input/*.png.

Usage: check_trunc.py adam7trunc adam7split progressive_decode

Each input is truncated with and without each of the -c and -r options.
Each truncated file is decoded with progressive_decode, which pushes it
through libpng's progressive reader as a browser would.  For trunc file N
this checks that:
  - passes 1..N decode completely and no row of a later pass arrives
    (unless the image is already complete);
  - the displayed result has the same pixels as adam7split's passN output;
  - without -c, the file is a byte prefix of the complete file;
  - with -c, every chunk has a correct CRC and the file ends with IEND;
  - trunc7 is the complete input (the rebuilt file, with -r).
adam7trunc may report that it found no clean cut (exit status 2) only
without -r; such inputs are then checked with -r alone.
Exits with status 1 if anything fails.
"""
import glob
import os
import re
import subprocess
import sys
import tempfile
import zlib

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT = os.path.join(HERE, "input")

MODES = [[], ["-c"], ["-r"], ["-r", "-c"]]


def pixels(path):
    im = Image.open(path)
    im = im.convert("I") if im.mode.startswith("I") else im.convert("RGBA")
    return im.size, list(im.getdata())


def chunk_problems(data):
    """Check that a file is a complete sequence of chunks ending in IEND."""
    problems, pos, ctype = [], 8, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        ctype = data[pos + 4:pos + 8]
        end = pos + 12 + length
        if end > len(data):
            return problems + [f"{ctype.decode()} chunk is cut short"]
        crc = int.from_bytes(data[end - 4:end], "big")
        if crc != zlib.crc32(data[pos + 4:end - 4]):
            problems.append(f"bad CRC in {ctype.decode()}")
        pos = end
    if ctype != b"IEND":
        problems.append("does not end with IEND")
    return problems


def main():
    trunc, split, decoder = (os.path.abspath(p) for p in sys.argv[1:4])
    inputs = sorted(glob.glob(os.path.join(INPUT, "*.png")))
    failures = checked = 0
    needed_r = []

    def fail(msg):
        nonlocal failures
        print("FAIL", msg)
        failures += 1

    with tempfile.TemporaryDirectory() as tmp:
        for path in inputs:
            name = os.path.splitext(os.path.basename(path))[0]
            with open(path, "rb") as f:
                original = f.read()
            subprocess.run([split, path, os.path.join(tmp, name)],
                           check=True, capture_output=True)

            unclean = False
            for mode in MODES:
                label = f"{name} [{' '.join(mode) or 'default'}]"
                if unclean and "-r" not in mode:
                    continue
                prefix = os.path.join(tmp, name + "".join(mode))
                run = subprocess.run([trunc, *mode, path, prefix],
                                     capture_output=True, text=True)
                if run.returncode == 2 and not mode:
                    unclean = True
                    needed_r.append(name)
                    continue
                if run.returncode != 0:
                    fail(f"{label}: exit {run.returncode}: "
                         f"{run.stderr.strip()}")
                    continue

                with open(f"{prefix}-trunc7.png", "rb") as f:
                    complete = f.read()
                if "-r" not in mode and complete != original:
                    fail(f"{label}: trunc7 is not the complete input")

                for n in range(1, 8):
                    checked += 1
                    tfile = f"{prefix}-trunc{n}.png"
                    rendered = f"{prefix}-render{n}.png"
                    with open(tfile, "rb") as f:
                        data = f.read()

                    problems = []
                    if "-c" in mode:
                        problems += chunk_problems(data)
                    elif not complete.startswith(data):
                        problems.append("not a prefix of the complete file")

                    dec = subprocess.run([decoder, tfile, rendered],
                                         capture_output=True, text=True)
                    m = re.match(r"passes=(\d) next_rows=(\d+)", dec.stdout)
                    if dec.returncode != 0 or not m:
                        problems.append(f"decode failed: {dec.stderr.strip()}")
                    else:
                        passes, next_rows = int(m[1]), int(m[2])
                        if passes == 0:
                            problems.append("no complete pass")
                        elif next_rows:
                            problems.append(f"{next_rows} rows of pass "
                                            f"{passes + 1} also decoded")
                        elif (pixels(rendered) !=
                              pixels(os.path.join(tmp, f"{name}-pass{n}.png"))):
                            problems.append(f"shows pass {passes}, which "
                                            f"differs from split pass {n}")
                    if problems:
                        fail(f"{label} trunc{n}: {'; '.join(problems)}")

    print(f"{len(inputs)} inputs, {checked} truncated files checked, "
          f"{failures} failures")
    if needed_r:
        print("no clean cut without -r (checked with -r only): "
              + ", ".join(needed_r))
    return 1 if failures or not inputs else 0


if __name__ == "__main__":
    sys.exit(main())
