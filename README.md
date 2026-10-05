# progressive-png-test
Generates tests for progressive PNG

## adam7split

Takes one Adam7-interlaced PNG and writes seven PNGs showing what a
progressive renderer displays after each interlace pass. Each pixel received
so far is replicated over the block it represents until later passes fill it
in: 8×8 after pass 1, then 4×8, 4×4, 2×4, 2×2, 1×2, and 1×1 (the complete
image) after pass 7.

```
make
./adam7split [-m] input.png [output-prefix]
```

`make` builds both `adam7split` and `adam7trunc`.

Writes `<prefix>-pass1.png` … `<prefix>-pass7.png`. The prefix defaults to the
input name without `.png`. Non-interlaced input is rejected.

This block-filling display is the example the PNG specification gives in
[section 13.10](https://w3c.github.io/png/#13Progressive-display), but the
specification doesn't require it. Chrome renders this way, while Firefox
interpolates between received pixels. Both are conforming, so the
`passN` images are not a pass/fail reference for intermediate passes.

`-m` also writes `<prefix>-mask1.png` … `<prefix>-mask7.png`. Each mask is
transparent at the pixel positions received by the end of that pass and
opaque grey elsewhere. Those positions hold their final values however a
viewer fills in the rest. So a partly loaded image under `maskN` should look
the same as the complete image under `maskN`, in any conforming viewer.

Pixels are decoded with libpng's interlace handling, one pass at a time.
Palette, sub-8-bit greyscale and `tRNS` are expanded, so outputs are 8-bit
greyscale/RGB with or without alpha; 16-bit input stays 16-bit. `gAMA`,
`cHRM`, `sRGB`, `iCCP` and `cICP`, and the HDR metadata chunks `mDCV` and
`cLLI`, are copied to every output. libpng gained support for `cICP` in 1.6.45
and for `mDCV`/`cLLI` in later 1.6 releases. When built against an older
libpng, any of these three it doesn't support is copied byte for byte as an
unknown chunk instead.

## adam7trunc

Takes one Adam7-interlaced PNG and writes seven truncated copies,
`<prefix>-trunc1.png` … `<prefix>-trunc7.png`. Truncated file N contains
enough image data to decode passes 1 to N and not one complete row of any
later pass. A progressive decoder that is given the file should therefore show
the same picture as `adam7split`'s `<prefix>-passN.png`. The two programs
together give matching test and reference images, for example for WPT.

```
./adam7trunc [-c] [-r] input.png [output-prefix]
```

- By default each file is a plain byte prefix of the input, like a partly
  downloaded file. It usually ends partway through an `IDAT` chunk.
- `-c` ends the file on complete chunks instead. The last `IDAT` is shortened
  with a correct CRC and an `IEND` follows. This suits decoders that discard
  an incomplete chunk. The zlib stream is still unfinished, so expect decoders
  to report missing image data at `IEND`.
- `-r` first recompresses the image data with a zlib sync flush at the end of
  every pass, one `IDAT` chunk per pass. Pixels are unchanged, but the files
  are then truncations of the recompressed PNG, and `trunc7` is that complete
  PNG. Use `-r` when `adam7trunc` warns that there is no clean cut: a deflate
  match ran from one pass into the next, so no byte boundary separates them.
  This is common in flat or smooth images. Without `-r` the exit status is 2
  in that case.

If no rows follow pass N, as always for N = 7 and earlier for tiny images,
file N is the complete PNG.

**Where the cut goes.** The program inflates the `IDAT` stream one byte at a
time to find the range of cut points that complete pass N without completing
the next pass's first row. It cuts at the **end** of that range. Decoders can
lag behind zlib. libpng's progressive reader, cut at the earliest point, may
still be holding back the last rows of pass N. Cutting late gives such
decoders the most room. The program prints each file's cut position and the
range it was chosen from.

## WPT reftests

`make wpt` writes web-platform-tests reftests to [wpt/](wpt/), with their
images in [wpt/support/](wpt/support/). The Makefile copies the images from
`tests/truncated/`, and [tests/make_wpt.py](tests/make_wpt.py) writes the
HTML. For each test image `<name>` there are two files:

- **`<name>-progressive.html`:** shows `<name>-trunc1.png` … `-trunc6.png`,
  each under the mask for its pass.
- **`<name>-progressive-ref.html`:** shows the complete image under the same
  six masks.

The masks leave visible only the pixels received by the end of each pass. A
browser passes if it displays those pixels with their final values, however
it fills in the rest. Images whose first truncation is already the complete
file (the 1×1 ones) are skipped, because they have nothing to show
progressively. Images are enlarged by whole-number factors with
`image-rendering: pixelated`.

### Tests

`make check` runs `adam7split` on the test images in [tests/](tests/) and
compares every output against an independently computed expected image. It
then checks that every `adam7trunc` output, decoded progressively by libpng,
shows the same picture as the matching `adam7split` output. See
[tests/README.md](tests/README.md) for what the test set covers and how to
regenerate it.
