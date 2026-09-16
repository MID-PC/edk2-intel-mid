#!/usr/bin/env python3
## SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Root-level build orchestrator for this multi-device firmware repository.
#
# Mirrors the interface convention of Project Silicium's build_uefi.py
# while delegating all edk2/pack logic to each device's per-device
# Platforms/<device>Pkg/DeviceBuild.py.  Devices are auto-discovered by
# globbing Platforms/*/DeviceBuild.py.
#
# Usage:
#   python3 build_uefi.py -d ducati                     # DEBUG (default)
#   python3 build_uefi.py -d t00g -r RELEASE
#   python3 build_uefi.py -d t00k --kdnet-usb           # KDNET-over-USB variant
#   python3 build_uefi.py -d ducati -c                  # clean + build
#   python3 build_uefi.py -d ducati -u                  # full sync + build:
#       git pull + git submodule update + stuart setup + stuart update
#   python3 build_uefi.py -d t00g -- KDNET_USB=1 -j 8   # extra args after '--'
#

import argparse
import logging
import os
import shutil
import stat
import subprocess
import sys

from pathlib import Path

#
# Paths (relative to repository root)
#
BUILD_PATH      = Path("Build")
OUT_PATH        = Path("out")
PLATFORM_PATH   = Path("Platforms")

logger: logging.Logger


def setup_logger():
    global logger
    logger = logging.getLogger(Path(sys.argv[0]).name)
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Build UEFI firmware for this repository's devices (e.g. ducati, t00g, t00k).",
    )

    parser.add_argument("-d", "--device", type=str, default=None,
                        help="Device codename (package dir minus 'Pkg').  Auto-detected if only one platform exists.")
    parser.add_argument("-r", "--release", type=str, default=None, choices=["RELEASE", "DEBUG"],
                        help="Build mode (default: DEBUG).")
    parser.add_argument("-c", "--clean", action="store_true",
                        help="Remove old build files before building.")
    parser.add_argument("-u", "--update", action="store_true",
                        help="Full workspace sync before building: git pull, git submodule update, "
                             "stuart setup, stuart update.")
    parser.add_argument("--kdnet-usb", action="store_true",
                        help="Build the KDNET-over-USB debug variant.")

    # Everything after '--' is forwarded to DeviceBuild.py verbatim.
    parser.add_argument("extra_args", nargs="*", metavar="ARGS",
                        help="Tokens passed through to DeviceBuild.py after '--' "
                             "(e.g. '-- KDNET_USB=1 -j 8'; KEY=VALUE overrides "
                             "platform defaults, other flags extend the build command).")

    return parser.parse_args()


def discover_platforms():
    """Return a dict mapping device name -> Platforms/<pkg> path for all
    platforms that have a DeviceBuild.py."""
    devices = {}
    for dbuild in sorted(PLATFORM_PATH.glob("*/DeviceBuild.py")):
        pkg_dir = dbuild.parent                   # Platforms/ducatiPkg
        pkg_name = pkg_dir.name                   # ducatiPkg
        device = pkg_name.removesuffix("Pkg")     # ducati
        devices[device] = pkg_dir
    return devices


def resolve_device(devices, requested):
    """Pick the device to build.  If only one platform exists, auto-detect."""
    if requested:
        if requested not in devices:
            logger.error(f'Unknown device "{requested}".  Available: {", ".join(devices)}')
            sys.exit(1)
        return requested

    if len(devices) == 1:
        device = next(iter(devices))
        logger.info(f"Auto-detected device: {device}")
        return device

    logger.error("Multiple devices found.  Please specify one with -d/--device:")
    for d in sorted(devices):
        logger.error(f"  {d}")
    sys.exit(1)


