#!/usr/bin/env python3
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

    img[base + 256:base + 256 + len(cmdline)] = cmdline

    # Kernel/ramdisk sizes are unused: the FD is the bootstub payload
    struct.pack_into("<II", img, base + 1024, 0, 0)

    # Parameter bytes; signed images get the fixed magic
    img[base + 1032:base + 1040] = param
    if sig:
        img[base + 1040:base + 1048] = SIGNED_MAGIC

    img[base + 4096:base + 4096 + len(payload)] = payload

    img[img_size:] = b"\xff" * pad

    struct.pack_into("<I", img, 48, len(img) // 512 - 1)

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
