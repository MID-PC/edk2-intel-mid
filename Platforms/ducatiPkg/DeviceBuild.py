##
# @file DeviceBuild.py
# Copyright (c) Microsoft Corporation.
# Copyright (c) Acer Iconia A1-830 firmware port contributors.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##
#
# ducatiPkg - Acer Iconia A1-830 (Clover Trail+) device build script.
#
# Pure-python stuart device build script.  Supersedes the repository's former
# bash build system (build.sh, setup_env.sh and per-platform pack_image.sh,
# all removed):
#
#   * workspace prep   (edk2.patch + brotli submodules + BaseTools)  -> PlatformPreBuild
#   * edk2 build       (build -p/-b/-t/-a with KDNET_USB defines)    -> UefiBuilder
#   * boot image pack  (patch_sec_entry.py + mkosip.py -> out/*.img) -> PlatformPostBuild
#
# Runnable from any directory: the script pins its CWD to the workspace root
# before doing anything.  Build settings can be given as KEY=VALUE
# arguments (CLI overrides the platform defaults in SetPlatformEnv):
#
#   python3 Platforms/ducatiPkg/DeviceBuild.py                  # DEBUG, CLANGPDB, IA32
#   python3 Platforms/ducatiPkg/DeviceBuild.py TARGET=RELEASE
#   python3 Platforms/ducatiPkg/DeviceBuild.py TARGET=RELEASE KDNET_USB=1
#   python3 Platforms/ducatiPkg/DeviceBuild.py -j 8
#
# stuart lifecycle hooks are also available:
#
#   python3 Platforms/ducatiPkg/DeviceBuild.py --setup     # stuart_setup
#   python3 Platforms/ducatiPkg/DeviceBuild.py --update    # stuart_update
#
# The produced flashable image is out/boot_ducati_<TARGET>.img (same path and
# content contract as the historical build.sh output).
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
from edk2toolext.invocables.edk2_setup import SetupSettingsManager
from edk2toolext.invocables.edk2_update import UpdateSettingsManager

# ------------------------------------------------------------------------------
# Static platform facts (mirror of the [Defines] section of ducatiPkg.dsc)
# ------------------------------------------------------------------------------

PACKAGE_NAME = "ducatiPkg"
DEVICE_NAME = "ducati"

# FD block that the FDF assembles; the built firmware lands in
# Build/ducatiPkg/<TARGET>_<TOOLCHAIN>/FV/<FD_NAME>.fd
FD_NAME = "DUCATI"

# Conf/target.txt is gitignored and regenerated from the edk2 BaseTools
# template by stuart (populate_conf_dir) on fresh checkouts.  That template
# defaults ACTIVE_PLATFORM/TARGET_ARCH/TOOL_CHAIN_TAG to EmulatorPkg/IA32/
# VS2022, and stuart applies target.txt with override precedence - so the
# defaults must never be left active, or the build would be hijacked.  The
# four values are managed in SetPlatformEnv (command-line overrideable)
# and passed explicitly on the build command line instead.
TARGET_TXT_CONTENT = """\
#
# Conf/target.txt - edk2 build defaults for the ducatiPkg (Acer Iconia A1-830)
# firmware build.
#
# Managed by Platforms/ducatiPkg/DeviceBuild.py (SetPlatformEnv).  The stuart
# flow passes the platform, target, arch and toolchain explicitly on the
# build command line, so the corresponding values here are intentionally
# commented out - an active value in this file would override the command
# line.  Per-target defaults (DEBUG, CLANGPDB, IA32) are enforced in
# SetPlatformEnv instead and are still overridable on the command line.
#
#ACTIVE_PLATFORM       = Platforms/ducatiPkg/ducatiPkg.dsc
#TARGET                = DEBUG
#TARGET_ARCH           = IA32
TOOL_CHAIN_CONF       = Conf/tools_def.txt
#TOOL_CHAIN_TAG        = CLANGPDB
BUILD_RULE_CONF = Conf/build_rule.txt
"""


def _fd_name_from_fdf(fdf_path: str) -> str:
    """Read the [FD.*] block name from the platform FDF (same rule as build.sh)."""
    with open(fdf_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"^\s*\[FD\.([A-Za-z0-9_]+)\]\s*$", line)
            if m:
                return m.group(1)
    return FD_NAME


