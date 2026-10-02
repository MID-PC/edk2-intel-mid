##
# @file DeviceBuild.py
# Copyright (c) Microsoft Corporation.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

import logging
import os
import shutil
import sys

from edk2toolext.environment.uefi_build import UefiBuilder
from edk2toolext.invocables.edk2_platform_build import BuildSettingsManager
from edk2toolext.invocables.edk2_parse import ParseSettingsManager
from edk2toolext.invocables.edk2_pr_eval import PrEvalSettingsManager
from edk2toolext.invocables.edk2_setup import RequiredSubmodule, SetupSettingsManager
from edk2toolext.invocables.edk2_update import UpdateSettingsManager

PACKAGE_NAME = "t00gPkg"
DEVICE_NAME = "t00g"

# Common Configuration
class CommonPlatform:
    PackagesSupported = (PACKAGE_NAME,)
    ArchSupported = ("IA32",)
    TargetsSupported = ("DEBUG", "RELEASE")
    Scopes = ("t00g", "gcc_ia32_linux")
    WorkspaceRoot = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    PackagesPath = (
        "Platforms",
        "Silicon/Intel",
        "Common/edk2",
    )


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
        self.env.SetValue("TARGET", "RELEASE", "Platform Hardcoded - default target")

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

        #
        # FD geometry, supplied by build_uefi.py from the device's TOML config
        # in Resources/Configs/. The platform FDF picks these up as $(FD_BASE),
        # $(FD_SIZE) and $(FD_BLOCKS), which also sets the PcdFdBaseAddress and
        # PcdFdSize PCDs read by SecMain, PlatformPei and SmBiosTableDxe.
        #
        for _name in ("FD_BASE", "FD_SIZE"):
            _value = self.env.GetValue(_name)
            if _value:
                self.env.SetValue(f"BLD_*_{_name}", _value, "Device TOML config")
        if self.env.GetValue("FD_SIZE"):
            _size = int(self.env.GetValue("FD_SIZE"), 0)
            self.env.SetValue("BLD_*_FD_BLOCKS", str(_size // 0x1000), "Device TOML config")

        return 0

    #
    # Boot image packing is handled by build_uefi.py from the device's config
    # in Resources/Configs/<device>.toml, which selects the format and its
    # options. Nothing device-specific belongs here.
    #
    def PlatformPostBuild(self):
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
