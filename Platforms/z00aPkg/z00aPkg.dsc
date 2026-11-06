
[Defines]
  PLATFORM_NAME                  = z00aPkg
  PLATFORM_GUID                  = 7B5C8E92-6F2A-4D3B-8C1E-2B3A4C5D6E7F
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/z00aPkg
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = z00aPkg/z00aPkg.fdf

  DEFINE SHELL_TYPE              = BUILD_SHELL
  #
  # Moorefield CPU variant selection
  #   Atom Z3560 - 1.83 GHz
  #   Atom Z3580 - 2.33 GHz
  #   Atom Z3590 - 2.50 GHz
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z3580

!include MoorefieldPkg/MoorefieldPkg.dsc.inc

[BuildOptions]
  CLANGPDB:*_*_*_CC_FLAGS        = -DSC_RESET_CUT_SD_POWER

[PcdsFixedAtBuild]
  # Device framebuffer
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x7F700000
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|1080
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|1088
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|1920
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4

  # SMBIOS
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ASUS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"ZenFone 2"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"Z00A"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"ZE551ML"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"ZE551ML"

[Components]
  # ACPI
  z00aPkg/AcpiPlatformDxe/AcpiTables.inf
