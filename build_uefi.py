#!/usr/bin/env python3

import argparse
import logging
import os
import shutil
import stat
import subprocess
import sys
import tomllib

from pathlib import Path

# Paths relative to repository root
BUILD_PATH         = Path("Build")
OUT_PATH           = Path("out")
PLATFORM_PATH      = Path("Platforms")
CONFIG_PATH        = Path("Resources/Configs")
SCRIPTS_PATH       = Path("Resources/Scripts")

sys.path.insert(0, str(SCRIPTS_PATH))
from BootImage import (  # noqa: E402
    BootImageError,
    dsc_output_directory,
    pack_boot_image,
)

logger: logging.Logger


def setup_logger():
    global logger
    logger = logging.getLogger(Path(sys.argv[0]).name)
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)

    # Resources/Scripts/BootImage.py logs under its own name; give it the same
    # handler so boot image progress shows up interleaved with ours.
    boot_image_logger = logging.getLogger("BootImage")
    boot_image_logger.addHandler(handler)
    boot_image_logger.setLevel(logging.INFO)


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


def load_device_config(device):
    """Read Resources/Configs/<device>.toml."""
    config_file = CONFIG_PATH / f"{device}.toml"
    if not config_file.is_file():
        logger.error(f'No config for "{device}": {config_file}')
        logger.error(f"expected a [uefi_fd] and a [boot_image] section naming the format")
        sys.exit(1)

    with open(config_file, "rb") as fh:
        return tomllib.load(fh)


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
    # Ask the platform DSC where its output goes rather than assuming
    # Build/<device>Pkg: not every platform uses that layout.
    dsc = PLATFORM_PATH / f"{device}Pkg" / f"{device}Pkg.dsc"
    build_dir = BUILD_PATH / f"{device}Pkg"
    if dsc.is_file():
        try:
            build_dir = Path(dsc_output_directory(dsc))
        except BootImageError as e:
            logger.warning(f"{e}, falling back to {build_dir}")

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

    config = load_device_config(device)

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

    #
    # Hand the FD geometry from the device's TOML to the build. DeviceBuild.py
    # forwards these into BLD_*_FD_*, and the platform FDF picks them up as
    # $(FD_BASE) / $(FD_SIZE). This keeps the TOML the single source of truth.
    #
    fd_config = config.get("uefi_fd") or {}
    if "base" not in fd_config or "size" not in fd_config:
        logger.error(f"{CONFIG_PATH / f'{device}.toml'}: [uefi_fd] needs both base and size")
        sys.exit(1)
    extra_args.append(f"FD_BASE={hex(fd_config['base'])}")
    extra_args.append(f"FD_SIZE={hex(fd_config['size'])}")

    rc = run_device_script(script, args.release, extra_args)
    if rc != 0:
        logger.error("Build failed")
        sys.exit(rc)

    try:
        pack_boot_image(config, device, args.release or "RELEASE",
                        Path(__file__).resolve().parent)
    except BootImageError as e:
        logger.error("Boot image creation failed")
        for line in str(e).splitlines():
            logger.error(f"    {line}")
        sys.exit(1)

if __name__ == "__main__":
    main()
