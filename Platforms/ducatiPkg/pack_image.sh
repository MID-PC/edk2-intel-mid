#!/usr/bin/env bash
#
# Pack the ducatiPkg (Acer Iconia A1-830, Intel Atom Z25xx) firmware into the
# stock OSIP boot image. Called by the repository build.sh after the edk2
# build produced the FD; this is where device-specific boot image handling
# lives.
#
# The generic pieces live in Resources/Scripts:
#   patch_sec_entry.py - direct-enter entry jump at FD offset 0
#   mkosip.py          - OSIP image assembly
# and the device's hdr/sig/cmdline/parameter bits are committed here in
# ImageResources/ (extracted once from the stock boot image while porting
# with Resources/Scripts/unpack_osip.py).
#
# Usage:  pack_image.sh <fd_path> <target>
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

set -euo pipefail

FD_PATH="${1:?fd path required}"
TARGET="${2:?target required}"

#------------------------------------------------------------------------------
# Paths. pack_image.sh lives in Platforms/<device>Pkg.
#------------------------------------------------------------------------------
PKG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$PKG_DIR/../.." && pwd)"
DEVICE="$(basename "$PKG_DIR")"     # ducatiPkg
DEVICE="${DEVICE%Pkg}"              # ducati

OUT_DIR="$ROOT_DIR/out"
OUT_IMAGE="$OUT_DIR/boot_${DEVICE}_${TARGET}.img"
OSIP_DIR="$PKG_DIR/ImageResources"

#------------------------------------------------------------------------------
# Make the image directly executable at offset 0: the FD starts with the FVSEC
# firmware volume header, whose first 16 bytes are the (unused) zero vector.
# Patch a 'jmp rel32' there so that entering the binary at its very first byte
# lands on SecEntry.
#------------------------------------------------------------------------------
echo "==> Patching SEC entry jump at offset 0"
python3 "$ROOT_DIR/Resources/Scripts/patch_sec_entry.py" "$FD_PATH"

FIRST_BYTE="$(od -An -tx1 -N1 "$FD_PATH" | tr -d ' \n')"
if [ "$FIRST_BYTE" != "e9" ]; then
  echo "error: image is not directly executable at offset 0" >&2
  exit 1
fi

for f in hdr sig cmdline.txt parameter; do
  [ -f "$OSIP_DIR/$f" ] || {
    echo "error: missing $OSIP_DIR/$f" >&2
    echo "        unpack the stock boot image once while porting:" >&2
    echo "        python3 Resources/Scripts/unpack_osip.py boot.img $OSIP_DIR" >&2
    exit 1
  }
done

#------------------------------------------------------------------------------
# Assemble the OSIP image with our firmware as the bootstub.
#
# The stock kernel/ramdisk slots are deliberately not carried: this device
# never jumps to a Linux kernel image out of the boot image - the bootloader
# enters the bootstub directly, so those slots only bloat the image.  mkosip
# places the payload (our firmware) at base+0x1000 with zero-size kernel and
# ramdisk, which is exactly what the stock flow produced after the (skipped)
# kernel/ramdisk.
#------------------------------------------------------------------------------
mkdir -p "$OUT_DIR"
rm -f "$OUT_IMAGE"

echo "==> Assembling OSIP image with mkosip"
python3 "$ROOT_DIR/Resources/Scripts/mkosip.py" -o "$OUT_IMAGE" -p "$FD_PATH" -d "$OSIP_DIR"

if [ ! -f "$OUT_IMAGE" ]; then
  echo "error: mkosip did not produce $OUT_IMAGE" >&2
  exit 1
fi

echo
echo "==> Done"
echo "    firmware : $FD_PATH"
echo "    boot image: $OUT_IMAGE ($(stat -c%s "$OUT_IMAGE") bytes)"
echo
echo "    Flash with:  fastboot flash boot $OUT_IMAGE"
