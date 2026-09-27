
!include IntelPkg/IntelPkg.dsc.inc

[BuildOptions]
!ifdef KDNET_USB
  CLANGPDB:*_*_*_ASLPP_FLAGS     = -DKDNET_USB
  CLANGPDB:*_*_*_CC_FLAGS        = -DKDNET_USB
!endif

[Defines]
  #
  # Moorefield CPU variant selection
  #   Atom Z3560 - 1.83 GHz
  #   Atom Z3580 - 2.33 GHz
  #   Atom Z3590 - 2.50 GHz
  #
!if $(SOC_VARIANT) == SOC_VARIANT_Z3560
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z3580
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z3590
!else
  !error "SOC_VARIANT must be set to SOC_VARIANT_Z3560, SOC_VARIANT_Z3580 or SOC_VARIANT_Z3590"
!endif

[PcdsFeatureFlag]
  #
  # Moorefield is a 64-bit (Silvermont) part. The primary bootloader enters the
  # image in 32-bit protected mode; SEC (X64/SecEntry.nasm) performs the
  # protected->long-mode switch, and DxeIpl keeps running in long mode from
  # there. Override the IntelPkg default (FALSE) so DxeIpl builds the long-mode
  # handoff instead of assuming a 32-bit DXE.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdDxeIplSwitchToLongMode|TRUE

[PcdsFixedAtBuild]
  #
  # Image / temporary RAM layout.
  # The primary bootloader loads the image at 0x10F00000 (the boot image
  # "second bootloader" slot) and jumps to its first byte in 32-bit protected
  # mode. 0x10F00000 lies inside the 0x06000000-0x7F600000 System RAM window
  # (see ZX551ML/tables/MMAP and iomem.txt).
  #
  gIntelMidTokenSpaceGuid.PcdFdBaseAddress|0x10F00000
  gIntelMidTokenSpaceGuid.PcdFdSize|0x00400000

  #
  # SEC/PEI temporary RAM (stack + heap + long-mode page tables) inside the
  # low 0x00100000-0x04000000 System RAM window. The top 32 KiB is used by the
  # SEC entry for the 4-level identity page tables (see X64/SecEntry.nasm).
  #
  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamBase|0x02000000
  gIntelMidTokenSpaceGuid.PcdSecPeiTemporaryRamSize|0x00040000

  #
  # Permanent PEI memory inside the main FD RAM window, above the FD image.
  #
  gIntelMidTokenSpaceGuid.PcdPeiMemoryBase|0x12000000

  #
  # Local APIC timer clock = bus clock. Moorefield runs a 100 MHz FSB.
  #
  gEfiMdePkgTokenSpaceGuid.PcdFSBClock|100000000

  #
  # PCI device MMIO windows for SfiMemoryMapLib. Moorefield GPU (0000:00:02.0)
  # BARs live at 0x80000000-0x9FFFFFFF and 0xC0000000-0xC1FFFFFF (iomem.txt).
  # The framebuffer at 0x7F700000 is inside the 0x7F600000-0x80000000 reserved
  # window already described as MMIO by the SFI MMAP, so no extra PCD needed.
  #
  gIntelMidTokenSpaceGuid.PcdPciGpuMmioBase|0x80000000
  gIntelMidTokenSpaceGuid.PcdPciGpuMmioSize|0x20000000
  gIntelMidTokenSpaceGuid.PcdPciIspMmioBase|0xC2000000
  gIntelMidTokenSpaceGuid.PcdPciIspMmioSize|0x00400000
  # No separate EMAC window on this part.
  gIntelMidTokenSpaceGuid.PcdPciEmacMmioSize|0x0

  #
  # SMBIOS Processor (Type 4) defines
  #
!if $(SOC_VARIANT) == SOC_VARIANT_Z3560
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z3560"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z3560"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|1830
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z3580
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z3580"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z3580"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|2330
!elseif $(SOC_VARIANT) == SOC_VARIANT_Z3590
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorModel|"Intel(R) Atom(TM) Processor Z3590"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"Intel Atom Z3590"
  gIntelMidTokenSpaceGuid.PcdSmbiosProcessorMaxSpeedMHz|2500
!else
  !error "SOC_VARIANT must be set to SOC_VARIANT_Z3560, SOC_VARIANT_Z3580 or SOC_VARIANT_Z3590"
!endif

[PcdsFixedAtBuild.X64]
  #
  # 64-bit NX/XD profile. Keep memory protection minimal so the plain
  # boot-to-shell image does not fault on the identity map SEC built.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetNxForStack|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdDxeNxMemoryProtectionPolicy|0x0000000000000000
  gEfiMdeModulePkgTokenSpaceGuid.PcdImageProtectionPolicy|0x00000000

[LibraryClasses]
  #
  # SCU IPC command based reset runtime
  #
  ResetSystemLib|MoorefieldPkg/Library/ScuResetSystemLib/ScuResetSystemLib.inf

[Components]
  #
  # PEIM
  #
  MoorefieldPkg/PlatformPei/PlatformPei.inf

  #
  # Emulated RTC Arch Protocol (required by DXE Core) + SCU watchdog management
  #
  MoorefieldPkg/Drivers/PlatformRuntimeDxe/PlatformRuntimeDxe.inf
