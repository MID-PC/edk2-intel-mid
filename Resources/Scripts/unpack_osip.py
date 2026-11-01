#!/usr/bin/env python3
import argparse
import os
import struct
import sys

SIG_SIZE = 480  # OSIP signature block between the 512-byte header and base


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="stock OSIP boot image (e.g. boot_sign.bin)")
    ap.add_argument("outdir",
                    help="destination directory "
                         "(e.g. Platforms/<device codename>Pkg/ImageResources)")
    args = ap.parse_args()

    with open(args.image, "rb") as fh:
        img = fh.read()
    if img[:4] != b"$OS$":
        sys.exit("error: %s is not an OSIP image" % args.image)

    base = 512 + SIG_SIZE
    if struct.unpack_from("<I", img, base + 0x400)[0] == 0:
        sys.exit("error: %s: kernel size field at base+0x400 is zero" % args.image)

    os.makedirs(args.outdir, exist_ok=True)
    blobs = {
        "hdr": img[:512],
        "sig": img[512:512 + SIG_SIZE],
        "cmdline.txt": img[base + 0x100:base + 0x400].split(b"\0")[0],
        "parameter": img[base + 0x408:base + 0x410],
    }
    for name, blob in blobs.items():
        with open(os.path.join(args.outdir, name), "wb") as fh:
            fh.write(blob)
    print("unpacked %s into %s (hdr/sig/cmdline.txt/parameter)"
          % (args.image, args.outdir))


if __name__ == "__main__":
    main()
