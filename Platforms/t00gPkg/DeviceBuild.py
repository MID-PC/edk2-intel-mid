##
# @file DeviceBuild.py
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

PACKAGE_NAME = "t00gPkg"
DEVICE_NAME = "t00g"
FD_NAME = "T00G"

def _fd_name_from_fdf(fdf_path: str) -> str:
    # Read the [FD.*] block name from the platform FDF
    with open(fdf_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"^\s*\[FD\.([A-Za-z0-9_]+)\]\s*$", line)
            if m:
                return m.group(1)
    return FD_NAME


# Common Configuration
class CommonPlatform:
    PackagesSupported = (PACKAGE_NAME,)
    ArchSupported = ("IA32",)
    TargetsSupported = ("DEBUG", "RELEASE")
    Scopes = ("ducati", "gcc_ia32_linux")
    WorkspaceRoot = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    PackagesPath = (
        "Platforms",
        "Silicon/Intel",
        "Common/edk2",
    )


# Derive FDF [FD.*] name at import; fall back to FD_NAME if unavailable
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

        ws = self.GetWorkspaceRoot()

        self.env.SetValue("PRODUCT_NAME", DEVICE_NAME, "Platform Hardcoded")
        self.env.SetValue("ACTIVE_PLATFORM", f"Platforms/{PACKAGE_NAME}/{PACKAGE_NAME}.dsc", "Platform Hardcoded")
        self.env.SetValue("TARGET_ARCH", "IA32", "Platform Hardcoded")
        self.env.SetValue("TOOL_CHAIN_TAG", "CLANGPDB", "Platform Hardcoded - default toolchain")
        self.env.SetValue("TARGET", "DEBUG", "Platform Hardcoded - default target")

        jobs = self.build_jobs if self.build_jobs else str(os.cpu_count() or 1)
        self.env.SetValue("MAX_CONCURRENT_THREAD_NUMBER", jobs, "From command line or host CPU count")

        base_tools = os.path.join(ws, "Common", "edk2", "BaseTools")
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

        # KDNET_USB
        if self.env.GetValue("KDNET_USB") == "1":
            self.env.SetValue("BLD_*_KDNET_USB", "1", "Platform Hardcoded (KDNET_USB=1)")

        return 0

    # Boot image packing
    def PlatformPostBuild(self):
        ws = self.GetWorkspaceRoot()
        target = self.env.GetValue("TARGET")
        out_base = self.env.GetValue("BUILD_OUTPUT_BASE")
        fd_path = os.path.join(out_base, "FV", f"{FD_NAME}.fd")

        pkg_dir = os.path.join(ws, "Platforms", PACKAGE_NAME)
        osip_dir = os.path.join(pkg_dir, "ImageResources")
        out_dir = os.path.join(ws, "out")
        out_image = os.path.join(out_dir, f"boot_{DEVICE_NAME}_{target}.img")

        if not os.path.isfile(fd_path):
            logging.critical(f"expected firmware image not produced: {fd_path}")
            logging.critical(f"    (looked under BUILD_OUTPUT_BASE={out_base})")
            return 1
        logging.info(f"==> FD saved as {fd_path} ({os.path.getsize(fd_path)} bytes)")

        for f in ("hdr", "sig", "cmdline.txt", "parameter"):
            if not os.path.isfile(os.path.join(osip_dir, f)):
                logging.critical(f"missing {os.path.join(osip_dir, f)}")
                logging.critical("        unpack the stock boot image once:")
                logging.critical(f"        python3 Resources/Scripts/unpack_osip.py boot.img {osip_dir}")
                return 1

        logging.info("==> Patching SEC entry jump at offset 0")
        patch = subprocess.run(
            [sys.executable, os.path.join(ws, "Resources", "Scripts", "patch_sec_entry.py"), fd_path],
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

        logging.info("==> Assembling OSIP image with mkosip")
        mkosip = subprocess.run(
            [
                sys.executable,
                os.path.join(ws, "Resources", "Scripts", "mkosip.py"),
                "-o", out_image,
                "-p", fd_path,
                "-d", osip_dir,
            ],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        if mkosip.returncode != 0 or not os.path.isfile(out_image):
            logging.critical("mkosip failed: %s", (mkosip.stderr or mkosip.stdout).strip())
            return 1

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
