# Test data

Test images for Adam7 progressive rendering, with the expected rendered state
after each of the seven passes. Use them to check `adam7split`, `adam7trunc`,
or any other progressive PNG renderer.

| Directory | Contents |
|---|---|
| `source/` | 29 non-interlaced originals. These are the reference pixels. |
| `input/` | The same 29 images re-encoded with Adam7 interlacing, plus 5 `*-hdr.png` variants that also carry `cICP`, `mDCV` and `cLLI`. Every input has a `gAMA` chunk. |
| `expected/` | `<name>-pass1.png` … `<name>-pass7.png` for each source: the image as displayed after that pass. |

Most of the images are random noise, so a pixel copied from the wrong
position is almost certain to show up as a difference. Two are smooth: a flat
colour (`RGBflat_40x40`) and a gradient (`RGBgradient_64x64`). Smooth images
compress into long deflate matches that can run from one pass into the next,
which matters for truncation. The images cover:

- **Colour types and bit depths:** RGB, RGBA, palette, palette with `tRNS`,
  8-bit greyscale, 16-bit greyscale and 1-bit greyscale.
- **Sizes:** 1×1, 3×5 and 5×3, which are smaller than one 8×8 Adam7 block so
  some passes are empty; 19×13, 21×17 and 37×23, which are not multiples of 8;
  and 40×40 and 64×64.

## Expected rendering

After pass *N*, each pixel shows the pixel at the top-left corner of the block
that contains it. The block size depends on the pass:

| After pass | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Block (w×h) | 8×8 | 4×8 | 4×4 | 2×4 | 2×2 | 1×2 | 1×1 |

That top-left pixel has always already been received by pass *N*. After
pass 7 the image is complete.

`make_expected.py` produces `expected/` from `source/` alone. It doesn't use
`adam7split` or libpng, so it is an independent reference. Each expected image
keeps its source's format (palette, 1-bit, 16-bit and so on), while
`adam7split` expands palette and low bit depths. Compare outputs with the
expected images by pixel values, not by file bytes.

A `*-hdr` input has the same pixels as the input without the suffix, so it
uses that input's expected images.

## Running the checks

From the repository root (needs Python 3 with Pillow):

```
make check
```

This runs two scripts.

[`check.py`](check.py) runs `adam7split` on every file in `input/`. It reports
any output that is missing, still interlaced, or has pixels that differ from
`expected/`. It also checks that `gAMA` is copied, and for `*-hdr` inputs that
`cICP`, `mDCV` and `cLLI` are copied byte for byte and placed before `IDAT`.
It checks each `-m` mask against the pass layout tables in the PNG
specification.

[`check_trunc.py`](check_trunc.py) runs `adam7trunc` on every input, with and
without `-c` and `-r`. It decodes each truncated file with
[`progressive_decode.c`](progressive_decode.c), which feeds the file to
libpng's progressive (push) reader the way a browser receives a download.
For `<name>-truncN.png` it checks that:

- passes 1 to N decode completely, and no row of a later pass arrives;
- the displayed image has the same pixels as `adam7split`'s `<name>-passN.png`;
- without `-c`, the file is a byte prefix of the complete file;
- with `-c`, every chunk has a correct CRC and the file ends with `IEND`.

`1bit_19x13` and `RGBflat_40x40` have no clean cut point without `-r`.
`adam7trunc` reports this, and the script checks those two with `-r` only.

To check other builds, run
`python3 tests/check.py path/to/adam7split` or
`python3 tests/check_trunc.py path/to/adam7trunc path/to/adam7split tests/progressive_decode`.

## Inspecting truncated images in a browser

```
make truncated
```

This writes the following to `truncated/` for every input: the `adam7trunc`
files, and the `adam7split -m` pass images and masks. It also writes an
`index.html` page. Open the page in each browser. For every pass it shows:

1. the truncated file as that browser renders it;
2. `adam7split`'s block-filling rendering, the specification's example,
   which isn't required;
3. the truncated file under the pass's mask;
4. the complete image under the same mask.

Rows 3 and 4 should match in every conforming browser. Rows 1 and 2 match
only in browsers that use the block-filling method. Inputs with no clean cut
point are truncated with `-r`. Pass references come from `adam7split`, not
`expected/`, so they carry the same colour chunks as the inputs.

## Regenerating the data

```
make test-data
```

This rebuilds `source/`, `input/` and `expected/` using the scripts here:

1. `gen_sources.py` generates the originals with Pillow, using a fixed
   random seed.
2. `interlace.c` is a small libpng tool that re-encodes each original as
   Adam7 and adds `gAMA`.
3. `add_hdr_chunks.py` makes the `*-hdr` variants.
4. `make_expected.py` computes the expected images.

The pixel data should come out the same every time, but the PNG files may not
be byte-identical across Pillow, libpng and zlib versions.
