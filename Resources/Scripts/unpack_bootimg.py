#!/usr/bin/env python3
##
# @file unpack_bootimg.py
# Extract the kernel and ramdisk from a stock Android boot image (header v0)
# so they can be re-used when reassembling the image with our UEFI FD in the
# "second bootloader" slot.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

import argparse
import os
import struct
import sys

BOOT_MAGIC = b"ANDROID!"


def align(value, page):
    return (value + page - 1) & ~(page - 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="stock boot image")
    ap.add_argument("outdir", help="destination directory for kernel/ramdisk")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        data = f.read()

    if data[:8] != BOOT_MAGIC:
        sys.exit("error: %s is not an Android boot image (missing ANDROID! magic)"
                 % args.image)

    (kernel_size, _kernel_addr,
     ramdisk_size, _ramdisk_addr,
     second_size, _second_addr,
     _tags_addr, page_size,
     header_version, _os_version) = struct.unpack_from("<IIIIIIIIII", data, 8)

    os.makedirs(args.outdir, exist_ok=True)

    # For header v0/v1 the header occupies exactly one page; sections follow
    # page-aligned in order: kernel, ramdisk, second.
    off = page_size

    kernel = data[off:off + kernel_size]
    off = align(off + kernel_size, page_size)
    ramdisk = data[off:off + ramdisk_size]

    with open(os.path.join(args.outdir, "kernel"), "wb") as f:
        f.write(kernel)
    with open(os.path.join(args.outdir, "ramdisk"), "wb") as f:
        f.write(ramdisk)

    print("unpacked %s (header v%d, page %d):" % (args.image, header_version, page_size))
    print("  kernel:  %d bytes" % kernel_size)
    print("  ramdisk: %d bytes" % ramdisk_size)
    print("  second:  %d bytes (discarded)" % second_size)
    print("  -> %s/kernel, %s/ramdisk" % (args.outdir, args.outdir))


if __name__ == "__main__":
    main()
