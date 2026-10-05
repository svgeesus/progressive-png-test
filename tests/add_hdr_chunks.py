#!/usr/bin/env python3
"""Make *-hdr.png variants of some test inputs carrying cICP, mDCV and cLLI.

The chunks are inserted straight after IHDR (before PLTE, as cICP requires).
The pixel data is unchanged, so each variant shares its expected output
with the plain input of the same name.
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
INPUT = os.path.join(HERE, "input")

# BT.2020 primaries, PQ transfer, RGB, full range; a P3 mastering display
# of 0.005-1000 cd/m2; MaxCLL 1000 cd/m2 and MaxFALL 400 cd/m2.
HDR_CHUNKS = [
    (b"cICP", bytes([9, 16, 0, 1])),
    (b"mDCV", struct.pack(">8H2I",
                          34000, 16000, 13250, 34500, 7500, 3000,
                          15635, 16450, 10000000, 50)),
    (b"cLLI", struct.pack(">2I", 10000000, 4000000)),
]

HDR_SOURCES = ["P_37x23", "Ptrns_21x17", "RGB_37x23", "I16_3x5", "RGBA_64x64"]


def chunk(ctype, data):
    return (struct.pack(">I", len(data)) + ctype + data +
            struct.pack(">I", zlib.crc32(ctype + data)))


def read_chunks(data):
    """Yield (type, payload) for each chunk of a PNG file's bytes."""
    pos = 8
    while pos < len(data):
        length, = struct.unpack(">I", data[pos:pos + 4])
        yield data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        pos += 12 + length


def main():
    end_of_ihdr = 8 + 12 + 13
    extra = b"".join(chunk(t, d) for t, d in HDR_CHUNKS)
    for name in HDR_SOURCES:
        with open(os.path.join(INPUT, name + ".png"), "rb") as f:
            data = f.read()
        with open(os.path.join(INPUT, name + "-hdr.png"), "wb") as f:
            f.write(data[:end_of_ihdr] + extra + data[end_of_ihdr:])


if __name__ == "__main__":
    main()
