
!include IntelPkg/IntelPkg.dsc.inc

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
  # Image / temporary RAM layout. 0x01101000 is where the primary bootloader
  # hands control over to the bootstub
  # Temporary RAM is put well above the image inside the 0x01000000-0x36FEFFFF System RAM window
  #
  gIntelMidTokenSpaceGuid.PcdFdBaseAddress|0x01101000
  gIntelMidTokenSpaceGuid.PcdFdSize|0x00200000

  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamBase|0x02000000
  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamSize|0x00040000

  #
  # Local APIC timer clock = bus clock
  # Cloverview runs a 100 MHz FSB
  #
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
  # SCU command based reset runtime
  #
  ResetSystemLib|CloverviewPkg/Library/ScuResetSystemLib/ScuResetSystemLib.inf

[Components]
  #
  # Cloverview SoC PEIM
  #
  CloverviewPkg/PlatformPei/PlatformPei.inf

  #
  # Cloverview Platform Setup
  #
  CloverviewPkg/Drivers/PlatformRuntimeDxe/PlatformRuntimeDxe.inf

  #
  # SD slot registration
  #
  CloverviewPkg/Drivers/SdHostDxe/SdHostDxe.inf

  #
  # USB ChipIdea OTG forced into EHCI host mode + generic USB EHCI stack
  # Disabled in the no-USB KDNET variant
  #
!ifndef KDNET_USB
  CloverviewPkg/Drivers/OtgHostDxe/OtgHostDxe.inf
  MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
!endif
