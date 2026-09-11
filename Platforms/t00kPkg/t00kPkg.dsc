## @file
#  t00kPkg - ASUS ZenFone 5 Lite A502CG (T00K) platform firmware
#
#  Booted by the primary (stock) bootloader at 0x01101000 in place of the Intel
#  bootstub, so no silicon init is performed. Debug output is rendered into the
#  already-running framebuffer at 0x3F000000 (540x960, stride 544, 32 bpp), which is also exposed
#  as a fixed-mode GOP.
#
#  Everything generation-independent (library stack, PCD policy, generic
#  component build list) comes from CloverviewPkg.dsc.inc, which itself chains
#  IntelPkg.dsc.inc. This file only carries what is unique to the A502CG.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = t00kPkg
  PLATFORM_GUID                  = 0D37CF67-B361-4F55-87F6-420AEB26B49A
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/t00kPkg
  SUPPORTED_ARCHITECTURES        = IA32
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platforms/t00kPkg/t00kPkg.fdf

  DEFINE SHELL_TYPE              = BUILD_SHELL

  #
  # CPU variant for this board: the Z2520 bin. CloverviewPkg.dsc.inc validates
  # it; it also picks the precompiled \_PR PPM SSDT (SsdPpm2520.aml) that the
  # device FDF packs - the DSDT itself is SKU-independent now. Other values:
  # SOC_VARIANT_Z2560, SOC_VARIANT_Z2580.
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z2520

!include Silicon/Intel/CloverviewPkg/CloverviewPkg.dsc.inc

[BuildOptions]
  #
  # ASLPP (device DSDT): -DKDNET_USB (KDNET_USB=1 ./build.sh t00k
  # DEBUG) swaps OTG0 to usb-debug.asl. CC: the KDNET define gates DBG2 table
  # packing in the device FDF. SOC_VARIANT is no longer an ASL macro (the \_PR
  # PPM surface is precompiled into SsdPpm2520.aml, not #included here).
  #
!ifdef KDNET_USB
  GCC:*_*_*_ASLPP_FLAGS          = -DKDNET_USB
  GCC:*_*_*_CC_FLAGS             = -DKDNET_USB
!endif

################################################################################
#
# Device-specific PCDs
#
################################################################################
[PcdsFixedAtBuild]
  gIntelMidTokenSpaceGuid.PcdConsoleStateBase|0x3EEFD000

  #
  # Framebuffer: fixed, already running.
  #
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x3F000000
  # Visible panel is 540 wide, but the display pipe scans out with a 544-pixel
  # (2176-byte) stride. Visible mode is 540x960; do not replace stride with width.
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|540
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|544
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|960
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4

  #
  # SMBIOS physical device identity. SmBiosTableDxe reads these and the CPU
  # identity from the generation package (CloverviewPkg) PCDs. These strings
  # feed the Windows "Computer Hardware ID", so keep them descriptive of the
  # actual machine.
  #
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ASUS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"ZenFone 5 Lite"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"t00k"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"A502CG"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"A502CG"

[PcdsPatchableInModule]
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|540
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|960
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|0

[PcdsDynamicDefault]
  #
  # Common boot/console/variable policy lives in the shared
  # Silicon/Intel/IntelPkg/IntelPkg.dsc.inc. Override here to differ.
  #

################################################################################
#
# Components - only what is owned by the device: the platform's ACPI tables.
# The rest of the build list comes from the silicon .dsc.inc chain.
#
################################################################################
[Components]
  #
  # ACPI. Windows' bootia32.efi/winload aborts with 0xc0000225 ("the firmware
  # (BIOS) is not ACPI compatible") when no RSDP/XSDT is published. AcpiTableDxe
  # builds the RSDP/XSDT; the SoC-generic AcpiPlatformDxe (CloverviewPkg)
  # installs a hardware-reduced FADT, a FACS, an MADT, the DSDT compiled from
  # AcpiTables.inf and whatever else this device packs into the fixed-table
  # FREEFORM (see t00kPkg.fdf), skipping tables the device excludes.
  #
  Silicon/Intel/CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  Platforms/t00kPkg/AcpiPlatformDxe/AcpiTables.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  # Use the SCU reset implementation, as in the supplied old A502CG package.
  ResetSystemLib|Silicon/Intel/CloverviewPkg/Library/ScuResetSystemLib/ScuResetSystemLib.inf
