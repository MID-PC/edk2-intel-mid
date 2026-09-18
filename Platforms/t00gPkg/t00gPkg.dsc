
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
  # Cloverview CPU variant selection
  #   Atom Z2520 - 1.2 GHz
  #   Atom Z2560 - 1.6 GHz
  #   Atom Z2580 - 2.0 GHz
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z2580

!include CloverviewPkg/CloverviewPkg.dsc.inc

[PcdsFixedAtBuild]
  # Device Framebuffer
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x7FC00000
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|720
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|720
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|1280
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4
  gIntelMidTokenSpaceGuid.PcdConsoleStateBase|0x7FAFF000

  # SMBIOS
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ASUS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"ZenFone 6"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"T00G"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"A600CG"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"A600CG"

[Components]
  # ACPI
  CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  Platforms/t00gPkg/AcpiPlatformDxe/AcpiTables.inf
