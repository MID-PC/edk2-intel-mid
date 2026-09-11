## @file
#  CloverviewPkg.dsc.inc - Cloverview SoC (Intel Atom Z25xx) DSC fragment
#
#  Per-generation configuration for the Cloverview platform. Chains the generic
#  Intel MID fragment (IntelPkg.dsc.inc) and adds the generation-specific PCDs
#  and component build list. Included from the platform DSC.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

!include Silicon/Intel/IntelPkg/IntelPkg.dsc.inc

[Defines]
  #
  # CPU variant selection (Mu-Silicium "SOC_TYPE"-style define; here named
  # SOC_VARIANT). The device DSC must set SOC_VARIANT to SOC_VARIANT_Z2520,
  # SOC_VARIANT_Z2560 or SOC_VARIANT_Z2580 before including this fragment:
  # it drives the SMBIOS processor PCDs below and determines which precompiled
  # \_PR PPM SSDT (SsdPpm<SKU>.aml, with an adjacent .dsl disassembly under
  # CloverviewPkg/Acpi/AcpiTables) the device FDF packs. It is NOT an ASL macro
  # any more: the
  # DSDT defines no \_PR since the PPM surface moved to the SSDT.
  #
  #   SOC_VARIANT_Z2520  1.2 GHz, 100 MHz FSB  (Asus A502CG-class part)
  #   SOC_VARIANT_Z2560  1.6 GHz, ~133 MHz FSB  (Acer Iconia A1-830)
  #   SOC_VARIANT_Z2580  2.0 GHz, ~133 MHz FSB  (Asus ZenFone 6 A600CG/T00G;
  #                        6 P-states from the stock SFI FREQ table)
  #
!if $(SOC_VARIANT) == SOC_VARIANT_Z2520
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z2560
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z2580
!else
  !error "SOC_VARIANT must be set to SOC_VARIANT_Z2520, SOC_VARIANT_Z2560 or SOC_VARIANT_Z2580"
!endif

[PcdsFixedAtBuild]
  #
  # Image / temporary RAM layout. 0x01101000 is where the primary bootloader
  # hands control over (in place of the stock bootstub). Temporary RAM is put
  # well above the image inside the 0x01000000-0x36FEFFFF System RAM window
  # reported by the stock kernel's iomem.  These PCDs are generic (declared in
  # IntelPkg.dec); the values are this generation's.
  #
  gIntelMidTokenSpaceGuid.PcdFdBaseAddress|0x01101000
  gIntelMidTokenSpaceGuid.PcdFdSize|0x00200000

  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamBase|0x02000000
  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamSize|0x00040000

  #
  # Local APIC timer clock = bus clock. Penwell/Cloverview run a 100 MHz FSB.
  #
  gEfiMdePkgTokenSpaceGuid.PcdFSBClock|100000000

  #
  # SMBIOS Processor (Type 4). SoC-generation values, selected by the CPU bin
  # (SOC_VARIANT). The marketing part name and speed differ per variant; the
  # "ProcessorModel" is the Atom Z2500-series family name.
  #
!if $(SOC_VARIANT) == SOC_VARIANT_Z2520
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z2520"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z2520"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|1200
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z2560
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z2560"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z2560"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|1600
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z2580
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z2580"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z2580"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|2000
!else
  !error "SOC_VARIANT must be set to SOC_VARIANT_Z2520, SOC_VARIANT_Z2560 or SOC_VARIANT_Z2580"
!endif

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  #
  # Real reset/shutdown. The shared IntelPkg.dsc.inc maps ResetSystemLib to
  # the BaseResetSystemLibNull no-op for every module; for runtime drivers the
  # Null lib means gRT->ResetSystem returns without doing anything, so Windows
  # "Shutdown" (Fast Startup writes the hiberfile, then the OS issues
  # EfiResetShutdown) never powers the SoC off and the next boot "restores
  # from the previous location". ScuResetSystemLib implements the Cloverview
  # SCU IPC reset plus the real PMU S5 power-off (pm_cmd = S5_VALUE), so
  # ResetSystemRuntimeDxe - the only runtime driver consuming ResetSystemLib -
  # behaves correctly. Affects no other module.
  #
  ResetSystemLib|Silicon/Intel/CloverviewPkg/Library/ScuResetSystemLib/ScuResetSystemLib.inf

################################################################################
#
# Components - the Cloverview SoC drivers. Placement/dispatch order lives in
# the platform FDF (Extra.fdf.inc / ducatiPkg.fdf), not here. The generic SEC
# and the core stack come from IntelPkg.dsc.inc.
#
################################################################################
[Components]
  #
  # ACPI fixed tables for the Cloverview SoC (FADT/FACS/MADT/DBG2) are NOT built
  # here anymore: they are pre-compiled .aml blobs packed by each device into
  # its own FREEFORM file (see the device FDF). The DSDT is built by the
  # platform package (Platforms/<dev>Pkg/AcpiPlatformDxe/AcpiTables.inf), since
  # it is device-specific. The SoC-generic AcpiPlatformDxe
  # (Silicon/Intel/CloverviewPkg/Drivers/AcpiPlatformDxe) is listed per-device
  # in the platform DSC.
  #

  #
  # PEI - first dispatch ([Depex] TRUE), carries the pre-PEI SoC work that used
  # to live in the Cloverview SEC (watchdog / APIC timer / XD enable).
  #
  Silicon/Intel/CloverviewPkg/PlatformPei/PlatformPei.inf

  #
  # Variable runtime support without NV storage (DRAM-loaded firmware).
  #
  Silicon/Intel/CloverviewPkg/Drivers/PlatformRuntimeDxe/PlatformRuntimeDxe.inf

  #
  # SD card stack. There is no real PCI on this SoC, so the SDHCI block at
  # 0xFFA58000 (mmc1, the SD slot per iomem_A502CG.txt) is registered as a
  # non-discoverable device; SdMmcPciHcDxe binds to it through
  # NonDiscoverablePciDeviceDxe. Card detect is not used (assume connected).
  #
  Silicon/Intel/CloverviewPkg/Drivers/SdHostDxe/SdHostDxe.inf

  #
  # USB: OTG0 (Chipidea HDRC @ 0xFFA60000) registered as a non-discoverable
  # EHCI controller, plus the generic USB stack.
  #
!ifndef KDNET_USB
  Silicon/Intel/CloverviewPkg/Drivers/OtgHostDxe/OtgHostDxe.inf
  MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
!endif
