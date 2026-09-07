## @file
#  ducatiPkg - Acer Iconia A1-830 (A502CG) platform firmware
#
#  Booted by the primary (stock) bootloader at 0x01101000 in place of the Intel
#  bootstub, so no silicon init is performed. Debug output is rendered into the
#  already-running framebuffer at 0x3F000000 (768x1024x4), which is also exposed
#  as a fixed-mode GOP.
#
#  Everything generation-independent (library stack, PCD policy, generic
#  component build list) comes from CloverviewPkg.dsc.inc, which itself chains
#  IntelPkg.dsc.inc. This file only carries what is unique to the A1-830.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = ducatiPkg
  PLATFORM_GUID                  = 5D6E7F80-9AB1-4C2D-8E3F-405162738495
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/ducatiPkg
  SUPPORTED_ARCHITECTURES        = IA32
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platforms/ducatiPkg/ducatiPkg.fdf

  DEFINE SHELL_TYPE              = BUILD_SHELL

  #
  # CPU variant: the Acer Iconia A1-830 uses the Z2560 bin. CloverviewPkg.dsc.inc
  # validates this; the ASLPP define below selects the \_PR PPM table compiled
  # into the DSDT (PrZ2560.asl). Other values: SOC_VARIANT_Z2520, SOC_VARIANT_Z2580.
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z2560

!include Silicon/Intel/CloverviewPkg/CloverviewPkg.dsc.inc

[BuildOptions]
  #
  # ASLPP (device DSDT): -D$(SOC_VARIANT) resolves the \_PR #include to the
  # right PrZ<SKU>.asl (SOC_VARIANT _is_ the ASLPP macro name, e.g.
  # SOC_VARIANT_Z2560); -DKDNET_USB (KDNET_USB=1 ./build.sh ducati
  # DEBUG) swaps OTG0 to usb-debug.asl. CC: the KDNET define gates DBG2 table
  # installation in AcpiPlatformDxe.
  #
!ifdef KDNET_USB
  GCC:*_*_*_ASLPP_FLAGS          = -D$(SOC_VARIANT) -DKDNET_USB
  GCC:*_*_*_CC_FLAGS             = -DKDNET_USB
!else
  GCC:*_*_*_ASLPP_FLAGS          = -D$(SOC_VARIANT)
!endif

################################################################################
#
# Device-specific PCDs
#
################################################################################
[PcdsFixedAtBuild]
  #
  # Framebuffer: fixed, already running.
  #
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x3F000000
  # Visible panel is 540 wide, but the display pipe scans out with a 544-pixel
  # (2176-byte) stride, so the two values must be kept separate. On this board
  # the bootloader runs the panel at 768x1024 rotated.
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|768
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|768
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|1024
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4

  #
  # SMBIOS physical device identity. SmBiosTableDxe reads these and the CPU
  # identity from the generation package (CloverviewPkg) PCDs. These strings
  # feed the Windows "Computer Hardware ID", so keep them descriptive of the
  # actual machine.
  #
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Acer"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"Iconia A1-830"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"ducati"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"A1-830"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"A1-830"

[PcdsPatchableInModule]
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|768
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|1024
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
  # builds the RSDP/XSDT; this driver installs a hardware-reduced FADT, a FACS,
  # an MADT and the DSDT compiled from AcpiTables.inf.
  #
  Platforms/ducatiPkg/AcpiPlatformDxe/AcpiPlatformDxe.inf
  Platforms/ducatiPkg/AcpiPlatformDxe/AcpiTables.inf
