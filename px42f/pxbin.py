#!/usr/bin/env python3
"""Makes the .bin the PX-42F runs from the linked app: the image (objcopy
-O binary of .image) with its header's sizes and CRC filled in (px_app.h).

    pxbin.py IMAGE SYMBOLS OUT.bin     (SYMBOLS: arm-none-eabi-nm's output)
"""
import struct
import sys
import zlib

BASE, RAM = 0x20060000, 0x20000
MAGIC, HEADER = 0x50415850, 24


def main():
    image_path, symbols_path, out_path = sys.argv[1:4]
    image = bytearray(open(image_path, 'rb').read())
    symbols = {}
    for line in open(symbols_path):
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)

    magic, version, _, _, entry, _ = struct.unpack_from('<6I', image, 0)
    if magic != MAGIC or symbols.get('px_app_header') != BASE:
        sys.exit(f'{image_path}: the header is not at 0x{BASE:08X} (px42f/app.ld)')
    bss = symbols['__bss_end'] - symbols['__bss_start']
    if len(image) + bss > RAM:
        sys.exit(f'{out_path}: {len(image) + bss} bytes of code, data and bss: more than the 128 KB an app has')
    struct.pack_into('<II', image, 8, len(image), bss)
    struct.pack_into('<I', image, 20, zlib.crc32(image[HEADER:]))
    open(out_path, 'wb').write(image)
    print(f'{out_path}: {len(image)} bytes, {bss} bytes of bss, '
          f'{RAM - len(image) - bss} bytes left for the heap, API {version}, entry 0x{entry:08X}')


if __name__ == '__main__':
    main()
