#!/usr/bin/env python3
##
# @file mkbootimg.py
# Minimal, self-contained implementation of Google's Android boot image
# format (boot image header version 0), sufficient to assemble a boot.img
# for devices whose bootloader consumes the standard mkbootimg layout
# instead of the Intel OSIP container.
#
# On the Asus Zenfone Zoom (ZX551ML / z00xs) the primary bootloader loads
# the "second bootloader" payload to its load address (0x10F00000) and jumps
# to it in 32-bit protected mode. We place the whole UEFI FD in that slot,
# keep the stock kernel/ramdisk so the header stays well-formed, and the
# caller appends the ASUS "sig" blob afterwards.
#
# This mirrors the fields printed by upstream mkbootimg's `--header_version 0`
# output so the produced image is byte-compatible with the stock loader.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

import argparse
import hashlib
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
BOOT_MAGIC_SIZE = 8
BOOT_NAME_SIZE = 16
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024


def pad_file(fh, page_size):
    pad = (page_size - (fh.tell() & (page_size - 1))) & (page_size - 1)
    if pad:
        fh.write(b"\x00" * pad)


def read_file(path):
    if not path:
        return b""
    with open(path, "rb") as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser(description="Assemble an Android boot image (header v0)")
    ap.add_argument("--kernel", required=True, help="kernel image")
    ap.add_argument("--ramdisk", default=None, help="ramdisk image")
    ap.add_argument("--second", default=None, help="second-stage payload (the UEFI FD)")
    ap.add_argument("--cmdline", default="", help="kernel command line")
    ap.add_argument("--board", default="", help="board/product name")
    ap.add_argument("--base", type=lambda x: int(x, 0), default=0x10000000)
    ap.add_argument("--kernel_offset", type=lambda x: int(x, 0), default=0x00008000)
    ap.add_argument("--ramdisk_offset", type=lambda x: int(x, 0), default=0x01000000)
    ap.add_argument("--second_offset", type=lambda x: int(x, 0), default=0x00f00000)
    ap.add_argument("--tags_offset", type=lambda x: int(x, 0), default=0x00000100)
    ap.add_argument("--pagesize", type=lambda x: int(x, 0), default=2048)
    ap.add_argument("--header_version", type=int, default=0)
    ap.add_argument("--os_version", type=int, default=0)
    ap.add_argument("-o", "--output", required=True, help="output boot image")
    args = ap.parse_args()

    if args.header_version != 0:
        sys.exit("mkbootimg.py: only boot image header version 0 is supported")

    kernel = read_file(args.kernel)
    ramdisk = read_file(args.ramdisk)
    second = read_file(args.second)

    if not kernel:
        sys.exit("mkbootimg.py: kernel is empty/missing")

    kernel_addr = args.base + args.kernel_offset
    ramdisk_addr = args.base + args.ramdisk_offset
    second_addr = args.base + args.second_offset
    tags_addr = args.base + args.tags_offset

    cmdline = args.cmdline.encode()
    if len(cmdline) >= BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE:
        sys.exit("mkbootimg.py: command line too long")
    cmd = cmdline[:BOOT_ARGS_SIZE - 1]
    extra = cmdline[BOOT_ARGS_SIZE - 1:]

    board = args.board.encode()
    if len(board) >= BOOT_NAME_SIZE:
        sys.exit("mkbootimg.py: board name too long")

    # SHA1 id computed the same way as upstream mkbootimg: over the image data
    # plus each section's length field.
    sha = hashlib.sha1()
    for blob in (kernel, ramdisk, second):
        sha.update(blob)
        sha.update(struct.pack("<I", len(blob)))
    img_id = sha.digest()[:20].ljust(32, b"\x00")[:32]

    hdr = struct.pack(
        "<8sIIIIIIIIII",
        BOOT_MAGIC,
        len(kernel), kernel_addr,
        len(ramdisk), ramdisk_addr,
        len(second), second_addr,
        tags_addr,
        args.pagesize,
        args.header_version,
        args.os_version,
    )
    hdr += board.ljust(BOOT_NAME_SIZE, b"\x00")
    hdr += cmd.ljust(BOOT_ARGS_SIZE, b"\x00")
    hdr += img_id
    hdr += extra.ljust(BOOT_EXTRA_ARGS_SIZE, b"\x00")

    with open(args.output, "wb") as f:
        f.write(hdr)
        pad_file(f, args.pagesize)
        f.write(kernel)
        pad_file(f, args.pagesize)
        if ramdisk:
            f.write(ramdisk)
            pad_file(f, args.pagesize)
        if second:
            f.write(second)
            pad_file(f, args.pagesize)

    print("mkbootimg.py: wrote %s" % args.output)
    print("  boot magic: ANDROID!")
    print("  kernel_size: %d" % len(kernel))
    print("  kernel load address: 0x%08x" % kernel_addr)
    print("  ramdisk size: %d" % len(ramdisk))
    print("  ramdisk load address: 0x%08x" % ramdisk_addr)
    print("  second bootloader size: %d" % len(second))
    print("  second bootloader load address: 0x%08x" % second_addr)
    print("  kernel tags load address: 0x%08x" % tags_addr)
    print("  page size: %d" % args.pagesize)
    print("  boot image header version: %d" % args.header_version)


if __name__ == "__main__":
    main()
