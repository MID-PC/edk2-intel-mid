##
# @file DeviceBuild.py
# Asus Zenfone Zoom (ZX551ML / z00xs) - 64-bit Moorefield (Atom Z3580/Z3590)
#
# Unlike the Cloverview droidboot devices, this board's bootloader consumes a
# standard Google mkbootimg image (header v0) instead of an Intel OSIP
# container. We place the whole UEFI FD in the "second bootloader" slot
# (loaded at 0x10F00000, entered in 32-bit protected mode) alongside the stock
# kernel/ramdisk, then append the ASUS "sig" blob. The signature is not
# actually verified - the bootloader only checks that it is present.
#
# Copyright (c) Microsoft Corporation.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

import logging
import os
import re
import shutil
import subprocess
import sys

from edk2toolext.environment.uefi_build import UefiBuilder
from edk2toolext.invocables.edk2_platform_build import BuildSettingsManager
from edk2toolext.invocables.edk2_parse import ParseSettingsManager
from edk2toolext.invocables.edk2_pr_eval import PrEvalSettingsManager
from edk2toolext.invocables.edk2_setup import RequiredSubmodule, SetupSettingsManager
from edk2toolext.invocables.edk2_update import UpdateSettingsManager

PACKAGE_NAME = "z00xsPkg"
DEVICE_NAME = "z00xs"
FD_NAME = "Z00XS"

# Boot image geometry, matching the stock ZX551ML boot image (mkbootimg -v).
BOOT_BASE = 0x10000000
KERNEL_OFFSET = 0x00008000     # kernel load 0x10008000
RAMDISK_OFFSET = 0x01000000    # ramdisk load 0x11000000
SECOND_OFFSET = 0x00F00000     # second (our FD) load 0x10F00000
TAGS_OFFSET = 0x00000100       # tags load 0x10000100
PAGE_SIZE = 2048

# Exact stock kernel command line (from the mkbootimg dump of the stock image).
BOOT_CMDLINE = (
    "init=/init pci=noearly console=logk0 loglevel=0 vmalloc=256M "
    "androidboot.hardware=mofd_v1 watchdog.watchdog_thresh=60 "
    "androidboot.spid=xxxx:xxxx:xxxx:xxxx:xxxx:xxxx "
    "androidboot.serialno=01234567890123456789 gpt "
    "snd_pcm.maximum_substreams=8 ptrace.ptrace_can_access=1 panic=15 "
    "ip=50.0.0.2:50.0.0.1::255.255.255.0::usb0:on debug_locks=0 "
    'n_gsm.mux_base_conf="ttyACM0,0 ttyXMM0,1" bootboost=1'
)


def _fd_name_from_fdf(fdf_path: str) -> str:
    with open(fdf_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"^\s*\[FD\.([A-Za-z0-9_]+)\]\s*$", line)
            if m:
                return m.group(1)
    return FD_NAME


# Common Configuration
class CommonPlatform:
    PackagesSupported = (PACKAGE_NAME,)
    ArchSupported = ("X64",)
    TargetsSupported = ("DEBUG", "RELEASE")
    Scopes = ("z00xs", "gcc_x64_linux")
    WorkspaceRoot = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    PackagesPath = (
        "Platforms",
        "Silicon/Intel",
        "Common/edk2",
    )


_fdf_path = os.path.join(CommonPlatform.WorkspaceRoot, "Platforms", PACKAGE_NAME, f"{PACKAGE_NAME}.fdf")
if os.path.isfile(_fdf_path):
    FD_NAME = _fd_name_from_fdf(_fdf_path)


# Configuration for Update & Setup
class SettingsManager(UpdateSettingsManager, SetupSettingsManager, PrEvalSettingsManager, ParseSettingsManager):
    def GetPackagesSupported(self):
        return CommonPlatform.PackagesSupported

    def GetArchitecturesSupported(self):
        return CommonPlatform.ArchSupported

    def GetTargetsSupported(self):
        return CommonPlatform.TargetsSupported

    def GetRequiredSubmodules(self):
        return [RequiredSubmodule("Common/edk2", False)]

    def SetArchitectures(self, list_of_requested_architectures):
        unsupported = set(list_of_requested_architectures) - set(self.GetArchitecturesSupported())
        if unsupported:
            error_string = "Unsupported Architecture Requested: " + " ".join(unsupported)
            logging.critical(error_string)
            raise Exception(error_string)
        self.ActualArchitectures = list_of_requested_architectures

    def GetWorkspaceRoot(self):
        return CommonPlatform.WorkspaceRoot

    def GetActiveScopes(self):
        return CommonPlatform.Scopes

    def GetPlatformDscAndConfig(self):
        return (f"{PACKAGE_NAME}/{PACKAGE_NAME}.dsc", {})

    def GetName(self):
        return DEVICE_NAME

    def GetPackagesPath(self):
        return CommonPlatform.PackagesPath