# ####################################################################################### #
#                                Common Configuration                                     #
# ####################################################################################### #
class CommonPlatform:
    PackagesSupported = (PACKAGE_NAME,)
    ArchSupported = ("IA32",)
    TargetsSupported = ("DEBUG", "RELEASE")
    # NOTE: the 'edk2-build' scope must NOT be active here. tianocore edk2's own
    # .pytool declares a nuget external dependency named 'mu_nasm' in that scope
    # (BaseTools/Bin/nasm_ext_dep.yaml); activating it would make stuart demand a
    # specific (downloaded) NASM build instead of the host toolchain this platform
    # has always built with. We only use stuart's plain UefiBuilder flow - no Mu
    # plugins - so 'edk2-build' (and its ext deps) stay out of scope.
    Scopes = ("ducati", "gcc_ia32_linux")
    # Platforms/ducatiPkg/DeviceBuild.py -> Platforms/ducatiPkg -> Platforms -> root
    WorkspaceRoot = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    # The workspace root is searched first by edk2's BaseTools for every file
    # reference, so the root-relative paths used throughout the DSC/FDF resolve
    # without an explicit root entry. "Platforms" makes the device package
    # resolvable by its bare name (ducatiPkg) - required by edk2 BaseTools
    # plugins such as DebugMacroCheck - and "Common/edk2" carries the edk2
    # packages (MdePkg, MdeModulePkg, ...). This mirrors the old
    # PACKAGES_PATH="$ROOT_DIR:$EDK2_DIR" export.
    PackagesPath = (
        "Platforms",
        "Silicon/Intel",
        "Common/edk2",
    )


# Derive the FDF [FD.*] block name at import time (same rule as build.sh).
# Falls back to FD_NAME ("DUCATI") if the FDF is unavailable.
_fdf_path = os.path.join(CommonPlatform.WorkspaceRoot, "Platforms", PACKAGE_NAME, f"{PACKAGE_NAME}.fdf")
if os.path.isfile(_fdf_path):
    FD_NAME = _fd_name_from_fdf(_fdf_path)


# ####################################################################################### #
#                         Configuration for Update & Setup                                #
# ####################################################################################### #
class SettingsManager(UpdateSettingsManager, SetupSettingsManager, PrEvalSettingsManager, ParseSettingsManager):
    def GetPackagesSupported(self):
        return CommonPlatform.PackagesSupported

    def GetArchitecturesSupported(self):
        return CommonPlatform.ArchSupported

    def GetTargetsSupported(self):
        return CommonPlatform.TargetsSupported

    def GetRequiredSubmodules(self):
        # Common/edk2 is a manually cloned nested git repository, not a submodule
        # of this workspace, so the RequiredSubmodule machinery does not apply.
        # Its two brotli bindings are resolved by PlatformBuilder._prepare_workspace().
        return []

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


