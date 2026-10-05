#!/usr/bin/env python3
"""Write truncated images, references and masks to tests/truncated/.

Usage: make_truncated.py path/to/adam7trunc path/to/adam7split

For every tests/input/*.png this writes adam7trunc's <name>-truncN.png and
adam7split -m's <name>-passN.png and <name>-maskN.png.  Inputs with no
clean cut point (adam7trunc exit status 2) are redone with -r.

It also writes tests/truncated/index.html, which shows, for each pass N:
  1. the truncated file as the browser renders it;
  2. adam7split's passN (the specification's example rendering, which
     replicates each received pixel over its block);
  3. the truncated file under maskN;
  4. the complete image under maskN.
Rows 3 and 4 hide everything except the pixel positions received by the
end of pass N, which hold their final values whatever method the browser
uses to fill the rest, so they should match in any browser.
"""
import glob
import html
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT = os.path.join(HERE, "input")
OUT = os.path.join(HERE, "truncated")

# Largest edge of each displayed image, in CSS pixels.  Images are scaled
# by a whole number so that each image pixel lines up with the mask.
TARGET_SIZE = 128

PAGE = """<!doctype html>
<meta charset="utf-8">
<title>Truncated Adam7 images</title>
<style>
  body {{ font: 14px system-ui, sans-serif; margin: 16px; background: #fff; color: #222; }}
  h2 {{ font-size: 16px; margin: 32px 0 4px; }}
  table {{ border-collapse: collapse; }}
  td, th {{ padding: 4px 8px; text-align: center; vertical-align: top; }}
  th {{ font-weight: normal; color: #666; }}
  th.row {{ text-align: right; max-width: 14em; }}
  tr.masked td {{ background: #eef4ff; }}
  .stack {{ position: relative; display: inline-block; line-height: 0; }}
  .stack img {{ display: block; image-rendering: pixelated; }}
  .stack img.mask {{ position: absolute; left: 0; top: 0; }}
  .note {{ color: #a40; }}
  #dpr {{ font-weight: bold; }}
</style>
<h1>Truncated Adam7 images</h1>
<p>For each pass N, from top to bottom:</p>
<ol>
  <li><code>&lt;name&gt;-truncN.png</code> as this browser renders it.</li>
  <li><code>&lt;name&gt;-passN.png</code> from <code>adam7split</code>: the
    specification's example rendering, with each received pixel replicated
    over its block. The specification does not require this method.</li>
  <li><code>&lt;name&gt;-truncN.png</code> under <code>&lt;name&gt;-maskN.png</code>.</li>
  <li>The complete image under the same mask.</li>
</ol>
<p>The mask is grey at every pixel position not yet received by the end of
pass N. The positions it leaves visible hold their final values whatever
method a browser uses to fill in the rest, so rows 3 and 4 (shaded) should
match in every browser. Images are enlarged by whole-number factors with
<code>image-rendering: pixelated</code>. Device pixel ratio here:
<span id="dpr"></span>; if it is not a whole number, edges may not line up
exactly.</p>
<script>document.getElementById("dpr").textContent = window.devicePixelRatio;</script>
{sections}
"""


def png_size(path):
    with open(path, "rb") as f:
        return struct.unpack(">II", f.read(24)[16:24])


def img(src, w, h, mask=None):
    tag = f'<img src="{html.escape(src)}" width="{w}" height="{h}" alt="">'
    if mask:
        tag += (f'<img class="mask" src="{html.escape(mask)}" width="{w}" '
                f'height="{h}" alt="">')
    return f'<td><span class="stack">{tag}</span></td>'


def main():
    trunc = os.path.abspath(sys.argv[1])
    split = os.path.abspath(sys.argv[2])
    os.makedirs(OUT, exist_ok=True)
    sections = []

    for path in sorted(glob.glob(os.path.join(INPUT, "*.png"))):
        name = os.path.splitext(os.path.basename(path))[0]
        prefix = os.path.join(OUT, name)

        run = subprocess.run([trunc, path, prefix], capture_output=True)
        note = ""
        if run.returncode == 2:
            run = subprocess.run([trunc, "-r", path, prefix],
                                 capture_output=True)
            note = (' <span class="note">(no clean cut in the original '
                    'data; made with -r)</span>')
        if run.returncode != 0:
            sys.exit(f"adam7trunc failed on {name}: {run.stderr.decode()}")
        subprocess.run([split, "-m", path, prefix], check=True,
                       capture_output=True)

        width, height = png_size(path)
        scale = max(1, TARGET_SIZE // max(width, height))
        w, h = width * scale, height * scale
        passes = range(1, 8)

        rows = [
            ("truncated", "",
             [img(f"{name}-trunc{n}.png", w, h) for n in passes]),
            ("adam7split", "",
             [img(f"{name}-pass{n}.png", w, h) for n in passes]),
            ("truncated, masked", "masked",
             [img(f"{name}-trunc{n}.png", w, h, f"{name}-mask{n}.png")
              for n in passes]),
            ("complete, masked", "masked",
             [img(f"{name}-pass7.png", w, h, f"{name}-mask{n}.png")
              for n in passes]),
        ]
        heads = "".join(f"<th>pass {n}</th>" for n in passes)
        body = "".join(
            f'<tr class="{cls}"><th class="row">{label}</th>{"".join(cells)}</tr>'
            for label, cls, cells in rows)
        sections.append(
            f"<h2>{html.escape(name)} ({width}&times;{height}, shown "
            f"&times;{scale}){note}</h2>\n"
            f"<table><tr><th></th>{heads}</tr>{body}</table>")

    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as f:
        f.write(PAGE.format(sections="\n".join(sections)))
    print(f"wrote {len(sections)} images' files and index.html to {OUT}")


if __name__ == "__main__":
    main()
