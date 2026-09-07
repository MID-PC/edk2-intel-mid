#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Make an edk2 FD directly executable at offset 0.

The FD starts with the FVSEC firmware volume header, whose first 16 bytes
are the (unused) zero vector.  Patch a 'jmp rel32' there so that entering
the binary at its very first byte lands on SecEntry (_ModuleEntryPoint,
'cli; cld') inside the SEC TE image at the top of FVSEC.
"""

import argparse
import struct
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", help="firmware FD to patch (in place)")
    args = ap.parse_args()
    path = args.path

    data = bytearray(open(path, "rb").read())

    if bytes(data[40:44]) != b"_FVH":
        sys.exit("error: no FV header at offset 0 of the FD")

    hdrlen = struct.unpack_from("<H", data, 48)[0]
    entry = None
    off = hdrlen
    while off + 24 < 0x10000:
        if bytes(data[off:off + 16]) == b"\xff" * 16:
            break
        size = data[off + 20] | (data[off + 21] << 8) | (data[off + 22] << 16)
        if size < 24 or size == 0xffffff:
            break
        soff = off + 24
        while soff + 4 <= off + size:
            ssize = data[soff] | (data[soff + 1] << 8) | (data[soff + 2] << 16)
            stype = data[soff + 3]
            if ssize < 4:
                break
            if stype == 0x12 and bytes(data[soff + 4:soff + 6]) == b"VZ":
                te = soff + 4
                stripped = struct.unpack_from("<H", data, te + 6)[0]
                aoe = struct.unpack_from("<I", data, te + 8)[0]
                entry = te + aoe - stripped + 40
            soff += (ssize + 3) & ~3
        off += (size + 7) & ~7

    if entry is None:
        sys.exit("error: SEC TE image not found in FVSEC")

    if data[entry] != 0xFA or data[entry + 1] != 0xFC:
        sys.exit("error: SEC entry at 0x%x does not start with cli;cld (0x%02x 0x%02x)"
                 % (entry, data[entry], data[entry + 1]))

    data[0] = 0xE9
    struct.pack_into("<i", data, 1, entry - 5)
    open(path, "wb").write(data)
    print("    SEC entry at 0x%06x, jmp patched at offset 0" % entry)


if __name__ == "__main__":
    main()