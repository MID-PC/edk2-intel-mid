#!/usr/bin/env bash
#
# Build a device firmware from the Platforms/ directory and produce its
# flashable boot image.
#
# Usage:  ./build.sh <device> [DEBUG|RELEASE] [edk2 build args...]
#
#   <device>  Codename of the platform to build; 'ducati' builds
#             Platforms/ducatiPkg. Any extra arguments are passed to the
#             edk2 'build' command.
#
# Optional (environment):
#   KDNET_USB=1  build the KDNET-over-USB variant (see ducatiPkg.dsc)
#   TOOLCHAIN    edk2 toolchain tag to use (default: GCC)
#   ARCH         build architecture (default: device DSC SUPPORTED_ARCHITECTURES,
#                first entry; IA32 if unset there)
#
# After the edk2 build produces the FD, the device's own pack_image.sh
# assembles the boot image. Without one, the FD is the only output.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

set -euo pipefail

#------------------------------------------------------------------------------
# Argument handling
#------------------------------------------------------------------------------
if [ $# -lt 1 ]; then
  echo "usage: $0 <device> [DEBUG|RELEASE] [edk2 build args...]" >&2
  exit 1
fi

DEVICE="${1,,}"
shift

TARGET="${1:-DEBUG}"
if [ "$TARGET" = "DEBUG" ] || [ "$TARGET" = "RELEASE" ]; then
  shift
else
  TARGET="DEBUG"
fi

PLATFORM_NAME="${DEVICE}Pkg"
PKG_DIR="Platforms/$PLATFORM_NAME"
DSC_FILE="$PKG_DIR/$PLATFORM_NAME.dsc"
FDF_FILE="$PKG_DIR/$PLATFORM_NAME.fdf"

#------------------------------------------------------------------------------
# Paths.
#------------------------------------------------------------------------------
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="$ROOT_DIR/$PKG_DIR"
DSC_FILE="$ROOT_DIR/$DSC_FILE"
FDF_FILE="$ROOT_DIR/$FDF_FILE"

EDK2_DIR="$ROOT_DIR/Common/edk2"
WANT_TOOLCHAIN="${TOOLCHAIN:-GCC}"

if [ ! -d "$PKG_DIR" ]; then
  echo "error: no platform '$DEVICE' (looked for $PLATFORM_NAME under Platforms/)" >&2
  echo "available:" >&2
  ls -d "$ROOT_DIR"/Platforms/*Pkg >&2
  exit 1
fi
[ -f "$DSC_FILE" ] || { echo "error: $DSC_FILE not found" >&2; exit 1; }
[ -f "$FDF_FILE" ] || { echo "error: $FDF_FILE not found" >&2; exit 1; }
[ -d "$EDK2_DIR" ] || { echo "error: $EDK2_DIR not found" >&2; exit 1; }

#------------------------------------------------------------------------------
# Architecture. Taken from the device DSC's SUPPORTED_ARCHITECTURES (first
# entry wins); IA32 remains the fallback so older/multi-arch DSCs keep the
# historic behaviour. An explicit ARCH= env var wins over both.
#------------------------------------------------------------------------------
BUILD_ARCH="$(sed -nE 's/^[[:space:]]*SUPPORTED_ARCHITECTURES[[:space:]]*=[[:space:]]*([A-Za-z0-9_]+).*/\1/p' "$DSC_FILE" | head -n1)"
BUILD_ARCH="${ARCH:-${BUILD_ARCH:-IA32}}"

echo "==> $PLATFORM_NAME build ($TARGET, $WANT_TOOLCHAIN, $BUILD_ARCH)"

#------------------------------------------------------------------------------
# EDK2 environment
#------------------------------------------------------------------------------
export WORKSPACE="$ROOT_DIR"
export PACKAGES_PATH="$ROOT_DIR:$EDK2_DIR"
export EDK_TOOLS_PATH="$EDK2_DIR/BaseTools"
export CONF_PATH="$ROOT_DIR/Conf"

mkdir -p "$CONF_PATH"

export PYTHON_COMMAND="${PYTHON_COMMAND:-$(command -v python3)}"

pushd "$EDK2_DIR" >/dev/null
# edksetup.sh references unset variables; relax 'nounset'/'errexit' while sourcing.
set +u +e
# shellcheck disable=SC1091
source ./edksetup.sh BaseTools >/dev/null
set -u -e
popd >/dev/null

if [ ! -x "$EDK_TOOLS_PATH/Source/C/bin/GenFv" ]; then
  echo "==> Building BaseTools"
  make -C "$EDK_TOOLS_PATH" -j"$(nproc)"
fi

#------------------------------------------------------------------------------
# Optional KDNET-over-USB variant.
#
# When KDNET_USB is set (non-empty), the build selects the KDNET USB debug
# variant instead of the default host-mode USB:
#   * DSDT OTG0 resolves to usb-debug.asl (no _CID PNP0D20, no host bring-up)
#   * a DBG2 table for the OTG0 USB debug port is installed
#   * OtgHostDxe (and the EHCI host stack) is left out of the image
# Use:  KDNET_USB=1 ./build.sh ducati DEBUG
#------------------------------------------------------------------------------
KDNET_FLAGS=""
if [ -n "${KDNET_USB:-}" ]; then
  KDNET_FLAGS="-D KDNET_USB=1"
  echo "==> KDNET-over-USB variant enabled"
fi

#------------------------------------------------------------------------------
# Build
#------------------------------------------------------------------------------
echo "==> Running edk2 build"
build \
  -a "$BUILD_ARCH" \
  -t "$WANT_TOOLCHAIN" \
  -b "$TARGET" \
  -p "$DSC_FILE" \
  -n "$(nproc)" \
  $KDNET_FLAGS \
  "$@"

#------------------------------------------------------------------------------
# Locate the produced FD (named after the [FD.*] block in the platform FDF)
#------------------------------------------------------------------------------
FD_NAME="$(sed -nE 's/^\[FD\.([A-Za-z0-9_]+)\].*/\1/p' "$FDF_FILE" | head -n1)"
if [ -z "$FD_NAME" ]; then
  echo "error: no [FD.*] block in $FDF_FILE" >&2
  exit 1
fi

FD_PATH="$ROOT_DIR/Build/$PLATFORM_NAME/${TARGET}_${WANT_TOOLCHAIN}/FV/$FD_NAME.fd"
if [ ! -f "$FD_PATH" ]; then
  echo "error: expected firmware image not produced: $FD_PATH" >&2
  exit 1
fi

echo "==> Firmware image: $FD_PATH ($(stat -c%s "$FD_PATH") bytes)"

#------------------------------------------------------------------------------
# Device-specific boot image packing (patching/repack/OSIP assembly)
#------------------------------------------------------------------------------
PACK_SCRIPT="$PKG_DIR/pack_image.sh"
if [ -f "$PACK_SCRIPT" ]; then
  bash "$PACK_SCRIPT" "$FD_PATH" "$TARGET"
else
  echo
  echo "==> No pack_image.sh in $PLATFORM_NAME; nothing further to produce."
  echo "    firmware : $FD_PATH"
fi