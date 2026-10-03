
!include IntelPkg/IntelPkg.dsc.inc

[BuildOptions]
!ifdef KDNET_USB
  CLANGPDB:*_*_*_ASLPP_FLAGS     = -DKDNET_USB
  CLANGPDB:*_*_*_CC_FLAGS        = -DKDNET_USB
!endif

[Defines]
  #
  # Moorefield CPU variant selection
  #   Atom Z3560 - 1.83 GHz
  #   Atom Z3580 - 2.33 GHz
  #   Atom Z3590 - 2.50 GHz
  #
!if $(SOC_VARIANT) == SOC_VARIANT_Z3560
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z3580
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z3590
!else
  !error "SOC_VARIANT must be set to SOC_VARIANT_Z3560, SOC_VARIANT_Z3580 or SOC_VARIANT_Z3590"
!endif

[PcdsFixedAtBuild]
  #
  # Temporary RAM is put well above the image inside the first System RAM window
  #
  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamBase|0x02000000
  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamSize|0x00040000

  #
  # Permanent PEI memory inside the main FD RAM window, above the FD image.
  #
  gIntelMidTokenSpaceGuid.PcdPeiMemoryBase|0x12000000

  #
  # SCU IPC-1 (intel_scu_ipc) and south-complex PMU (intel_pmu_driver)
  #
  gIntelMidTokenSpaceGuid.PcdScuIpcBase|0xFF009000
  gIntelMidTokenSpaceGuid.PcdPmuBase|0xFF00B000

  #
  # Legacy RAM window override (0x0-0x9FFFF)
  #
  gIntelMidTokenSpaceGuid.PcdLegacyMemorySize|0x000A0000
  gIntelMidTokenSpaceGuid.PcdLegacyTopBase|0x000A0000
  gIntelMidTokenSpaceGuid.PcdLegacyTopSize|0x00000000

  #
  # IVT/BDA (0x0-0x3FFF) as BootServicesData
  #
  gIntelMidTokenSpaceGuid.PcdLegacyReservedType|0x00000004

  #
  # Physical addressing bitness, consumed by PlatformMemoryMapLib
  #
  gIntelMidTokenSpaceGuid.PcdCpuMaxPhysAddrBits|36

  # Local APIC timer clock = bus clock; Moorefield runs a 100 MHz FSB
  gEfiMdePkgTokenSpaceGuid.PcdFSBClock|100000000

  #
  # PCI device MMIO windows for SfiMemoryMapLib
  #
  gIntelMidTokenSpaceGuid.PcdPciGpuMmioBase|0x80000000
  gIntelMidTokenSpaceGuid.PcdPciGpuMmioSize|0x20000000
  gIntelMidTokenSpaceGuid.PcdPciIspMmioBase|0xC2000000
  gIntelMidTokenSpaceGuid.PcdPciIspMmioSize|0x00400000
  # No separate EMAC window on Moorefield
  gIntelMidTokenSpaceGuid.PcdPciEmacMmioSize|0x0

  #
  # SMBIOS Processor (Type 4) defines
  #
!if $(SOC_VARIANT) == SOC_VARIANT_Z3560
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z3560"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z3560"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|1830
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z3580
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z3580"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z3580"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|2330
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z3590
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z3590"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z3590"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|2500
!else
  !error "SOC_VARIANT must be set to SOC_VARIANT_Z3560, SOC_VARIANT_Z3580 or SOC_VARIANT_Z3590"
!endif

[LibraryClasses]
  #
  # SCU IPC command based reset runtime
  #
  ResetSystemLib|IntelPkg/Library/ScuResetSystemLib/ScuResetSystemLib.inf

[Components]
  #
  # PEIM
  #
  MoorefieldPkg/PlatformPei/PlatformPei.inf

  #
  # Platform Setup
  #
  MoorefieldPkg/Drivers/PlatformRuntimeDxe/PlatformRuntimeDxe.inf

  #
  # SD slot registration
  #
  MoorefieldPkg/Drivers/SdHostDxe/SdHostDxe.inf

  #
  # DWC3 USB3 host controller + generic USB XHCI stack
  #
  MoorefieldPkg/Drivers/XhciDwc3Dxe/XhciDwc3Dxe.inf
  MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf

  #
  # ACPI installation
  #
  MoorefieldPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
