
[Defines]
  PLATFORM_NAME                  = z00xsPkg
  PLATFORM_GUID                  = 4D2E6F81-7A3B-4C5D-9E0F-1A2B3C4D5E6F
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/z00xsPkg
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = z00xsPkg/z00xsPkg.fdf

  DEFINE SHELL_TYPE              = BUILD_SHELL
  #
  # Moorefield CPU variant selection
  #   Atom Z3560 - 1.83 GHz
  #   Atom Z3580 - 2.33 GHz
  #   Atom Z3590 - 2.50 GHz
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z3580

!include MoorefieldPkg/MoorefieldPkg.dsc.inc

[PcdsFixedAtBuild]
  # Device framebuffer
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x7F700000
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|1080
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|1088
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|1920
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4
  gIntelMidTokenSpaceGuid.PcdConsoleStateBase|0x11FFF000

  # SMBIOS
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ASUS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"ZenFone Zoom"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"Z00XS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"ZX551ML"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"ZX551ML"

[Components]
  # ACPI
  z00xsPkg/AcpiPlatformDxe/AcpiTables.inf
