#!/usr/bin/env python3
##
# @file BootImage.py
# Boot image packing for edk2-intel-mid.
#
# Every device is described by a TOML file in Resources/Configs/<device>.toml, and this
# module turns that into a flashable image. A new format means a new BootImagePacker
# subclass registered in PACKERS; nothing else changes.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

import logging
import os
import re
import subprocess
import sys

from pathlib import Path

# Directory holding this file and the tools it drives.
SCRIPTS = Path(__file__).resolve().parent

# build_uefi.py attaches its handler here; the root logger defaults to WARNING.
logger = logging.getLogger("BootImage")

FD_NAME_RE = re.compile(r"^\s*\[FD\.([A-Za-z0-9_]+)\]\s*$")
OUTPUT_DIRECTORY_RE = re.compile(r"^\s*OUTPUT_DIRECTORY\s*=\s*(\S+)")

# Offset 0 must become a direct jmp to the SEC entry: the primary bootloader
# jumps there in 32-bit protected mode. x86 near jmp opcode.
JMP_OPCODE = b"\xe9"


class BootImageError(Exception):
    """Raised when a device cannot be packed. The message is user-facing."""


class BootImagePacker:
    """Base class for a boot image format.

    A subclass declares its ImageResources inputs and how to invoke the tool that
    assembles the format.
    """

    # Format name, as written in the [boot_image] format key of a device TOML.
    format_name = None

    # Name the FD goes by inside the assembled image: the slot flag for formats
    # that have slots, progress output for the rest.
    fd_slot = "payload"

    # Files that must exist in ImageResources. Missing is a hard error: these
    # blobs are device-specific and cannot be synthesized.
    required_inputs = ()

    # [boot_image] keys this format understands. Anything else is a config error.
    accepted_options = frozenset()

    # How to populate the required inputs from the stock image, shown when one is
    # missing. {outdir} is substituted.
    unpack_hint = None

    def assemble(self, fd_path, res_dir, out_image, options):
        """Build `out_image` from the FD at `fd_path`.

        fd_path   - Path to the patched FD
        res_dir   - Path to the device's ImageResources directory
        out_image - Path to write
        options   - the [boot_image] table from the device TOML
        """
        raise NotImplementedError

    def reject_unknown_options(self, options):
        """Raise BootImageError if `options` has a key this format cannot use."""
        unknown = set(options) - self.accepted_options
        if unknown:
            raise BootImageError(
                f'unknown [boot_image] key{_plural(unknown)} for format '
                f'"{self.format_name}": {", ".join(sorted(unknown))}'
            )

    def check_inputs(self, res_dir):
        """Verify required_inputs exist, raising BootImageError if not."""
        missing = [name for name in self.required_inputs
                   if not (res_dir / name).is_file()]
        if not missing:
            return
        lines = [f"missing boot image input{_plural(missing)} in {res_dir}: "
                 f"{', '.join(missing)}."]
        if self.unpack_hint:
            lines.append("extract them from the stock boot image once:")
            lines.append(f"    {self.unpack_hint.format(outdir=res_dir)}")
        raise BootImageError("\n".join(lines))


class OsipPacker(BootImagePacker):
    """Intel OSIP container, used by the Cloverview droidboot devices.

    The FD is the bootstub payload; hdr/sig/cmdline.txt/parameter come straight
    from the stock image. See Resources/Scripts/mkosip.py.
    """

    format_name = "osip"
    fd_slot = "bootstub"
    required_inputs = ("hdr", "sig", "cmdline.txt", "parameter")
    unpack_hint = ("python3 Resources/Scripts/unpack_osip.py boot.img "
                   "{outdir}")

    def assemble(self, fd_path, res_dir, out_image, options):
        self.reject_unknown_options(options)

        result = _run_tool("mkosip.py",
                           "-o", str(out_image),
                           "-p", str(fd_path),
                           "-d", str(res_dir),
                           expect=out_image)
        return result


class AndroidBootPacker(BootImagePacker):
    """Standard Google mkbootimg boot image (header version 0).

    The FD goes in the second bootloader slot, which the z00xs bootloader and
    any other AOSP-style bootloader loads at the FD's PcdFdBaseAddress. The
    stock kernel and ramdisk fill the remaining slots so the header stays
    well-formed. See Resources/Scripts/mkbootimg.py.
    """

    format_name = "android"
    fd_slot = "second"
    required_inputs = ("kernel", "ramdisk")
    unpack_hint = ("python3 Resources/Scripts/unpack_bootimg.py boot.img "
                   "{outdir}")

    # [boot_image] keys mapped to mkbootimg flags. An explicit table, not a loop, so
    # that a key with no matching flag is reported instead of silently dropped.
    FLAG_MAP = {
        "base": "--base",
        "cmdline": "--cmdline",
        "kernel_offset": "--kernel_offset",
        "ramdisk_offset": "--ramdisk_offset",
        "second_offset": "--second_offset",
        "tags_offset": "--tags_offset",
        "pagesize": "--pagesize",
        "header_version": "--header_version",
    }

    # Passed through in hex; the rest go as str(), since mkbootimg parses them
    # with int(x, 0) or expects plain text.
    HEX_KEYS = ("base", "kernel_offset", "ramdisk_offset",
                "second_offset", "tags_offset")

    accepted_options = frozenset(FLAG_MAP)

    def assemble(self, fd_path, res_dir, out_image, options):
        self.reject_unknown_options(options)

        cmd = ["--kernel", str(res_dir / "kernel"),
               "--ramdisk", str(res_dir / "ramdisk"),
               f"--{self.fd_slot}", str(fd_path)]

        for key, flag in self.FLAG_MAP.items():
            value = options.get(key)
            if value is None:
                continue
            cmd += [flag, hex(value) if key in self.HEX_KEYS else str(value)]

        cmd += ["-o", str(out_image)]
        return _run_tool("mkbootimg.py", *cmd, expect=out_image)


