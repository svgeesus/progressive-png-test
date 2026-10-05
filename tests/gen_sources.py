#!/usr/bin/env python3
"""Generate the non-interlaced source images in tests/source/.

Random noise is used so that any pixel taken from the wrong place shows up
as a mismatch.  Sizes include images smaller than one 8x8 Adam7 block, in
which some passes are empty.  The random seed is fixed, so the pixel data is
the same every time.
"""
import os
import random

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "source")

MODES = ["RGB", "RGBA", "P", "L", "I;16"]
SIZES = [(1, 1), (3, 5), (5, 3), (37, 23), (64, 64)]


def noise(rng, mode, w, h):
    im = Image.new(mode, (w, h))
    if mode == "P":
        im.putpalette([rng.randrange(256) for _ in range(768)])
        im.putdata([rng.randrange(256) for _ in range(w * h)])
    elif mode == "I;16":
        im.putdata([rng.randrange(65536) for _ in range(w * h)])
    elif mode == "L":
        im.putdata([rng.randrange(256) for _ in range(w * h)])
    else:
        im.putdata([tuple(rng.randrange(256) for _ in mode)
                    for _ in range(w * h)])
    return im


def main():
    rng = random.Random(1)
    os.makedirs(OUT, exist_ok=True)

    def save(im, name, **kw):
        im.save(os.path.join(OUT, name + ".png"), **kw)

    for mode in MODES:
        for w, h in SIZES:
            save(noise(rng, mode, w, h), f"{mode.replace(';', '')}_{w}x{h}")

    bilevel = noise(rng, "L", 19, 13).point(lambda v: 255 if v > 127 else 0)
    save(bilevel.convert("1"), "1bit_19x13")

    trns = bytes(rng.randrange(256) for _ in range(256))
    save(noise(rng, "P", 21, 17), "Ptrns_21x17", transparency=trns)

    # Smooth images compress with long matches that can run from one pass
    # into the next, so the truncation tests need these as well as noise.
    save(Image.new("RGB", (40, 40), (200, 30, 90)), "RGBflat_40x40")
    gradient = Image.linear_gradient("L").resize((64, 64))
    save(Image.merge("RGB", (gradient, gradient.rotate(90), gradient)),
         "RGBgradient_64x64")


if __name__ == "__main__":
    main()
