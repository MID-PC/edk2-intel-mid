
[Defines]
  PLATFORM_NAME                  = ducatiPkg
  PLATFORM_GUID                  = 5D6E7F80-9AB1-4C2D-8E3F-405162738495
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/ducatiPkg
  SUPPORTED_ARCHITECTURES        = IA32
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platforms/ducatiPkg/ducatiPkg.fdf

  DEFINE SHELL_TYPE              = BUILD_SHELL

  #
  # Cloverview CPU variant selection
  #   Atom Z2520 - 1.2 GHz
  #   Atom Z2560 - 1.6 GHz
  #   Atom Z2580 - 2.0 GHz
  #
  DEFINE SOC_VARIANT             = SOC_VARIANT_Z2560

!include CloverviewPkg/CloverviewPkg.dsc.inc

[BuildOptions]
!ifdef KDNET_USB
  CLANGPDB:*_*_*_ASLPP_FLAGS     = -DKDNET_USB
  CLANGPDB:*_*_*_CC_FLAGS        = -DKDNET_USB
!endif

[PcdsFixedAtBuild]
  #
  # Device Framebuffer
  #
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x3F000000
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|768
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|768
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|1024
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4

  #
  # SMBIOS
  #
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Acer"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"Iconia A1-830"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"ducati"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"A1-830"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"A1-830"

[Components]
  #
  # ACPI
  #
  CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  Platforms/ducatiPkg/AcpiPlatformDxe/AcpiTables.inf
