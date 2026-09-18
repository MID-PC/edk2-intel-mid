#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Extract hdr/sig/cmdline.txt/parameter from a stock OSIP boot image into
a per-device ImageResources dir for mkosip.py (one-time porting step)."""

import argparse
import os
import struct
import sys

SIG_SIZE = 480  # signature block between the 512-byte header and base


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="stock OSIP boot image (e.g. boot_sign.bin)")
    ap.add_argument("outdir",
                    help="destination directory "
                         "(e.g. Platforms/<dev>Pkg/ImageResources)")
    args = ap.parse_args()

    img = open(args.image, "rb").read()
    if img[:4] != b"$OS$":
        sys.exit("error: %s is not an OSIP image" % args.image)

    base = 512 + SIG_SIZE
    if struct.unpack_from("<I", img, base + 0x400)[0] == 0:
        sys.exit("error: %s: kernel size field at base+0x400 is zero" % args.image)

    os.makedirs(args.outdir, exist_ok=True)
    open(os.path.join(args.outdir, "hdr"), "wb").write(img[:512])
    open(os.path.join(args.outdir, "sig"), "wb").write(img[512:512 + SIG_SIZE])
    open(os.path.join(args.outdir, "cmdline.txt"), "wb").write(
        img[base + 0x100:base + 0x400].split(b"\0")[0])
    open(os.path.join(args.outdir, "parameter"), "wb").write(
        img[base + 0x408:base + 0x410])
    print("unpacked %s into %s (hdr/sig/cmdline.txt/parameter)"
          % (args.image, args.outdir))


if __name__ == "__main__":
    main()