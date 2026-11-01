#!/usr/bin/env python3

import argparse
import logging
import os
import re
import shutil
import stat
import subprocess
import sys

from pathlib import Path

# python 3.10 compat
try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib

#
# Logger
#
logger: logging.Logger

def setup_logger():
    global logger

    # Create Logger
    logger = logging.getLogger(Path(sys.argv[0]).name)

    # Set Logger Attributes
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)

#
# Default Paths
#
BUILD_PATH    = Path("Build")
OUT_PATH      = Path("out")
PLATFORM_PATH = Path("Platforms")
CONFIG_PATH   = Path("Resources/Configs")
SCRIPTS_PATH  = Path("Resources/Scripts")

#
# Boot Image
#
FD_NAME_RE          = re.compile(r"^\s*\[FD\.([A-Za-z0-9_]+)\]\s*$")
OUTPUT_DIRECTORY_RE = re.compile(r"^\s*OUTPUT_DIRECTORY\s*=\s*(\S+)")

# Offset 0 must become a direct jmp to the SEC entry
JMP_OPCODE = b"\xe9"

class BootImageError(Exception):
    pass

class BootImagePacker:
    # Base class for a boot image format: declares its ImageResources inputs
    # and how to assemble the format

    # Format name, as written in the [boot_image] key of a device TOML
    format_name = None

    # Name the FD goes by inside the assembled image: the slot flag for formats
    # that have slots, progress output for the rest
    fd_slot = "payload"

    # Files that must exist in ImageResources
    required_inputs = ()

    # [boot_image] keys the chosen image format accepts
    accepted_options = frozenset()

    # help output that hints on how to populate the device's ImageResources
    unpack_hint = None

    def assemble(self, fd_path, res_dir, out_image, options):
        # Assemble out_image from the patched FD and the [boot_image] table
        raise NotImplementedError

    def reject_unknown_options(self, options):
        # Raise BootImageError if options has a key this format cannot use
        unknown = set(options) - self.accepted_options
        if unknown:
            raise BootImageError(
                f'unknown [boot_image] key{_plural(unknown)} for format '
                f'"{self.format_name}": {", ".join(sorted(unknown))}'
            )

    def check_inputs(self, res_dir):
        # Verify required_inputs exist
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
    # Intel OSIP format image: The FD replaces the Intel bootstub,
    # skipping the kernel and ramdisk

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
    # Google AOSP format image, header v0
    # The FD goes in the second bootloader slot, loaded at PcdFdBaseAddress

    format_name = "android"
    fd_slot = "second"
    required_inputs = ("kernel", "ramdisk")
    unpack_hint = ("python3 Resources/Scripts/unpack_bootimg.py boot.img "
                   "{outdir}")

    # [boot_image] keys mapped to mkbootimg.py flags
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

    # Other keys that must be passed through in hex
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

# Registry of known image formats (what a device TOML's [boot_image] accepts)
PACKERS = {
    OsipPacker.format_name: OsipPacker,
    AndroidBootPacker.format_name: AndroidBootPacker,
}

#
# Boot Image Helpers
#
def _plural(items):
    # Return the plural suffix for a message about items
    return "" if len(items) == 1 else "s"

