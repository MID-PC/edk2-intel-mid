
!include IntelPkg/IntelPkg.dsc.inc

[BuildOptions]
!ifdef KDNET_USB
  CLANGPDB:*_*_*_ASLPP_FLAGS     = -DKDNET_USB
  CLANGPDB:*_*_*_CC_FLAGS        = -DKDNET_USB
!endif

[Defines]
  #
  # Cloverview CPU variant selection
  #   Atom Z2520 - 1.2 GHz
  #   Atom Z2560 - 1.6 GHz
  #   Atom Z2580 - 2.0 GHz
  #
!if $(SOC_VARIANT) == SOC_VARIANT_Z2520
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z2560
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z2580
!else
  !error "SOC_VARIANT must be set to SOC_VARIANT_Z2520, SOC_VARIANT_Z2560 or SOC_VARIANT_Z2580"
!endif

[PcdsFixedAtBuild]
  #
  # Temporary RAM is put well above the image inside the first System RAM window
  #
  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamBase|0x02000000
  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamSize|0x00040000

  #
  # SCU IPC-1 (intel_scu_ipc) and south-complex PMU (intel_pmu_driver)
  #
  gIntelMidTokenSpaceGuid.PcdScuIpcBase|0xFF11C000
  gIntelMidTokenSpaceGuid.PcdPmuBase|0xFF11D000

  # Local APIC timer clock = bus clock; Cloverview runs a 100 MHz FSB
  gEfiMdePkgTokenSpaceGuid.PcdFSBClock|100000000

  #
  # SMBIOS Processor (Type 4) defines
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

[LibraryClasses]
  #
  # SCU IPC command based reset runtime
  #
  ResetSystemLib|IntelPkg/Library/ScuResetSystemLib/ScuResetSystemLib.inf

[Components]
  #
  # PEIM
  #
  CloverviewPkg/PlatformPei/PlatformPei.inf

  #
  # Platform Setup
  #
  CloverviewPkg/Drivers/PlatformRuntimeDxe/PlatformRuntimeDxe.inf

  #
  # SD slot registration
  #
  CloverviewPkg/Drivers/SdHostDxe/SdHostDxe.inf

  # ChipIdea OTG forced into EHCI host mode + generic USB EHCI stack
  # Disabled in the no-USB KDNET variant
!ifndef KDNET_USB
  CloverviewPkg/Drivers/OtgHostDxe/OtgHostDxe.inf
  MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
!endif

  #
  # ACPI installation
  #
  CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
