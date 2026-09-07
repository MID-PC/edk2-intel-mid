#!/usr/bin/env bash
#
# One-time environment setup for the firmware tree.
#
# Prepares an edk2 checkout (cloned manually, or by this script with
# SETUP_CLONE=1) so that ./build.sh <device> [DEBUG|RELEASE] is ready to run:
#   * applies Resources/edk2.patch (Clover Trail/UEFI bring-up: EHCI Chipidea
#                             host-mode re-assert, SdMmc/PCI, GCD, graphics
#                             console, CR0.WP/CR3 page-table fixes, MP init)
#   * builds BaseTools        (build.sh rebuilds on demand; doing it here
#                             surfaces tool chain problems up front)
#
# The patch is applied with 'git apply' onto the edk2 checkout and leaves it
# as an uncommitted working-tree change. Running this script again is safe:
# an already-applied patch is detected and skipped.
#
# Usage:  ./setup_env.sh
#
# Optional (environment):
#   EDK2_DIR      path to the edk2 checkout (default: ./Common/edk2)
#   SETUP_CLONE=1 clone edk2 into EDK2_DIR first if it does not exist yet
#   EDK2_COMMIT   commit to leave the clone at (default: the commit the
#                 patch is authored against, checked out when cloning)
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

set -euo pipefail

error() {
  echo "error: $*" >&2
  exit 1
}

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDK2_DIR="${EDK2_DIR:-$ROOT_DIR/Common/edk2}"
PATCH_FILE="$ROOT_DIR/Resources/edk2.patch"
REF_COMMIT="fc939c7b37"

[ -f "$PATCH_FILE" ] || error "$PATCH_FILE not found"

#------------------------------------------------------------------------------
# edk2 checkout
#------------------------------------------------------------------------------
if [ ! -d "$EDK2_DIR/.git" ]; then
  if [ -n "${SETUP_CLONE:-}" ]; then
    echo "==> Cloning edk2 into $EDK2_DIR"
    git clone https://github.com/tianocore/edk2 "$EDK2_DIR"
    git -C "$EDK2_DIR" checkout "${EDK2_COMMIT:-$REF_COMMIT}"
  else
    error "$EDK2_DIR not found"
    echo "    clone it manually, e.g.:  git clone https://github.com/tianocore/edk2 Common/edk2" >&2
    echo "    (this patch is against edk2 $REF_COMMIT)" >&2
  fi
fi

git -C "$EDK2_DIR" rev-parse --is-inside-work-tree >/dev/null \
  || error "$EDK2_DIR is not a git work tree"

HEAD_SHORT="$(git -C "$EDK2_DIR" rev-parse --short HEAD)"
echo "==> edk2 @ $HEAD_SHORT ($EDK2_DIR)"

#------------------------------------------------------------------------------
# edk2.patch
#------------------------------------------------------------------------------
if git -C "$EDK2_DIR" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "==> edk2.patch already applied -> skipping"
elif git -C "$EDK2_DIR" apply --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "==> Applying edk2.patch"
  git -C "$EDK2_DIR" apply "$PATCH_FILE"
  echo "    applied (kept uncommitted in the edk2 working tree)"
elif git -C "$EDK2_DIR" apply --3way --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "==> Applying edk2.patch (3-way, context drifted)"
  git -C "$EDK2_DIR" apply --3way "$PATCH_FILE"
else
  error "cannot apply edk2.patch to the $HEAD_SHORT working tree"
  echo "    the patch is authored against edk2 $REF_COMMIT." >&2
  echo "    to reset a dirty checkout and retry:" >&2
  echo "      git -C \"$EDK2_DIR\" checkout -- ." >&2
fi

#------------------------------------------------------------------------------
# Submodules (BaseTools brotli bindings)
#------------------------------------------------------------------------------
# edk2's BaseTools build fails without the libbrotli sources, which live in a
# git submodule that a plain clone leaves empty. Only the two brotli bindings
# are needed here; CryptoPkg (openssl/mbedtls) and the oniguruma regex bindings
# are only fetched on demand because this platform does not reference them.
echo "==> Initializing brotli submodules"
git -C "$EDK2_DIR" submodule update --init \
  BaseTools/Source/C/BrotliCompress/brotli \
  MdeModulePkg/Library/BrotliCustomDecompressLib/brotli

#------------------------------------------------------------------------------
# BaseTools
#------------------------------------------------------------------------------
# Always run make: it is a fast no-op when the tools are already built, and it
# heals a tree whose previous BaseTools build aborted part-way (gating on a
# single binary such as GenFv would hide a partial build).
echo "==> Building BaseTools (no-op if already built)"
make -C "$EDK2_DIR/BaseTools" -j"$(nproc)"

echo "==> Environment ready: ./build.sh <device> [DEBUG|RELEASE]"