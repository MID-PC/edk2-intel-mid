## @file
#  t00gPkg - Asus ZenFone 6 (T00G / A600CG) platform firmware
#
#  Booted by the primary (stock) bootloader at 0x01101000 in place of the Intel
#  bootstub, so no silicon init is performed. Debug output is rendered into the
#  already-running framebuffer at 0x7FC00000 (720x1280x4), which is also exposed
#  as a fixed-mode GOP.
#
#  Everything generation-independent (library stack, PCD policy, generic
#  component build list) comes from CloverviewPkg.dsc.inc, which itself chains
#  IntelPkg.dsc.inc. This file only carries what is unique to the ZenFone 6.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = t00gPkg
  PLATFORM_GUID                  = 9AE1C4B7-5D3F-4E82-9A21-6C8D4FB07E11
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/t00gPkg
  SUPPORTED_ARCHITECTURES        = IA32
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platforms/t00gPkg/t00gPkg.fdf

  DEFINE SHELL_TYPE              = BUILD_SHELL

  #
  # CPU variant: the Asus ZenFone 6 uses the Z2580 bin. CloverviewPkg.dsc.inc
  # validates this; the ASLPP define below selects the \_PR PPM table compiled
  # into the DSDT (PrZ2580.asl). Other values: SOC_VARIANT_Z2520, SOC_VARIANT_Z2560.
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z2580

!include Silicon/Intel/CloverviewPkg/CloverviewPkg.dsc.inc

[BuildOptions]
  #
  # ASLPP (device DSDT): -D$(SOC_VARIANT) resolves the \_PR #include to the
  # right PrZ<SKU>.asl (SOC_VARIANT _is_ the ASLPP macro name, e.g.
  # SOC_VARIANT_Z2580); -DKDNET_USB (KDNET_USB=1 ./build.sh t00g
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
  # Framebuffer: fixed, already running. On the ZenFone 6 the bootloader
  # leaves the display engine scanning out of the graphics steal window at
  # the top of RAM (dmesg: "base in RAM: 0x7fc00000", 4 MiB, dvmt mode=2),
  # which is where A600CG's iomem shows the reserved 0x7fb00000-0x7fffffff
  # window. The GPU's own GMMADR aperture (region 0) is 0x80000000 (256 MiB)
  # and its GTT (region 3) is at 0xDFEC0000.
  #
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x7FC00000
  # Visible panel is 720 wide; the display pipe scans out at native 720
  # (2880-byte) stride, so width/stride stay together. 6.0" 720x1280.
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|720
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|720
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|1280
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4

  #
  # Shared firmware console-state page. The default (0x3EEFD000) is the top
  # page of the A502CG's high DRAM island; on the ZenFone 6 the island runs up
  # to 0x7FAFF3FF (A600CG iomem: "379fd400-7faff3ff System RAM"), so the page
  # moves to 0x7FAFF000 - the top page, just below the 0x7FB00000 MMCONFIG.
  # PlatformPei reserves it and the DSDT's SYSR claims the same range.
  #
  gIntelMidTokenSpaceGuid.PcdConsoleStateBase|0x7FAFF000

  #
  # SMBIOS physical device identity. SmBiosTableDxe reads these and the CPU
  # identity from the generation package (CloverviewPkg) PCDs. These strings
  # feed the Windows "Computer Hardware ID", so keep them descriptive of the
  # actual machine.
  #
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ASUS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"ZenFone 6"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"T00G"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"A600CG"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"A600CG"

[PcdsPatchableInModule]
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|720
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|1280
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
  Platforms/t00gPkg/AcpiPlatformDxe/AcpiPlatformDxe.inf
  Platforms/t00gPkg/AcpiPlatformDxe/AcpiTables.inf