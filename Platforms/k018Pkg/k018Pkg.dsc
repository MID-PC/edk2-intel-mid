
[Defines]
  PLATFORM_NAME                  = k018Pkg
  PLATFORM_GUID                  = 5944194D-373F-43D9-9058-C2E537F705AB
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/k018/Pkg
  SUPPORTED_ARCHITECTURES        = IA32
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = k018Pkg/k018Pkg.fdf

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
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|1280
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|1280
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|800
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4

  # SMBIOS
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ASUS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"Transformer TF103CG"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"k018"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"TF103CG"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"TF103CG"

[Components]
  # ACPI
  CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  k018Pkg/AcpiPlatformDxe/AcpiTables.inf
