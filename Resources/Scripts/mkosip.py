#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""
Assemble an Intel MID "OSIP" boot image.

The layout mirrors what the mboot tool produces for these devices:

    [OSIP header (512 bytes)][signature block (optional)]

after which, relative to the end of the signature block ("base"):

    base + 0x0100    command line (up to 1024 bytes)
    base + 0x0400    LE32 kernel size, LE32 ramdisk size, 8-byte
                     "parameter" field at +0x0408 and, for signed images,
                     a fixed magic at +0x0410
    base + 0x1000    payload (the firmware bootstub)

The kernel/ramdisk slots are unused here: the firmware bootstub is what the
bootloader enters directly, so they stay at zero size.  Device-specific
hdr/sig/cmdline/parameter bits come from a per-device ImageResources
directory (extracted once from the stock boot image while porting).

The OSIP header carries the total number of sectors at offset 48 and an
XOR checksum over its first 56 bytes at offset 7.
"""

import argparse
import os
import struct
import sys

SIGNED_MAGIC = b"\xBD\x02\xBD\x02\xBD\x12\xBD\x12"
HDR_SIZE = 512


def read_file(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        sys.exit("mkosip: cannot open '%s': %s" % (path, e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True, help="output image")
    ap.add_argument("-p", "--payload", required=True,
                    help="firmware bootstub binary (the edk2 FD)")
    ap.add_argument("-d", "--osipdir", required=True,
                    help="directory with hdr/sig/cmdline.txt/parameter "
                         "(the device's ImageResources)")
    args = ap.parse_args()

    hdr = read_file(os.path.join(args.osipdir, "hdr"))
    if len(hdr) != HDR_SIZE:
        sys.exit("mkosip: hdr must be %d bytes" % HDR_SIZE)

    sig_path = os.path.join(args.osipdir, "sig")
    sig = read_file(sig_path) if os.path.exists(sig_path) else b""

    cmdline = read_file(os.path.join(args.osipdir, "cmdline.txt"))[:1024]
    param = read_file(os.path.join(args.osipdir, "parameter"))[:8]
    param = param.ljust(8, b"\0")
    payload = read_file(args.payload)

    base = len(hdr) + len(sig)

    img_size = base + 4096 + len(payload)
    pad = (-img_size) % 512
    img = bytearray(img_size + pad)

    img[0:len(hdr)] = hdr
    img[len(hdr):base] = sig

    # Command line block
    img[base + 256:base + 256 + len(cmdline)] = cmdline

    # Kernel/ramdisk sizes are unused: the firmware is the bootstub payload
    struct.pack_into("<II", img, base + 1024, 0, 0)

    # Platform parameters and, for signed images, the padding magic
    img[base + 1032:base + 1040] = param
    if sig:
        img[base + 1040:base + 1048] = SIGNED_MAGIC

    # Payload goes into the bootstub slot
    img[base + 4096:base + 4096 + len(payload)] = payload

    # Trailing padding
    img[img_size:] = b"\xff" * pad

    # Update the sector count
    struct.pack_into("<I", img, 48, len(img) // 512 - 1)

    # Update the XOR checksum over the first 56 header bytes
    chk = bytearray(img[:56])
    chk[7] = 0
    x = 0
    for b in chk:
        x ^= b
    img[7] = x

    with open(args.output, "wb") as f:
        f.write(img)


if __name__ == "__main__":
    main()