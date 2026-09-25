
[Defines]
  PLATFORM_NAME                  = vespaPkg
  PLATFORM_GUID                  = 63E1E143-8539-4002-AB8B-61AE14FEA68C
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/vespaPkg
  SUPPORTED_ARCHITECTURES        = IA32
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = ducatiPkg/ducatiPkg.fdf

  DEFINE SHELL_TYPE              = BUILD_SHELL
  #
  # Cloverview CPU variant selection
  #   Atom Z2520 - 1.2 GHz
  #   Atom Z2560 - 1.6 GHz
  #   Atom Z2580 - 2.0 GHz
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z2560

!include CloverviewPkg/CloverviewPkg.dsc.inc

[PcdsFixedAtBuild]
  # Device Framebuffer
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x3F000000
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|600
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|608
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|1024
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4

  # SMBIOS
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Acer"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"Iconia B1-730"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"vespa"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"B1-730"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"B1-730"

[Components]
  # ACPI
  CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  ducatiPkg/AcpiPlatformDxe/AcpiTables.inf
