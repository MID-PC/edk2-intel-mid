#!/usr/bin/env python3

import argparse
import subprocess
import sys

from pathlib import Path

ROOT = Path(__file__).resolve().parent
CONFIG_PATH = ROOT / "Resources" / "Configs"
PLATFORM_PATH = ROOT / "Platforms"
BUILD_SCRIPT = ROOT / "build_uefi.py"


def discover_devices():
    devices = []
    for config in sorted(CONFIG_PATH.glob("*.toml")):
        device = config.stem
        device_build = PLATFORM_PATH / f"{device}Pkg" / "DeviceBuild.py"
        if not device_build.is_file():
            raise SystemExit(
                f'device config "{config.relative_to(ROOT)}" has no matching '
                f'platform build script: {device_build.relative_to(ROOT)}'
            )
        devices.append(device)

    if not devices:
        raise SystemExit(
            f"No device configs found in {CONFIG_PATH.relative_to(ROOT)}"
        )
    return devices


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=(
            "Build UEFI images for every device in the tree"
        ),
    )
    parser.add_argument(
        "-r", "--release", "--target", dest="target",
        choices=("RELEASE", "DEBUG"), default="RELEASE",
        help="Build target (default: RELEASE)",
    )
    parser.add_argument(
        "-c", "--clean", action="store_true",
        help="Remove old build files",
    )
    parser.add_argument(
        "--list", action="store_true",
        help="Return a list of existing devices (no build)",
    )
    parser.add_argument(
        "extra_args", nargs="*", metavar="ARGS",
        help="Options passed through to build_uefi.py",
    )
    args = parser.parse_args()
    return args


def build_device(device, args):
    cmd = [sys.executable, str(BUILD_SCRIPT), "-d", device,
           "-r", args.target]
    if args.clean:
        cmd.append("-c")
    if args.extra_args:
        cmd.extend(["--", *args.extra_args])

    return subprocess.run(cmd, cwd=ROOT).returncode


def main():
    args = parse_arguments()
    devices = discover_devices()

    if args.list:
        print("\n".join(devices))
        return 0

    failures = []
    for device in devices:
        print(f"==> Building {device} ({args.target})", flush=True)
        if build_device(device, args) != 0:
            failures.append(device)

    if failures:
        print(
            f"Failed to build: {', '.join(failures)}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