# Registry of known formats, keyed by what a device TOML's [boot_image] accepts.
PACKERS = {
    OsipPacker.format_name: OsipPacker,
    AndroidBootPacker.format_name: AndroidBootPacker,
}


def _plural(items):
    """Return the plural suffix for a message about `items`."""
    return "" if len(items) == 1 else "s"


def _run_tool(name, *args, expect=None):
    """Run one of the tools in this directory, returning its stdout.

    expect - path the tool is required to produce
    """
    tool = SCRIPTS / name
    result = subprocess.run(
        [sys.executable, str(tool), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if result.returncode != 0 or (expect is not None and not expect.is_file()):
        detail = (result.stderr or result.stdout).strip()
        raise BootImageError(f"{name} failed: {detail}")
    return result.stdout.strip()


def get_packer(format_name):
    """Return the packer class for `format_name`, or raise BootImageError."""
    if format_name not in PACKERS:
        raise BootImageError(
            f'unknown boot image format "{format_name}" '
            f'(known: {", ".join(sorted(PACKERS))})')
    return PACKERS[format_name]


def fd_name_from_fdf(fdf_path):
    """Read the [FD.*] block name out of a platform FDF."""
    with open(fdf_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            match = FD_NAME_RE.match(line)
            if match:
                return match.group(1)
    raise BootImageError(f"no [FD.*] block found in {fdf_path}")


def dsc_output_directory(dsc_path):
    """Read OUTPUT_DIRECTORY out of a platform DSC.

    Parsed rather than assumed to be Build/<device>Pkg, because that is not
    true for every platform.
    """
    with open(dsc_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            match = OUTPUT_DIRECTORY_RE.match(line)
            if match:
                return match.group(1)
    raise BootImageError(f"no OUTPUT_DIRECTORY in {dsc_path}")


def locate_fd(workspace, device, target):
    """Find the FD produced by a completed build."""
    pkg_dir = workspace / "Platforms" / f"{device}Pkg"
    dsc = pkg_dir / f"{device}Pkg.dsc"
    if not dsc.is_file():
        raise BootImageError(f"platform DSC not found: {dsc}")

    out_dir = dsc_output_directory(dsc)
    fd_name = fd_name_from_fdf(pkg_dir / f"{device}Pkg.fdf")
    fd_path = workspace / out_dir / f"{target}_CLANGPDB" / "FV" / f"{fd_name}.fd"
    if not fd_path.is_file():
        raise BootImageError(
            f"expected firmware image not produced: {fd_path}\n"
            f"    (looked under OUTPUT_DIRECTORY={out_dir})")
    return fd_path


def patch_sec_entry(fd_path):
    """Make offset 0 of the FD jump directly to the SEC entry."""
    logger.info("==> Patching SEC entry jump at offset 0")
    _run_tool("patch_sec_entry.py", str(fd_path))

    with open(fd_path, "rb") as fh:
        first_byte = fh.read(1)
    if first_byte != JMP_OPCODE:
        raise BootImageError(
            "image is not directly executable at offset 0 "
            f"(first byte {first_byte.hex()})")


def apply_post_steps(out_image, res_dir, post):
    """Apply the device's [post] section.

    The only supported key is `append`, an ordered list of ImageResources blobs
    to concatenate onto the finished image. A list rather than a single value
    because the order matters and a device may need more than one blob.
    """
    unknown = set(post) - {"append"}
    if unknown:
        raise BootImageError(
            f"unknown [post] key{_plural(unknown)}: {', '.join(sorted(unknown))} "
            "(supported: append)")

    for name in post.get("append", ()):
        src = res_dir / name
        if not src.is_file():
            raise BootImageError(f"missing blob to append: {src}")
        logger.info(f"==> Appending {name} ({os.path.getsize(src)} bytes)")
        with open(out_image, "ab") as dst, open(src, "rb") as blob:
            dst.write(blob.read())


def pack_boot_image(config, device, target, workspace):
    """Build the device's boot image and return the output path.

    config    - the parsed device TOML
    device    - device codename
    target    - build target, DEBUG or RELEASE
    workspace - path to the repository root
    """
    image_config = config.get("boot_image")
    if image_config is None:
        raise BootImageError("no [boot_image] section in the device config")

    format_name = image_config.get("format")
    if format_name is None:
        raise BootImageError("no format specified in [boot_image]")

    options = {key: value for key, value in image_config.items()
               if key != "format"}
    packer = get_packer(format_name)()

    fd_path = locate_fd(workspace, device, target)
    logger.info(f"==> FD saved as {fd_path} "
                f"({os.path.getsize(fd_path)} bytes)")

    res_dir = workspace / "Platforms" / f"{device}Pkg" / "ImageResources"
    if not res_dir.is_dir():
        raise BootImageError(f"no ImageResources directory for {device}: "
                             f"{res_dir}")
    packer.check_inputs(res_dir)

    patch_sec_entry(fd_path)

    out_dir = workspace / "out"
    out_image = out_dir / f"boot_{device}_{target}.img"
    os.makedirs(out_dir, exist_ok=True)
    if out_image.is_file():
        out_image.unlink()

    logger.info(f"==> Assembling {format_name} boot image "
                f"(FD as {packer.fd_slot} payload)")
    output = packer.assemble(fd_path, res_dir, out_image, options)
    for line in output.splitlines():
        logger.info(f"    {line}")

    apply_post_steps(out_image, res_dir, config.get("post", {}))

    logger.info(f"==> Output image saved as {out_image} "
                f"({os.path.getsize(out_image)} bytes)")
    return out_image
