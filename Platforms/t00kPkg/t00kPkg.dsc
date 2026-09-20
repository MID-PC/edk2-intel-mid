
[Defines]
  PLATFORM_NAME                  = t00kPkg
  PLATFORM_GUID                  = 0D37CF67-B361-4F55-87F6-420AEB26B49A
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/t00kPkg
  SUPPORTED_ARCHITECTURES        = IA32
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = t00kPkg/t00kPkg.fdf

  DEFINE SHELL_TYPE              = BUILD_SHELL
  #
  # Cloverview CPU variant selection
  #   Atom Z2520 - 1.2 GHz
  #   Atom Z2560 - 1.6 GHz
  #   Atom Z2580 - 2.0 GHz
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z2520

!include CloverviewPkg/CloverviewPkg.dsc.inc

[PcdsFixedAtBuild]
  # Device Framebuffer
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x3F000000
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|540
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|544
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|960
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4

  # SMBIOS
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ASUS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"ZenFone 5 Lite"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"t00k"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"A502CG"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"A502CG"

[Components]
  # ACPI
  CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  t00kPkg/AcpiPlatformDxe/AcpiTables.inf
