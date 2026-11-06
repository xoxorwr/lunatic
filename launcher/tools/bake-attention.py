#!/usr/bin/env python3
"""Bake a white soft halo once; runtime rendering only scales/tints its alpha."""
import math
import pathlib
import struct
import zlib

size = 256
rows = bytearray()
for y in range(size):
    rows.append(0)  # PNG filter: none
    for x in range(size):
        radius = math.hypot((x + .5 - size / 2) / (size / 2),
                            (y + .5 - size / 2) / (size / 2))
        # A broad ring, fully transparent at the texture boundary.
        ring = (.4 * math.exp(-((radius - .78) / .22) ** 2) +
                .6 * math.exp(-((radius - .88) / .065) ** 2))
        fade = max(0, min(1, (1 - radius) / .12))
        alpha = round(255 * ring * fade)
        rows.extend((255, 255, 255, alpha))

def chunk(kind, data):
    return (struct.pack('!I', len(data)) + kind + data +
            struct.pack('!I', zlib.crc32(kind + data) & 0xffffffff))

png = (b'\x89PNG\r\n\x1a\n' +
       chunk(b'IHDR', struct.pack('!2I5B', size, size, 8, 6, 0, 0, 0)) +
       chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b''))
path = pathlib.Path(__file__).resolve().parent.parent / 'data/assets/attention-halo.png'
path.write_bytes(png)
