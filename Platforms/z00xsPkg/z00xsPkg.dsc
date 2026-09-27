
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
  # Device framebuffer: 1080x1920, stride 1088 pixels, 4 bytes/pixel,
  # pre-lit by the bootloader at 0x7F700000 (inside the 0x7F600000-0x80000000
  # reserved/MMIO window per ZX551ML/iomem.txt).
  gIntelMidTokenSpaceGuid.PcdFrameBufferBase|0x7F700000
  gIntelMidTokenSpaceGuid.PcdFrameBufferWidth|1080
  gIntelMidTokenSpaceGuid.PcdFrameBufferStride|1088
  gIntelMidTokenSpaceGuid.PcdFrameBufferHeight|1920
  gIntelMidTokenSpaceGuid.PcdFrameBufferBpp|4
  # Console-state scratch page: in the 0x06000000-0x7F600000 System RAM window,
  # above the FD image (0x10F00000 + 0x300000 = 0x11200000) but below the
  # permanent PEI memory window (PcdPeiMemoryBase = 0x12000000) so the PEI heap
  # never allocates over it. Reserved via a carve-out HOB in PlatformPei.
  gIntelMidTokenSpaceGuid.PcdConsoleStateBase|0x11FFF000

  # SMBIOS
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ASUS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemModel|"ZenFone Zoom"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailModel|"Z00XS"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemRetailSku|"ZX551ML"
  gIntelMidTokenSpaceGuid.PcdSmbiosSystemBoardModel|"ZX551ML"