def _run_tool(name, *args, expect=None):
    # Run a tool in Resources/Scripts and return its stdout
    tool = Path(__file__).resolve().parent / SCRIPTS_PATH / name
    result = subprocess.run(
        [sys.executable, str(tool), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if result.returncode != 0 or (expect is not None and not expect.is_file()):
        detail = (result.stderr or result.stdout).strip()
        raise BootImageError(f"{name} failed: {detail}")
    return result.stdout.strip()

def get_packer(format_name):
    # Return the packer class for format_name, or raise BootImageError
    if format_name not in PACKERS:
        raise BootImageError(
            f'unknown boot image format "{format_name}" '
            f'(known: {", ".join(sorted(PACKERS))})')
    return PACKERS[format_name]

def fd_name_from_fdf(fdf_path):
    # Read the [FD.*] block name out of a platform FDF
    with open(fdf_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            match = FD_NAME_RE.match(line)
            if match:
                return match.group(1)
    raise BootImageError(f"no [FD.*] block found in {fdf_path}")

def dsc_output_directory(dsc_path):
    # Read OUTPUT_DIRECTORY out of a DSC
    with open(dsc_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            match = OUTPUT_DIRECTORY_RE.match(line)
            if match:
                return match.group(1)
    raise BootImageError(f"no OUTPUT_DIRECTORY in {dsc_path}")

def locate_fd(workspace, device, target):
    # Find the FD produced by a completed build
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
    # patch_sec_entry's purpose is to make offset 0 of the FD
    # jump directly to the SEC entrypoint
    logger.info("==> Patching SEC entry jump at offset 0")
    _run_tool("patch_sec_entry.py", str(fd_path))

    with open(fd_path, "rb") as fh:
        first_byte = fh.read(1)
    if first_byte != JMP_OPCODE:
        raise BootImageError(
            "image is not directly executable at offset 0 "
            f"(first byte {first_byte.hex()})")

def apply_post_steps(out_image, res_dir, post):
    # Apply the [post] section of the toml, ex. [append]
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
    # Build the device's boot image from its TOML and return the output path
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
    out_image = out_dir / f"uefi_{device}_{target}.img"
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

#
# Build Helpers
#
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
                        help="Options passed through to DeviceBuild.py after '--' "
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
    # Read Resources/Configs/<device>.toml
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
    if subprocess.run(["git", "submodule", "update", "--init", "--recursive"]).returncode != 0:
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

    if base_tools_bin_dir(edk2_dir) is None:
        if build_base_tools(edk2_dir) != 0:
            return 1

    return 0

def base_tools_bin_dir(edk2_dir):
    # Return the folder Edk2ToolsBuild.py put the compiled tools in, or None:
    # Bin/Win32 or Bin/Win64 under Visual Studio, Source/C/bin otherwise
    name = "GenFv.exe" if os.name == "nt" else "GenFv"
    if os.name == "nt":
        folders = (("Bin", "Win32"), ("Bin", "Win64"))
    else:
        folders = (("Source", "C", "bin"),)
    for parts in folders:
        folder = edk2_dir / "BaseTools" / Path(*parts)
        if (folder / name).is_file():
            return folder
    return None

def build_base_tools(edk2_dir):
    # Build BaseTools through Edk2ToolsBuild.py
    tool = edk2_dir / "BaseTools" / "Edk2ToolsBuild.py"
    if not tool.is_file():
        logger.error(f"BaseTools entry point not found: {tool}")
        return 1

    tags = ("VS2026", "VS2022", "VS2019") if os.name == "nt" else ("GCC",)
    for tag in tags:
        logger.info(f"==> Building BaseTools ({tag})")
        if subprocess.run([sys.executable, str(tool), "-t", tag]).returncode == 0:
            return 0
    logger.error("BaseTools build failed")
    return 1

def _remove_readonly(func, path, exc_info):
    # Windows refuses to delete read-only files; clear the bit and retry
    os.chmod(path, stat.S_IWRITE)
    func(path)

def clean_build(device):
    # Read build output path from dsc
    dsc = PLATFORM_PATH / f"{device}Pkg" / f"{device}Pkg.dsc"
    build_dir = BUILD_PATH / f"{device}Pkg"
    if dsc.is_file():
        try:
            build_dir = Path(dsc_output_directory(dsc))
        except BootImageError as e:
            logger.warning(f"{e}, falling back to {build_dir}")

    if build_dir.is_dir():
        logger.info(f"==> Removing {build_dir}")
        # onexc replaced onerror in 3.12; both take the same callback shape.
        if sys.version_info >= (3, 12):
            shutil.rmtree(build_dir, onexc=_remove_readonly)
        else:
            shutil.rmtree(build_dir, onerror=_remove_readonly)

    for img in OUT_PATH.glob(f"uefi_{device}_*.img"):
        logger.info(f"==> Removing {img}")
        img.unlink()

def run_device_script(script_path, build_mode, extra_args):
    cmd = [sys.executable, str(script_path)]
    if build_mode:
        cmd.append(f"TARGET={build_mode}")
    cmd.extend(extra_args)
    logger.info(f"==> {' '.join(cmd)}")
    return subprocess.run(cmd).returncode

#
# Main
#
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

    # Read FD geometry from the TOML
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