def update_local_repo():
    """Pull latest changes and sync git submodules.

    'git submodule update' is safe for this workspace: Common/edk2 is a
    gitlink kept as a manual checkout with edk2.patch applied in its working
    tree, and git only touches submodules whose checked-out commit differs
    from the gitlink pointer.  Since Common/edk2 sits at the recorded commit,
    the update is a no-op and the uncommitted patch changes survive untouched.
    Nested submodule work (the brotli bindings inside Common/edk2) is managed
    by DeviceBuild.py's workspace preparation.
    """
    logger.info("==> Updating local repository")
    if subprocess.run(["git", "pull"]).returncode != 0:
        logger.error("git pull failed")
        return False
    logger.info("==> Syncing git submodules")
    if subprocess.run(["git", "submodule", "update"]).returncode != 0:
        logger.error("git submodule update failed")
        return False
    return True


def _rmtree_onerror(func, path, exc_info):
    """shutil.rmtree onerror handler: on Windows build outputs are sometimes
    read-only; clear the attribute and retry before giving up."""
    os.chmod(path, stat.S_IWRITE)
    func(path)


def clean_build(device):
    """Remove build artifacts for the given device only.

    Only that device's Build/<device>Pkg tree and its output images are
    removed; other devices' images in the shared out/ directory are kept.
    """
    pkg_name = f"{device}Pkg"
    build_dir = BUILD_PATH / pkg_name
    if build_dir.is_dir():
        logger.info(f"==> Removing {build_dir}")
        shutil.rmtree(build_dir, onerror=_rmtree_onerror)

    # Remove this device's output images (shared out/ is left otherwise intact)
    for img in OUT_PATH.glob(f"boot_{device}_*.img"):
        logger.info(f"==> Removing {img}")
        img.unlink()


def run_device_script(script_path, build_mode, extra_args):
    """Run DeviceBuild.py with the given arguments.  Returns the exit code."""
    cmd = [sys.executable, str(script_path)]
    if build_mode:
        cmd.append(f"TARGET={build_mode}")
    cmd.extend(extra_args)
    logger.info(f"==> {' '.join(cmd)}")
    return subprocess.run(cmd).returncode


def main():
    # Pin CWD to the repository root: everything below (git pull, platform
    # discovery, Build/, out/, DeviceBuild.py invocations) is root-relative
    # and must resolve regardless of where this script was launched from.
    os.chdir(Path(__file__).resolve().parent)

    setup_logger()
    args = parse_arguments()

    # Discover and resolve device
    devices = discover_platforms()
    device = resolve_device(devices, args.device)
    pkg_dir = devices[device]
    script = pkg_dir / "DeviceBuild.py"

    if not script.is_file():
        logger.error(f"DeviceBuild.py not found at {script}")
        sys.exit(1)

    # -- full workspace sync: git pull + submodule update, then stuart setup/update
    if args.update:
        if not update_local_repo():
            sys.exit(1)

    # -- clean
    if args.clean:
        clean_build(device)

    # -- stuart setup + update: always on --update (full sync), otherwise only
    #    on a first build (no Build/<device>Pkg tree yet)
    build_dir = BUILD_PATH / f"{device}Pkg"
    if args.update or not build_dir.is_dir():
        logger.info("==> stuart setup + update")
        for action in ["--setup", "--update"]:
            if subprocess.run([sys.executable, str(script), action]).returncode != 0:
                logger.error(f"{action} failed")
                sys.exit(1)

    # -- build
    extra_args = list(args.extra_args)
    if args.kdnet_usb:
        extra_args.append("KDNET_USB=1")

    rc = run_device_script(script, args.release, extra_args)
    if rc != 0:
        logger.error("Build failed")
        sys.exit(rc)

    # -- summary (default target is DEBUG, enforced in SetPlatformEnv)
    image = OUT_PATH / f"boot_{device}_{args.release or 'DEBUG'}.img"
    if image.is_file():
        logger.info("")
        logger.info(f"  Output: {image} ({image.stat().st_size:,} bytes)")
        logger.info(f"  Flash:  fastboot flash boot {image}")
    logger.info("")


if __name__ == "__main__":
    main()
