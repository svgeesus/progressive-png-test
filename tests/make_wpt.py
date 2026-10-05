#!/usr/bin/env python3
"""Write WPT reftests for progressive display of Adam7 PNGs into wpt/.

Usage: make_wpt.py

Expects `make wpt` to have copied, for each test image <name>, these files
into wpt/support/: <name>-trunc1.png ... -trunc6.png (from adam7trunc),
<name>-mask1.png ... -mask6.png and <name>-pass7.png (from adam7split -m).

For each image it writes:
  wpt/<name>-progressive.html      the six truncated files, each under the
                                   mask for its pass;
  wpt/<name>-progressive-ref.html  the complete image under the same six
                                   masks.
The masks hide every pixel position not yet received, so the test does
not depend on how the browser fills those in, which the PNG specification
leaves open.
"""
import glob
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WPT = os.path.join(ROOT, "wpt")
SUPPORT = os.path.join(WPT, "support")

# Largest edge of each displayed image, in CSS pixels.  Images are scaled
# by a whole number so that each image pixel lines up with its mask.
TARGET_SIZE = 96
PASSES = range(1, 7)

PAGE = """<!DOCTYPE html>
<meta charset="utf-8">
<title>{title}</title>
<link rel="author" title="Chris Lilley" href="mailto:chris@w3.org">
{links}
<style>
  .pass {{ position: relative; display: inline-block; margin: 0 8px 8px 0; line-height: 0; }}
  .pass img {{ display: block; image-rendering: pixelated; }}
  .pass img.mask {{ position: absolute; left: 0; top: 0; }}
</style>
<p>Test passes if the images below match the reference: the grey masks
leave visible only the pixels received by the end of each pass, which
must show their final values.</p>
{images}
"""


def png_size(path):
    with open(path, "rb") as f:
        return struct.unpack(">II", f.read(24)[16:24])


def images(name, image_for_pass, w, h):
    return "\n".join(
        f'<div class="pass">'
        f'<img src="support/{image_for_pass(n)}" width="{w}" height="{h}" alt="">'
        f'<img class="mask" src="support/{name}-mask{n}.png" width="{w}" '
        f'height="{h}" alt=""></div>'
        for n in PASSES)


def main():
    names = sorted(os.path.basename(p)[:-len("-pass7.png")]
                   for p in glob.glob(os.path.join(SUPPORT, "*-pass7.png")))
    for name in names:
        width, height = png_size(os.path.join(SUPPORT, f"{name}-pass7.png"))
        scale = max(1, TARGET_SIZE // max(width, height))
        w, h = width * scale, height * scale

        test_links = "\n".join([
            '<link rel="help" '
            'href="https://w3c.github.io/png/#13Progressive-display">',
            f'<link rel="match" href="{name}-progressive-ref.html">',
            '<meta name="assert" content="When an Adam7-interlaced PNG has '
            'been received up to the end of pass N, for N from 1 to 6, the '
            'pixels received so far are displayed with their final values. '
            'Positions not yet received, which a viewer may fill in any way, '
            'are hidden by a mask.">',
        ])
        test = PAGE.format(
            title=f"PNG: progressive display of interlaced image {name}",
            links=test_links,
            images=images(name, lambda n: f"{name}-trunc{n}.png", w, h))
        ref = PAGE.format(
            title=f"PNG: progressive display of interlaced image {name} "
                  f"(reference)",
            links="",
            images=images(name, lambda n: f"{name}-pass7.png", w, h))

        with open(os.path.join(WPT, f"{name}-progressive.html"), "w",
                  encoding="utf-8", newline="\n") as f:
            f.write(test)
        with open(os.path.join(WPT, f"{name}-progressive-ref.html"), "w",
                  encoding="utf-8", newline="\n") as f:
            f.write(ref)
    print(f"wrote {len(names)} reftests to {WPT}")


if __name__ == "__main__":
    main()