# ####################################################################################### #
#                         Actual Configuration for Platform Build                         #
# ####################################################################################### #
class PlatformBuilder(UefiBuilder, BuildSettingsManager):
    def __init__(self):
        UefiBuilder.__init__(self)
        self.build_jobs = None

    def AddCommandLineOptions(self, parserObj):
        parserObj.add_argument(
            "-j", "--jobs", dest="build_jobs", type=str, default=None,
            help="Optional - number of parallel edk2 build jobs (default: host CPU count).",
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

    # -------------------------------------------------------------------------
    # Environment
    # -------------------------------------------------------------------------
    def SetPlatformEnv(self):
        logging.debug("PlatformBuilder SetPlatformEnv")

        ws = self.GetWorkspaceRoot()

        self.env.SetValue("PRODUCT_NAME", DEVICE_NAME, "Platform Hardcoded")
        # Root-relative DSC path; BaseTools resolves it against WORKSPACE first.
        self.env.SetValue("ACTIVE_PLATFORM", f"Platforms/{PACKAGE_NAME}/{PACKAGE_NAME}.dsc", "Platform Hardcoded")
        self.env.SetValue("TARGET_ARCH", "IA32", "Platform Hardcoded")
        self.env.SetValue("TOOL_CHAIN_TAG", "CLANGPDB", "Platform Hardcoded - default toolchain")
        # Default build target (DEBUG).  A CLI 'TARGET=...' argument wins over
        # this because the invocable applies command-line tokens first and they
        # are created non-overridable.
        self.env.SetValue("TARGET", "DEBUG", "Platform Hardcoded - default target")

        jobs = self.build_jobs if self.build_jobs else str(os.cpu_count() or 1)
        self.env.SetValue("MAX_CONCURRENT_THREAD_NUMBER", jobs, "From command line or host CPU count")

        # edk2 BaseTools live inside the Common/edk2 checkout.
        base_tools = os.path.join(ws, "Common", "edk2", "BaseTools")
        self.env.SetValue("EDK_TOOLS_PATH", base_tools, "Platform Hardcoded")

        # Put the edk2 BaseTools on PATH exactly like edksetup.sh would:
        #   BinWrappers/*   -> the 'build' python entry point
        #   Source/C/bin    -> the compiled C tools (GenFv, GenFds, ...)
        wrappers = "BinWrappers/PosixLike" if os.name != "nt" else "BinWrappers"
        path_additions = [
            os.path.join(base_tools, wrappers),
            os.path.join(base_tools, "Source", "C", "bin"),
        ]
        os.environ["PATH"] = os.pathsep.join(path_additions + [os.environ.get("PATH", "")])

        # .nasm sources (BaseLib, BaseCpuLib, DxeIpl) are assembled with a bare
        # 'nasm' resolved via PATH (NASM_PREFIX is unset); fail early with a
        # clear message instead of a cryptic error half-way through the build.
        if shutil.which("nasm") is None:
            logging.critical("nasm not found on PATH: the platform assembles .nasm "
                             "sources.  Install NASM and add it to PATH.")
            return 1

        # KDNET-over-USB variant: mirrors 'KDNET_USB=1 ./build.sh ducati DEBUG'.
        # The define is only emitted when explicitly requested so the DSC/FDF
        # '!ifdef KDNET_USB' gating keeps the exact behaviour of the bash build.
        if self.env.GetValue("KDNET_USB") == "1":
            self.env.SetValue("BLD_*_KDNET_USB", "1", "Platform Hardcoded (KDNET_USB=1)")

        # Keep the gitignored Conf/target.txt from ever carrying the edk2
        # template's EmulatorPkg/VS2022 defaults (see TARGET_TXT_CONTENT).
        # Must happen before conf_mgmt.populate_conf_dir() and
        # ParseTargetFile() run later in SetEnv.
        conf_target = os.path.join(ws, "Conf", "target.txt")
        try:
            current = open(conf_target, "r", encoding="utf-8").read()
        except FileNotFoundError:
            current = ""
        if current != TARGET_TXT_CONTENT:
            os.makedirs(os.path.dirname(conf_target), exist_ok=True)
            with open(conf_target, "w", encoding="utf-8") as fh:
                fh.write(TARGET_TXT_CONTENT)
            logging.info("==> (Re)wrote Conf/target.txt with ducatiPkg-safe defaults")

        return 0

    # -------------------------------------------------------------------------
    # Workspace preparation (was setup_env.sh's job)
    # -------------------------------------------------------------------------
    def _prepare_workspace(self):
        ws = self.GetWorkspaceRoot()
        edk2_dir = os.path.join(ws, "Common", "edk2")
        patch_file = os.path.join(ws, "Resources", "edk2.patch")
        ref_commit = "fc939c7b37"

        if not os.path.isfile(patch_file):
            logging.critical(f"Missing {patch_file} - cannot prepare the edk2 tree.")
            return 1

        def git(*args, **kwargs):
            cmd = ["git", "-C", edk2_dir] + list(args)
            return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                  cwd=kwargs.pop("cwd", None))

        if shutil.which("git") is None:
            logging.critical("no 'git' on PATH: needed to apply edk2.patch and init the "
                             "brotli submodules (Git for Windows provides it on Windows).")
            return 1

        # -- edk2.patch ------------------------------------------------------
        # Same decision ladder as setup_env.sh: already-applied is skipped,
        # otherwise apply, falling back to a 3-way merge if the context drifted.
        if git("apply", "--reverse", "--check", patch_file).returncode == 0:
            logging.info("==> edk2.patch already applied -> skipping")
        elif git("apply", "--check", patch_file).returncode == 0:
            logging.info("==> Applying edk2.patch")
            if git("apply", patch_file).returncode != 0:
                logging.critical("git apply failed to apply edk2.patch")
                return 1
            logging.info("    applied (kept uncommitted in the edk2 working tree)")
        elif git("apply", "--3way", "--check", patch_file).returncode == 0:
            logging.info("==> Applying edk2.patch (3-way, context drifted)")
            if git("apply", "--3way", patch_file).returncode != 0:
                logging.critical("git apply --3way failed to apply edk2.patch")
                return 1
        else:
            logging.critical("cannot apply edk2.patch to the edk2 working tree")
            logging.critical(f"    the patch is authored against edk2 {ref_commit}.")
            logging.critical('    to reset a dirty checkout and retry: git -C "%s" checkout -- .' % edk2_dir)
            return 1

        # -- brotli submodules ----------------------------------------------
        # Only the two Brotli bindings are needed; CryptoPkg (openssl/mbedtls)
        # and the oniguruma regex bindings are fetched on demand because this
        # platform does not reference them.
        logging.info("==> Initializing brotli submodules")
        r = git(
            "submodule", "update", "--init",
            "BaseTools/Source/C/BrotliCompress/brotli",
            "MdeModulePkg/Library/BrotliCustomDecompressLib/brotli",
        )
        if r.returncode != 0:
            logging.critical("failed to initialize the brotli submodules in %s" % edk2_dir)
            return 1

        # -- BaseTools -------------------------------------------------------
        # On-demand build, gated the same way build.sh did (GenFv present =>
        # nothing to do; a partial build is healed).  The C tool is named
        # GenFv.exe on Windows, GenFv on POSIX.
        genfv = os.path.join(edk2_dir, "BaseTools", "Source", "C", "bin",
                             "GenFv.exe" if os.name == "nt" else "GenFv")
        if not os.path.isfile(genfv) or not os.access(genfv, os.X_OK):
            logging.info("==> Building BaseTools")
            # 'make' on POSIX; on Windows this needs GNU make (msys2/MSYS2) -
            # the VS2022 nmake path is a separate, as-yet-unwired toolchain
            # decision.  Resolving through PATH keeps the host's toolchain in
            # use on every platform instead of hardcoding a Unix binary name.
            make = shutil.which("make")
            if make is None:
                logging.critical("no 'make' on PATH: BaseTools needs GNU make (POSIX, or "
                                 "msys2/MSYS2 on Windows; VS2022 nmake is not wired up yet)")
                return 1
            # Guard against non-GNU 'make' implementations (dmake, jom, ...)
            # picked up from PATH on Windows: the BaseTools C build and the edk2
            # module GNUmakefiles both target GNU make.
            make_ver = subprocess.run([make, "--version"], capture_output=True, text=True,
                                      encoding="utf-8", errors="replace")
            if make_ver.returncode != 0 or "GNU Make" not in (make_ver.stdout or ""):
                logging.critical(f"'{make}' is not GNU make: BaseTools needs GNU make "
                                 "(msys2/MSYS2 on Windows; VS2022 nmake is not wired up yet)")
                return 1
            r = subprocess.run([make, "-C", os.path.join(edk2_dir, "BaseTools"),
                                "-j", str(os.cpu_count() or 1)])
            if r.returncode != 0:
                logging.critical("BaseTools build failed")
                return 1
        return 0

    def PlatformPreBuild(self):
        return self._prepare_workspace()

    # -------------------------------------------------------------------------
    # Boot image packing (was pack_image.sh's job)
    # -------------------------------------------------------------------------
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
        logging.info(f"==> Firmware image: {fd_path} ({os.path.getsize(fd_path)} bytes)")

        for f in ("hdr", "sig", "cmdline.txt", "parameter"):
            if not os.path.isfile(os.path.join(osip_dir, f)):
                logging.critical(f"missing {os.path.join(osip_dir, f)}")
                logging.critical("        unpack the stock boot image once while porting:")
                logging.critical(f"        python3 Resources/Scripts/unpack_osip.py boot.img {osip_dir}")
                return 1

        # Make the image directly executable at offset 0: the FD starts with the
        # FVSEC firmware volume header, whose first 16 bytes are the (unused)
        # zero vector. Patch a 'jmp rel32' there so entering the binary at its
        # very first byte lands on SecEntry.
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

        logging.info(f"==> Boot image: {out_image} ({os.path.getsize(out_image)} bytes)")
        logging.info("    flash with:  fastboot flash boot %s" % out_image)
        return 0

    def FlashRomImage(self):
        # No on-device flashing from the build host; the fastboot hint above is enough.
        return 0


if __name__ == "__main__":
    import argparse

    from edk2toolext.invocables.edk2_platform_build import Edk2PlatformBuild
    from edk2toolext.invocables.edk2_setup import Edk2PlatformSetup
    from edk2toolext.invocables.edk2_update import Edk2Update

    # Pin CWD to the workspace root before computing the script path: stuart
    # resolves the '-c' config (and workspace-relative PackagesPath entries)
    # against the current directory, and on Windows os.path.relpath() raises
    # if the script lives on a different drive than the CWD.
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