# Actual Configuration for Platform Build
class PlatformBuilder(UefiBuilder, BuildSettingsManager):
    def __init__(self):
        UefiBuilder.__init__(self)
        self.build_jobs = None

    def AddCommandLineOptions(self, parserObj):
        parserObj.add_argument(
            "-j", "--jobs", dest="build_jobs", type=str, default=None,
            help="Optional - number of parallel edk2 build jobs (default: host CPU count)",
        )

    def RetrieveCommandLineOptions(self, args):
        self.build_jobs = args.build_jobs

    def GetWorkspaceRoot(self):
        return CommonPlatform.WorkspaceRoot

    def GetPackagesPath(self):
        return CommonPlatform.PackagesPath

    def GetActiveScopes(self):
        return CommonPlatform.Scopes

    def GetName(self):
        return PACKAGE_NAME

    def GetLoggingLevel(self, loggerType):
        return logging.INFO

    # Environment
    def SetPlatformEnv(self):
        logging.debug("PlatformBuilder SetPlatformEnv")

        self.env.SetValue("PRODUCT_NAME", DEVICE_NAME, "Platform Hardcoded")
        self.env.SetValue("ACTIVE_PLATFORM", f"Platforms/{PACKAGE_NAME}/{PACKAGE_NAME}.dsc", "Platform Hardcoded")
        self.env.SetValue("TARGET_ARCH", "X64", "Platform Hardcoded")
        self.env.SetValue("TOOL_CHAIN_TAG", "CLANGPDB", "Platform Hardcoded - default toolchain")
        self.env.SetValue("TARGET", "RELEASE", "Platform Hardcoded - default target")

        jobs = self.build_jobs if self.build_jobs else str(os.cpu_count() or 1)
        self.env.SetValue("MAX_CONCURRENT_THREAD_NUMBER", jobs, "From command line or host CPU count")

        base_tools = os.path.join(self.GetWorkspaceRoot(), "Common", "edk2", "BaseTools")
        self.env.SetValue("EDK_TOOLS_PATH", base_tools, "Platform Hardcoded")
        wrappers = "BinWrappers/PosixLike" if os.name != "nt" else "BinWrappers"
        path_additions = [
            os.path.join(base_tools, wrappers),
            os.path.join(base_tools, "Source", "C", "bin"),
        ]
        os.environ["PATH"] = os.pathsep.join(path_additions + [os.environ.get("PATH", "")])
        if shutil.which("nasm") is None:
            logging.critical("nasm not found in PATH")
            return 1

        if self.env.GetValue("KDNET_USB") == "1":
            self.env.SetValue("BLD_*_KDNET_USB", "1", "Platform Hardcoded (KDNET_USB=1)")

        return 0

    # Boot image packing (mkbootimg + appended ASUS sig)
    def PlatformPostBuild(self):
        ws = self.GetWorkspaceRoot()
        target = self.env.GetValue("TARGET")
        out_base = self.env.GetValue("BUILD_OUTPUT_BASE")
        fd_path = os.path.join(out_base, "FV", f"{FD_NAME}.fd")

        pkg_dir = os.path.join(ws, "Platforms", PACKAGE_NAME)
        res_dir = os.path.join(pkg_dir, "ImageResources")
        scripts = os.path.join(ws, "Resources", "Scripts")
        out_dir = os.path.join(ws, "out")
        out_image = os.path.join(out_dir, f"boot_{DEVICE_NAME}_{target}.img")

        kernel = os.path.join(res_dir, "kernel")
        ramdisk = os.path.join(res_dir, "ramdisk")
        sig = os.path.join(res_dir, "sig")

        if not os.path.isfile(fd_path):
            logging.critical(f"expected firmware image not produced: {fd_path}")
            logging.critical(f"    (looked under BUILD_OUTPUT_BASE={out_base})")
            return 1
        logging.info(f"==> FD saved as {fd_path} ({os.path.getsize(fd_path)} bytes)")

        # Required boot-image inputs. kernel/ramdisk come from the stock image:
        #   python3 Resources/Scripts/unpack_bootimg.py boot.img Platforms/z00xsPkg/ImageResources
        for label, path in (("kernel", kernel), ("ramdisk", ramdisk), ("sig", sig)):
            if not os.path.isfile(path):
                logging.critical(f"missing {label}: {path}")
                if label in ("kernel", "ramdisk"):
                    logging.critical("        extract them from the stock boot image once:")
                    logging.critical(f"        python3 Resources/Scripts/unpack_bootimg.py boot.img {res_dir}")
                return 1

        # The primary bootloader jumps to offset 0 of the second-stage payload
        # in 32-bit protected mode. Make offset 0 of the FD a direct jmp to the
        # SEC entry (same mechanism as the OSIP devices).
        logging.info("==> Patching SEC entry jump at offset 0")
        patch = subprocess.run(
            [sys.executable, os.path.join(scripts, "patch_sec_entry.py"), fd_path],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        if patch.returncode != 0:
            logging.critical("patch_sec_entry.py failed: %s", (patch.stderr or patch.stdout).strip())
            return 1

        with open(fd_path, "rb") as fh:
            first_byte = fh.read(1)
        if first_byte != b"\xe9":
            logging.critical("image is not directly executable at offset 0 (first byte %s)" % first_byte.hex())
            return 1

        os.makedirs(out_dir, exist_ok=True)
        if os.path.isfile(out_image):
            os.remove(out_image)

        logging.info("==> Assembling Android boot image (FD as second bootloader)")
        mkboot = subprocess.run(
            [
                sys.executable, os.path.join(scripts, "mkbootimg.py"),
                "--kernel", kernel,
                "--ramdisk", ramdisk,
                "--second", fd_path,
                "--cmdline", BOOT_CMDLINE,
                "--base", hex(BOOT_BASE),
                "--kernel_offset", hex(KERNEL_OFFSET),
                "--ramdisk_offset", hex(RAMDISK_OFFSET),
                "--second_offset", hex(SECOND_OFFSET),
                "--tags_offset", hex(TAGS_OFFSET),
                "--pagesize", str(PAGE_SIZE),
                "--header_version", "0",
                "-o", out_image,
            ],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        if mkboot.returncode != 0 or not os.path.isfile(out_image):
            logging.critical("mkbootimg failed: %s", (mkboot.stderr or mkboot.stdout).strip())
            return 1
        logging.info(mkboot.stdout.strip())

        # Append the ASUS "sig" blob. It is not verified; the bootloader only
        # checks that it is present:  cat zf2_6_sig >> <boot image>
        logging.info("==> Appending ASUS sig (%d bytes)" % os.path.getsize(sig))
        with open(out_image, "ab") as dst, open(sig, "rb") as src:
            dst.write(src.read())

        logging.info(f"==> Output image saved as {out_image} ({os.path.getsize(out_image)} bytes)")
        return 0

    def FlashRomImage(self):
        return 0


if __name__ == "__main__":
    import argparse

    from edk2toolext.invocables.edk2_platform_build import Edk2PlatformBuild
    from edk2toolext.invocables.edk2_setup import Edk2PlatformSetup
    from edk2toolext.invocables.edk2_update import Edk2Update

    os.chdir(CommonPlatform.WorkspaceRoot)
    SCRIPT_PATH = os.path.relpath(__file__)

    parser = argparse.ArgumentParser(add_help=False)

    parse_group = parser.add_mutually_exclusive_group()

    parse_group.add_argument("--update", "--UPDATE", action="store_true", help="Invokes stuart_update")
    parse_group.add_argument("--setup", "--SETUP", action="store_true", help="Invokes stuart_setup")

    args, remaining = parser.parse_known_args()

    new_args = ["stuart", "-c", SCRIPT_PATH]
    new_args = new_args + remaining

    sys.argv = new_args

    if args.setup:
        Edk2PlatformSetup().Invoke()
    elif args.update:
        Edk2Update().Invoke()
    else:
        Edk2PlatformBuild().Invoke()
