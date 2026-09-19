#!/usr/bin/env python3

import argparse
import logging
import os
import shutil
import stat
import subprocess
import sys

from pathlib import Path

# Paths relative to repository root
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
        description="Build edk2-intel-mid UEFI image for a given device",
    )

    parser.add_argument("-d", "--device", type=str, default=None,
                        help="Device codename (<device codename>'Pkg')")
    parser.add_argument("-r", "--release", type=str, default=None, choices=["RELEASE", "DEBUG"],
                        help="Build mode (default: DEBUG).")
    parser.add_argument("-c", "--clean", action="store_true",
                        help="Remove old build files before building")
    parser.add_argument("-u", "--update", action="store_true",
                        help="Full workspace sync before building: git pull, git submodule update, "
                             "stuart setup, stuart update")
    parser.add_argument("--kdnet-usb", action="store_true",
                        help="Build the KDNET-over-USB debug variant")

    # Everything after '--' goes to DeviceBuild.py verbatim
    parser.add_argument("extra_args", nargs="*", metavar="ARGS",
                        help="Tokens passed through to DeviceBuild.py after '--' "
                             "(e.g. '-- KDNET_USB=1 -j 8'; KEY=VALUE overrides "
                             "platform defaults, other flags extend the build command)")

    return parser.parse_args()


def discover_platforms():
    devices = {}
    for dbuild in sorted(PLATFORM_PATH.glob("*/DeviceBuild.py")):
        pkg_dir = dbuild.parent
        pkg_name = pkg_dir.name
        device = pkg_name.removesuffix("Pkg")
        devices[device] = pkg_dir
    return devices


def resolve_device(devices, requested):
    if requested:
        if requested not in devices:
            logger.error(f'Device "{requested}" not found in tree')
            sys.exit(1)
        return requested

    logger.error("Please specify a device with -d/--device:")
    for d in sorted(devices):
        logger.error(f"  {d}")
    sys.exit(1)


def update_local_repo():
    logger.info("==> Updating local repository")
    if subprocess.run(["git", "pull"]).returncode != 0:
        logger.error("git pull failed")
        return False
    logger.info("==> Syncing git submodules")
    if subprocess.run(["git", "submodule", "update"]).returncode != 0:
        logger.error("git submodule update failed")
        return False
    return True


def prepare_workspace():
    workspace = Path(__file__).resolve().parent
    edk2_dir = workspace / "Common" / "edk2"
    patch_file = workspace / "Resources" / "edk2.patch"

    def git(*args):
        return subprocess.run(
            ["git", "-C", str(edk2_dir)] + list(args),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    if shutil.which("git") is None:
        logger.error("no git detected in PATH")
        return 1

    synced = subprocess.run(
        ["git", "-C", str(workspace), "submodule", "update", "--init", "Common/edk2"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if synced.returncode != 0:
        logger.error("failed to initialize Common/edk2")
        return 1

    if git("apply", "--reverse", "--check", str(patch_file)).returncode == 0:
        logger.info("==> edk2.patch already applied -> skipping")
    elif git("apply", "--check", str(patch_file)).returncode == 0:
        logger.info("==> Applying edk2.patch")
        if git("apply", str(patch_file)).returncode != 0:
            logger.error("git apply failed to apply edk2.patch")
            return 1
        logger.info("    applied (kept uncommitted in the edk2 working tree)")
    elif git("apply", "--3way", "--check", str(patch_file)).returncode == 0:
        logger.info("==> Applying edk2.patch (3-way, context drifted)")
        if git("apply", "--3way", str(patch_file)).returncode != 0:
            logger.error("git apply --3way failed to apply edk2.patch")
            return 1
    else:
        logger.error("cannot apply edk2.patch to the edk2 working tree")
        logger.error(f'    to reset a dirty checkout and retry: git -C "{edk2_dir}" reset --hard')
        return 1

    logger.info("==> Initializing required edk2 submodules")
    r = git(
        "submodule",
        "update",
        "--init",
        "BaseTools/Source/C/BrotliCompress/brotli",
        "MdePkg/Library/MipiSysTLib/mipisyst",
        "MdeModulePkg/Library/BrotliCustomDecompressLib/brotli",
    )
    if r.returncode != 0:
        logger.error(f"failed to initialize the required edk2 submodules in {edk2_dir}")
        return 1

    genfv = edk2_dir / "BaseTools" / "Source" / "C" / "bin" / ("GenFv.exe" if os.name == "nt" else "GenFv")
    if not genfv.is_file() or not os.access(genfv, os.X_OK):
        logger.info("==> Building BaseTools")
        make = shutil.which("make")
        if make is None:
            logger.error("make not detected in PATH")
            return 1
        r = subprocess.run(
            [make, "-C", str(edk2_dir / "BaseTools"), "-j", str(os.cpu_count() or 1)]
        )
        if r.returncode != 0:
            logger.error("BaseTools build failed")
            return 1

    return 0


def _rmtree_onerror(func, path, exc_info):
    os.chmod(path, stat.S_IWRITE)
    func(path)


def clean_build(device):
    pkg_name = f"{device}Pkg"
    build_dir = BUILD_PATH / pkg_name
    if build_dir.is_dir():
        logger.info(f"==> Removing {build_dir}")
        shutil.rmtree(build_dir, onerror=_rmtree_onerror)

    for img in OUT_PATH.glob(f"boot_{device}_*.img"):
        logger.info(f"==> Removing {img}")
        img.unlink()


def run_device_script(script_path, build_mode, extra_args):
    cmd = [sys.executable, str(script_path)]
    if build_mode:
        cmd.append(f"TARGET={build_mode}")
    cmd.extend(extra_args)
    logger.info(f"==> {' '.join(cmd)}")
    return subprocess.run(cmd).returncode


def main():
    os.chdir(Path(__file__).resolve().parent)

    setup_logger()
    args = parse_arguments()

    devices = discover_platforms()
    device = resolve_device(devices, args.device)
    pkg_dir = devices[device]
    script = pkg_dir / "DeviceBuild.py"

    if not script.is_file():
        logger.error(f"DeviceBuild.py not found at {script}")
        sys.exit(1)

    if args.update:
        if not update_local_repo():
            sys.exit(1)

    if args.clean:
        clean_build(device)

    build_dir = BUILD_PATH / f"{device}Pkg"
    if args.update or not build_dir.is_dir():
        logger.info("==> stuart setup + update")
        for action in ["--setup", "--update"]:
            if subprocess.run([sys.executable, str(script), action]).returncode != 0:
                logger.error(f"{action} failed")
                sys.exit(1)

    if prepare_workspace() != 0:
        logger.error("workspace preparation failed")
        sys.exit(1)

    extra_args = list(args.extra_args)
    if args.kdnet_usb:
        extra_args.append("KDNET_USB=1")

    rc = run_device_script(script, args.release, extra_args)
    if rc != 0:
        logger.error("Build failed")
        sys.exit(rc)

if __name__ == "__main__":
    main()
