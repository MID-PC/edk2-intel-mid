/** @file
  Platform PEIM for Clover Trail+ (Atom Z25xx).

  The primary bootloader has already trained DRAM, configured the SCU and left
  the display engine scanning out of 0x3F000000. This PEIM therefore only:

    1. Walks the SFI MMAP table the bootloader left in the legacy BIOS area
       (0x000E0000-0x00100000) and reports one EFI resource descriptor HOB
       per entry (type 7 = RAM, 11 = MMIO, 12 = I/O, anything else reserved),
       mirroring the sfi_setup_e820() consumer in the U-Boot Tangier
       reference (arch/x86/cpu/tangier/sdram.c).  SFI is mandatory on this
       platform: SfiMemoryMapLib refuses to return when the table is missing
       (the primary bootloader always publishes it), so there is no fallback
       map and no degraded-boot path.
    2. Validates the permanent PEI window and every firmware carve-out
       against the RAM descriptors, and emits the PCI device MMIO windows the
       bootloader never publishes via SFI from per-board PCDs.
    3. Reports the permanent system memory to the PEI Core.
    4. Signals boot mode and memory discovery.

  Reference memory map for the A502CG (SFI MMAP from sfi-tables/MMAP,
  cross-checked with iomem_A502CG.txt).  SFI types: 7 = conventional,
  6 = runtime data, 11 = MMIO.

    00000000-00097fff  conventional (legacy, not reported: IVT/BDA)
    00098000-000fffff  runtime data (RAM buffer / legacy BIOS area)
    00100000-00cfffff  conventional  System RAM
    00d00000-00ffffff  MMIO          reserved
    01000000-36feffff  conventional  System RAM  <-- firmware image lives here
    36ff0000-378fcfff  MMIO          reserved
    378fd000-379fd3ff  MMIO          reserved (+ RAM buffer)
    379fd400-3eeff3ff  conventional  System RAM
    3eeff400-3eefffff  RAM buffer
    3ef00000-3effffff  MMIO          PCI MMCONFIG (bus 00)
    3f000000-3fffffff  MMIO          framebuffer window (GOP base 0x3F000000)
    fec00000-fec00fff  MMIO          IOAPIC
    fee00000-fee00fff  MMIO          Local APIC
    ff000000-ffffffff  MMIO          south complex (SCU, HSU, SDHCI, I2C...)

  The PCI device BAR windows (0x40000000 GPU 00:02.0, 0xDF800000
  00:03.0/00:02.0, 0xFA000000 00:06.0) never appear in SFI: the bootloader
  does not enumerate PCI, so they are PCD-defined (PcdPci*Mmio*).

  IMPORTANT: the firmware image is loaded at 0x01101000, i.e. *inside* the
  0x01000000-0x36feffff DRAM window.  The DXE Core requires every memory
  allocation HOB (the FD image, the SEC/PEI temporary RAM) and the BFV HOB to
  be covered by a EFI_RESOURCE_SYSTEM_MEMORY descriptor HOB; otherwise
  CoreInitializeMemoryServices()/CoreAddMemoryDescriptor() cannot promote the
  range, the DXE Core faults while handing off, and the SoC resets.  The main
  DRAM descriptor therefore starts at 0x01000000, while the permanent PEI
  memory window is placed above the image so the PEI Core never allocates over
  it.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>
#include <Library/PeimEntryPoint.h>
#include <Library/PeiServicesLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/LocalApicLib.h>
#include <Library/PcdLib.h>
#include <Library/SfiMemoryMapLib.h>
#include <Ppi/MasterBootMode.h>
#include <Ppi/MemoryDiscovered.h>



//
// The whole 0-1 MiB block (IVT/BDA, EBDA, VGA/ISA hole, legacy BIOS/SFI
// area) is reported through the PcdLegacy* real-mode trampoline machinery
// below, so the SFI walk skips whatever the bootloader describes under 1 MiB;
// emitting both would create overlapping resource descriptors. The PCI device
// BAR windows the bootloader never publishes via SFI (it does not enumerate
// PCI) come from per-board PcdPci*Mmio* PCDs (see IntelPkg.dec) instead of
// hard-coded tables, which keeps this shared PEIM correct across the
// drei/A502CG and t00g boards.
//
#define CT_SUB_MEGABYTE_LIMIT  0x00100000ULL

#define CT_SYSTEM_MEMORY_ATTRIBUTES  (                  \
  EFI_RESOURCE_ATTRIBUTE_PRESENT                      | \
  EFI_RESOURCE_ATTRIBUTE_INITIALIZED                  | \
  EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE                  | \
  EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE            | \
  EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE      | \
  EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE         | \
  EFI_RESOURCE_ATTRIBUTE_TESTED                         \
  )

#define CT_MMIO_ATTRIBUTES  (                           \
  EFI_RESOURCE_ATTRIBUTE_PRESENT                      | \
  EFI_RESOURCE_ATTRIBUTE_INITIALIZED                  | \
  EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE                  | \
  EFI_RESOURCE_ATTRIBUTE_TESTED                         \
  )

//
// True when [Start1, Start1+Size1) and [Start2, Start2+Size2) intersect. All
// windows here lie within the 4 GiB address space, so the sums cannot wrap.
//
STATIC
BOOLEAN
MmapRangeOverlaps (
  IN UINT64  Start1,
  IN UINT64  Size1,
  IN UINT64  Start2,
  IN UINT64  Size2
  )
{
  return (Start1 < (Start2 + Size2)) && (Start2 < (Start1 + Size1));
}

//
// True when [InnerBase, InnerBase+InnerSize) lies entirely within
// [OuterBase, OuterBase+OuterSize).
//
STATIC
BOOLEAN
MmapRangeIsInside (
  IN UINT64  InnerBase,
  IN UINT64  InnerSize,
  IN UINT64  OuterBase,
  IN UINT64  OuterSize
  )
{
  return (InnerBase >= OuterBase) &&
         (InnerBase + InnerSize <= OuterBase + OuterSize);
}

//
// Emit one resource descriptor HOB, logging its identity.
//
STATIC
VOID
ReportResourceWindow (
  IN EFI_RESOURCE_TYPE  ResourceType,
  IN UINT64             Attributes,
  IN UINT64             Base,
  IN UINT64             Size,
  IN CONST CHAR8        *Name
  )
{
  DEBUG ((
    DEBUG_VERBOSE,
    "PlatformPei: %a %lx-%lx\n",
    Name,
    Base,
    Base + (Size - 1)
    ));

  BuildResourceDescriptorHob (ResourceType, Attributes, Base, Size);
}

//
// The DXE Core promotes each memory allocation HOB by allocating the exact
// range (EfiReservedMemoryType / AllocateAddress), so a carve-out must be
// fully covered by an EFI_RESOURCE_SYSTEM_MEMORY descriptor or the promotion
// fails at hand-off. Validate a firmware carve-out against the RAM windows
// collected from the SFI walk (plus the legacy real-mode block).
//
STATIC
VOID
ValidateCarveOut (
  IN UINT64                Base,
  IN UINT64                Size,
  IN CONST SFI_MMIO_REGION *RamWindows,
  IN UINTN                 RamCount,
  IN CONST CHAR8           *Name
  )
{
  UINTN  Index;

  for (Index = 0; Index < RamCount; Index++) {
    if (MmapRangeIsInside (Base, Size, RamWindows[Index].Base, RamWindows[Index].Size)) {
      return;
    }
  }

  DEBUG ((
    DEBUG_ERROR,
    "PlatformPei: carve-out %a (%lx-%lx) lies outside every RAM window\n",
    Name,
    Base,
    Base + (Size - 1)
    ));
}

STATIC EFI_PEI_PPI_DESCRIPTOR  mPpiBootMode = {
  EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
  &gEfiPeiMasterBootModePpiGuid,
  NULL
};

STATIC EFI_PEI_PPI_DESCRIPTOR  mPpiMemoryDiscovered = {
  EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
  &gEfiPeiMemoryDiscoveredPpiGuid,
  NULL
};

//
// Cloverview/Penwell SCU IPC-1 block (see iomem: ff11c000 intel_scu_ipc).
//
#define SCU_IPC_CMD_OFFSET       0x00u
#define SCU_IPC_STATUS_OFFSET    0x04u
#define SCU_IPC_STATUS_BUSY      BIT0
#define SCU_IPC_STATUS_ERROR     BIT1
#define SCU_IPC_POLL_LIMIT       1000000u

#define IPCMSG_WATCHDOG_TIMER    0xF8u
#define IPC_WDT_SUB_STOP         0x01u

STATIC
BOOLEAN
ScuIpcWaitNotBusy (
  VOID
  )
{
  UINT32  Status;
  UINT32  Retry;

  for (Retry = 0; Retry < SCU_IPC_POLL_LIMIT; Retry++) {
    Status = MmioRead32 (FixedPcdGet32 (PcdScuIpcBase) + SCU_IPC_STATUS_OFFSET);
    if (Status == MAX_UINT32) {
      return FALSE;
    }

    if ((Status & SCU_IPC_STATUS_BUSY) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Stop the SCU kernel watchdog with IPCMSG_WATCHDOG_TIMER / sub-command 0.

  The stock bootstub arms the SCU watchdog before handing control to the OS
  image; if nobody kicks or disables it, the SCU issues a cold reset a few
  seconds later (the only way this SoC can reset itself). Send the disable
  request exactly like intel_scu_watchdog in the CTP kernel.
**/
STATIC
VOID
DisableScuWatchdog (
  VOID
  )
{
  UINT32  Command;
  UINT32  Status;

  if (MmioRead32 (FixedPcdGet32 (PcdScuIpcBase) + SCU_IPC_STATUS_OFFSET) == MAX_UINT32) {
    DEBUG ((DEBUG_WARN, "PlatformPei: SCU IPC not present, skipping watchdog disable\n"));
    return;
  }

  if (!ScuIpcWaitNotBusy ()) {
    DEBUG ((DEBUG_ERROR, "PlatformPei: SCU IPC busy before watchdog disable\n"));
    return;
  }

  //
  // No payload at all: do NOT touch WBUF0 (writing it with inlen = 0 makes the
  // SCU reject the message on Cloverview). The disable request is sub-command
  // 1 (STOP), encoded as (sub << 12) | msg = 0x000010F8, exactly like the
  // known-good bootstub/SEC reference.
  //
  Command = IPCMSG_WATCHDOG_TIMER | (IPC_WDT_SUB_STOP << 12);
  MmioWrite32 (FixedPcdGet32 (PcdScuIpcBase) + SCU_IPC_CMD_OFFSET, Command);

  if (!ScuIpcWaitNotBusy ()) {
    DEBUG ((DEBUG_ERROR, "PlatformPei: SCU IPC timeout during watchdog disable\n"));
    return;
  }

  Status = MmioRead32 (FixedPcdGet32 (PcdScuIpcBase) + SCU_IPC_STATUS_OFFSET);
  if ((Status & SCU_IPC_STATUS_ERROR) != 0) {
    DEBUG ((DEBUG_ERROR, "PlatformPei: SCU watchdog disable rejected (status 0x%08x)\n", Status));
  } else {
    DEBUG ((DEBUG_INFO, "PlatformPei: SCU watchdog disabled (status 0x%08x)\n", Status));
  }
}

/**
  Enable the XD (execute-disable) feature before any page tables exist.

  The IFWI leaves IA32_MISC_ENABLE (MSR 0x1A0) bit 34 (XD Bit Disable) SET on
  this Cloverview part. With XD disabled, CPUID.80000001h:EDX[20] (NX) reads 0
  and bit 63 of a PAE paging entry is RESERVED, so any entry with it set
  raises a page fault (reserved-bit violation) on first access.

  It is a chicken-and-egg trap to gate the clear on that CPUID bit: CPUID only
  reports NX after bit 34 is actually cleared. The Linux kernel's verify_cpu.S
  clears bit 34 unconditionally for Intel family/model >= 6/0xd ("Clear bogus
  XD_DISABLE bits") and only then re-reads CPUID - which is what makes NX work
  under Android/Linux on this same silicon. Mirror that here: attempt the clear
  first, then verify by re-reading the MSR. If the bit sticks, the part
  genuinely lacks EDB and NX stays disabled.

  Once XD is enabled DxeIpl builds 4G PAE page tables, and CpuDxe sets the NX
  bit whenever something requests EFI_MEMORY_XP - e.g. NonDiscoverablePciDeviceDxe's
  NonCoherent DMA path calling SetMemorySpaceAttributes (..., EFI_MEMORY_XP |
  EFI_MEMORY_UC) on the buffers it hands to EhciDxe.

  Must run before DxeIpl builds the NX-capable page tables; this PEIM is the
  first one dispatched ([Depex] TRUE) so it does.
**/
STATIC
VOID
EnableExecuteDisable (
  VOID
  )
{
  UINT32  RegEax;
  UINT32  RegEdx;
  UINTN   Family;
  UINTN   Model;
  UINT64  MiscEnable;

  Family = 0;
  Model  = 0;
  AsmCpuid (0x1, &RegEax, NULL, NULL, &RegEdx);
  if ((RegEdx & BIT9) != 0) {                       // have full leaf 1
    Family = (RegEax >> 8) & 0xF;                   // bits 11:8
    if (Family == 6) {
      Model = ((RegEax >> 16) & 0xF) << 4 |         // extended model, bits 19:16
              ((RegEax >> 4) & 0xF);                //          model, bits 7:4
    }

    if ((Family > 6) || ((Family == 6) && (Model >= 0x0D))) {
      MiscEnable = AsmReadMsr64 (0x1A0);
      if ((MiscEnable & BIT34) != 0) {
        AsmWriteMsr64 (0x1A0, MiscEnable & ~BIT34);
        MiscEnable = AsmReadMsr64 (0x1A0);
        if ((MiscEnable & BIT34) == 0) {
          DEBUG ((
            DEBUG_ERROR,
            "PlatformPei: XD enabled, IA32_MISC_ENABLE now 0x%08x%08x (XD-disable=0)\n",
            (UINT32)RShiftU64 (MiscEnable, 32),
            (UINT32)MiscEnable
            ));
        } else {
          DEBUG ((
            DEBUG_ERROR,
            "PlatformPei: XD clear refused by silicon, IA32_MISC_ENABLE 0x%08x%08x (XD-disable=1)\n",
            (UINT32)RShiftU64 (MiscEnable, 32),
            (UINT32)MiscEnable
            ));
        }
      } else {
        DEBUG ((
          DEBUG_ERROR,
          "PlatformPei: XD already enabled, IA32_MISC_ENABLE now 0x%08x%08x (XD-disable=0)\n",
          (UINT32)RShiftU64 (MiscEnable, 32),
          (UINT32)MiscEnable
          ));
      }
    } else {
      DEBUG ((
        DEBUG_ERROR,
        "PlatformPei: XD not touched (family %d model %d), EDB requires >= 6/0xd\n",
        (UINT32)Family,
        (UINT32)Model
        ));
    }
  }
}

/**
  Report the whole platform memory map.

  Single-pass port of the SFI MMAP consumer in the U-Boot Tangier reference
  (arch/x86/cpu/tangier/sdram.c: sfi_setup_e820 / sfi_get_bank_size /
  board_get_usable_ram_top) applied to the UEFI HOB model:

    - every SFI MMAP entry becomes one EFI resource descriptor HOB
      (7 -> SYSTEM_MEMORY, 11 -> MEMORY_MAPPED_IO, 12 -> IO, anything else
      -> MEMORY_RESERVED), page-aligned inwards;
    - the type-6 block under 1 MiB is deliberately skipped: the whole
      real-mode block comes from the PcdLegacy* PCD machinery below;
    - the permanent PEI window is validated against the RAM descriptor that
      also covers the loaded firmware image, and is placed dynamically just
      below the top of that window when the PCD value does not describe a
      valid interior point;
    - the PCI-device MMIO windows the bootloader never puts into SFI (it does
      not enumerate PCI) come from per-board PcdPci*Mmio* PCDs and are
      emitted as MEMORY_MAPPED_IO beside the SFI-derived windows;
    - firmware-owned carve-outs stay PCD-defined, but each one is validated
      to lie inside a RAM descriptor and to not overlap any sibling before
      its allocation HOB is built.

  SFI is mandatory: the library refuses to return when the bootloader has not
  published the MMAP table, so the walk below always sees a decoded table and
  there is no constant fallback map.
**/
STATIC
VOID
PlatformPeiInstallMemoryMap (
  VOID
  )
{
  EFI_STATUS         Status;
  UINTN              Index;
  UINTN              RamIndex;
  UINTN              MmioIndex;
  UINTN              RamCount;
  UINTN              CarveCount;
  UINTN              PciMmioCount;
  UINTN              MmapMmioCount;
  UINT64             FdBase;
  UINT64             FdSize;
  UINT64             TempRamBase;
  UINT64             TempRamSize;
  UINT64             LegacyBase;
  UINT64             LegacySize;
  UINT64             PeiMemBase;
  UINT64             PeiMemSize;
  UINT64             ConsoleBase;
  UINT64             MainBase;
  UINT64             MainLimit;
  UINT64             Start;
  UINT64             Size;
  UINT64             Limit;
  UINT64             Attr;
  EFI_RESOURCE_TYPE  ResourceType;
  CONST CHAR8        *TypeName;
  BOOLEAN            Found;
  SFI_MMAP_TABLE     SfiMmap;
  UINTN              SfiCount;
  SFI_MMIO_REGION    RamWindows[SFI_MAX_MMAP_ENTRIES + 1];
  SFI_MMIO_REGION    MmapMmio[SFI_MAX_MMAP_ENTRIES];
  SFI_MMIO_REGION    PciMmio[3];
  SFI_MMIO_REGION    CarveOuts[5];

  FdBase      = (UINT64)FixedPcdGet32 (PcdFdBaseAddress) &
                ~(UINT64)EFI_PAGE_MASK;
  FdSize      = ALIGN_VALUE (
                  (UINT64)FixedPcdGet32 (PcdFdBaseAddress) +
                  (UINT64)FixedPcdGet32 (PcdFdSize) - FdBase,
                  EFI_PAGE_SIZE
                  );
  TempRamBase = (UINT64)FixedPcdGet32 (PcdSecPeiTemporaryRamBase) &
                ~(UINT64)EFI_PAGE_MASK;
  TempRamSize = ALIGN_VALUE (
                  (UINT64)FixedPcdGet32 (PcdSecPeiTemporaryRamBase) +
                  (UINT64)FixedPcdGet32 (PcdSecPeiTemporaryRamSize) -
                  TempRamBase,
                  EFI_PAGE_SIZE
                  );
  LegacyBase  = (UINT64)FixedPcdGet32 (PcdLegacyMemoryBase);
  LegacySize  = (UINT64)FixedPcdGet32 (PcdLegacyMemorySize);
  PeiMemBase  = (UINT64)FixedPcdGet32 (PcdPeiMemoryBase);
  ConsoleBase = (UINT64)FixedPcdGet32 (PcdConsoleStateBase);

  //
  // RAM windows collected for carve-out validation. Entry 0 is the legacy
  // real-mode block reported from PCDs below, so the legacy carve-outs are
  // validated with the same machinery as the firmware windows.
  //
  RamWindows[0].Base = LegacyBase;
  RamWindows[0].Size = LegacySize;
  RamWindows[0].Name = "legacy real-mode";
  RamCount = 1;

  //
  // PCI-device MMIO windows from per-board PCDs (see IntelPkg.dec). These are
  // the only MMIO windows SFI cannot publish on this platform: the bootloader
  // does not enumerate PCI, so no BAR entries ever reach the table. A Size of
  // 0 disables the window.
  //
  PciMmioCount = 0;
  if (FixedPcdGet64 (PcdPciGpuMmioSize) != 0) {
    PciMmio[PciMmioCount].Base = FixedPcdGet64 (PcdPciGpuMmioBase);
    PciMmio[PciMmioCount].Size = FixedPcdGet64 (PcdPciGpuMmioSize);
    PciMmio[PciMmioCount].Name = "GPU 00:02.0";
    PciMmioCount++;
  }
  if (FixedPcdGet64 (PcdPciIspMmioSize) != 0) {
    PciMmio[PciMmioCount].Base = FixedPcdGet64 (PcdPciIspMmioBase);
    PciMmio[PciMmioCount].Size = FixedPcdGet64 (PcdPciIspMmioSize);
    PciMmio[PciMmioCount].Name = "00:03.0 / 00:02.0";
    PciMmioCount++;
  }
  if (FixedPcdGet64 (PcdPciEmacMmioSize) != 0) {
    PciMmio[PciMmioCount].Base = FixedPcdGet64 (PcdPciEmacMmioBase);
    PciMmio[PciMmioCount].Size = FixedPcdGet64 (PcdPciEmacMmioSize);
    PciMmio[PciMmioCount].Name = "00:06.0";
    PciMmioCount++;
  }

  //
  // Walk the live SFI MMAP table, one resource descriptor HOB per entry,
  // mirroring sfi_setup_e820() in the U-Boot Tangier reference. SfiGetMmap()
  // asserts (dead-loops) when the bootloader failed to publish the table, so
  // reaching this point means the map is decoded and authoritative.
  //
  Status = SfiGetMmap (&SfiMmap);
  ASSERT_EFI_ERROR (Status);
  SfiCount = SfiMmap.EntryCount;

  MainBase       = 0;
  MainLimit      = 0;
  MmapMmioCount  = 0;

  for (Index = 0; Index < SfiCount; Index++) {
    Start = SfiMmap.Entry[Index].PhysStart;
    Size  = SfiMmap.Entry[Index].Pages << EFI_PAGE_SHIFT;

    if (Size == 0) {
      continue;
    }
    if (Start > MAX_UINT64 - Size) {
      DEBUG ((
        DEBUG_ERROR,
        "PlatformPei: SFI entry %d wraps the address space\n",
        (UINT32)Index
        ));
      continue;
    }
    Limit = Start + Size;

    //
    // The whole sub-1 MiB block is reported through the legacy machinery
    // below; the bootloader's own rows there must not be emitted a second
    // time or the descriptors would overlap.
    //
    if (Limit <= CT_SUB_MEGABYTE_LIMIT) {
      DEBUG ((
        DEBUG_VERBOSE,
        "PlatformPei: SFI entry %d (%lx-%lx) covered by legacy window\n",
        (UINT32)Index,
        Start,
        Limit
        ));
      continue;
    }

    //
    // MMAP entries are not guaranteed page aligned; square each window
    // inwards so the descriptor boundaries are GCD-clean.
    //
    Start = ALIGN_VALUE (Start, EFI_PAGE_SIZE);
    Limit = Limit & ~(UINT64)(EFI_PAGE_SIZE - 1);
    if (Start >= Limit) {
      continue;
    }
    Size = Limit - Start;

    switch (SfiMmap.Entry[Index].Type) {
    case SFI_MMAP_TABLE_TYPE_RAM:
      ResourceType = EFI_RESOURCE_SYSTEM_MEMORY;
      Attr         = CT_SYSTEM_MEMORY_ATTRIBUTES;
      TypeName     = "RAM";
      break;

    case SFI_MMAP_TABLE_TYPE_MMIO:
      ResourceType = EFI_RESOURCE_MEMORY_MAPPED_IO;
      Attr         = CT_MMIO_ATTRIBUTES;
      TypeName     = "MMIO";
      break;

    case 12:                     // SFI type 12 = I/O window (see U-Boot
      ResourceType = EFI_RESOURCE_IO;   // arch/x86/include/asm/sfi.h)
      Attr         = CT_MMIO_ATTRIBUTES;
      TypeName     = "I/O";
      break;

    default:                     // 6 = reserved runtime data, 8 = ACPI, ...
      ResourceType = EFI_RESOURCE_MEMORY_RESERVED;
      Attr         = CT_MMIO_ATTRIBUTES;
      TypeName     = "reserved";
      break;
    }

    if (ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) {
      if (RamCount < ARRAY_SIZE (RamWindows)) {
        RamWindows[RamCount].Base = Start;
        RamWindows[RamCount].Size = Size;
        RamWindows[RamCount].Name = "SFI RAM";
        RamCount++;
      }

      //
      // The main window is the one covering the loaded firmware image; the
      // permanent PEI window is validated against it below.
      //
      if ((MainLimit == 0) && (FdBase >= Start) && (FdBase < Limit)) {
        MainBase  = Start;
        MainLimit = Limit;
      }
    } else if (ResourceType == EFI_RESOURCE_MEMORY_MAPPED_IO) {
      if (MmapMmioCount < ARRAY_SIZE (MmapMmio)) {
        MmapMmio[MmapMmioCount].Base = Start;
        MmapMmio[MmapMmioCount].Size = Size;
        MmapMmio[MmapMmioCount].Name = TypeName;
        MmapMmioCount++;
      }
    }

    ReportResourceWindow (ResourceType, Attr, Start, Size, TypeName);
  }

  //
  // The DXE Core requires the FD image and every memory allocation HOB to be
  // covered by a SYSTEM_MEMORY descriptor (CoreAddMemoryDescriptor cannot
  // promote a range that no resource descriptor covers). If no SFI RAM entry
  // covers the firmware image the map is unusable - a firmware defect. There
  // is no constant fallback: the SFI table is authoritative, so stop.
  //
  if (MainLimit == 0) {
    DEBUG ((
      DEBUG_ERROR,
      "PlatformPei: no SFI RAM entry covers the firmware image at %lx, "
      "memory map unusable\n",
      FdBase
      ));
    ASSERT (FALSE);
    CpuDeadLoop ();
  }

  DEBUG ((
    DEBUG_INIT,
    "PlatformPei: SFI MMAP: %d entries -> main %lx-%lx, %d RAM windows, %d MMIO windows\n",
    (UINT32)SfiCount,
    MainBase,
    MainLimit,
    (UINT32)(RamCount - 1),
    (UINT32)MmapMmioCount
    ));

  //
  // Permanent PEI memory. The PCD value describes the board's preferred
  // window when it lies inside the main DRAM window; otherwise place it
  // dynamically just below the top of the main window (analogue of
  // board_get_usable_ram_top in the U-Boot Tangier reference) so the PCD can
  // never land in the reserved hole or over the firmware image.
  //
  if ((PeiMemBase < MainBase) || (PeiMemBase >= MainLimit)) {
    DEBUG ((
      DEBUG_ERROR,
      "PlatformPei: PcdPeiMemoryBase 0x%lx outside main window, placing PEI memory dynamically\n",
      PeiMemBase
      ));
    PeiMemBase = (MainLimit - SIZE_64MB) & ~(UINT64)(EFI_PAGE_SIZE - 1);
  }
  PeiMemSize = MainLimit - PeiMemBase;

  DEBUG ((
    DEBUG_INIT,
    "PlatformPei: permanent PEI memory %lx-%lx\n",
    PeiMemBase,
    PeiMemBase + PeiMemSize - 1
    ));

  //
  // Hand the window above the firmware image to the PEI Core as permanent
  // memory. The descriptor HOBs below cover a larger range (from the main
  // window base) so that the image and the temporary RAM are described too.
  //
  Status = PeiServicesInstallPeiMemory (PeiMemBase, PeiMemSize);
  ASSERT_EFI_ERROR (Status);

  //
  // Legacy low memory, required for the OS AP startup trampoline (see the
  // PcdLegacyMemory* entries in IntelPkg.dec for the rationale).
  //
  ReportResourceWindow (
    EFI_RESOURCE_SYSTEM_MEMORY,
    CT_SYSTEM_MEMORY_ATTRIBUTES,
    LegacyBase,
    LegacySize,
    "legacy real-mode"
    );

  //
  // PCI-device MMIO windows (never published by SFI). Skip any window that
  // overlaps an SFI-derived RAM or MMIO descriptor: the SFI table is the
  // bootloader's truth and overlapping resource descriptors would make the
  // DXE Core GCD initialisation assert.
  //
  for (Index = 0; Index < PciMmioCount; Index++) {
    Found = FALSE;
    for (RamIndex = 0; RamIndex < RamCount; RamIndex++) {
      if (MmapRangeOverlaps (
            PciMmio[Index].Base,
            PciMmio[Index].Size,
            RamWindows[RamIndex].Base,
            RamWindows[RamIndex].Size
            )) {
        Found = TRUE;
        break;
      }
    }
    for (MmioIndex = 0; !Found && (MmioIndex < MmapMmioCount); MmioIndex++) {
      if (MmapRangeOverlaps (
            PciMmio[Index].Base,
            PciMmio[Index].Size,
            MmapMmio[MmioIndex].Base,
            MmapMmio[MmioIndex].Size
            )) {
        Found = TRUE;
        break;
      }
    }
    if (Found) {
      DEBUG ((
        DEBUG_ERROR,
        "PlatformPei: PCI window %a (%lx-%lx) overlaps an SFI window, skipped\n",
        PciMmio[Index].Name,
        PciMmio[Index].Base,
        PciMmio[Index].Base + PciMmio[Index].Size - 1
        ));
      continue;
    }
    ReportResourceWindow (
      EFI_RESOURCE_MEMORY_MAPPED_IO,
      CT_MMIO_ATTRIBUTES,
      PciMmio[Index].Base,
      PciMmio[Index].Size,
      PciMmio[Index].Name
      );
  }

  //
  // Firmware carve-outs: allocation HOBs. Collect them up front so each one
  // can be validated both against the RAM windows and against its siblings.
  //
  CarveCount = 0;
  CarveOuts[CarveCount].Base = (UINT64)FixedPcdGet32 (PcdLegacyReservedBase);
  CarveOuts[CarveCount].Size = (UINT64)FixedPcdGet32 (PcdLegacyReservedSize);
  CarveOuts[CarveCount].Name = "legacy reserved";
  CarveCount++;
  CarveOuts[CarveCount].Base = (UINT64)FixedPcdGet32 (PcdLegacyTopBase);
  CarveOuts[CarveCount].Size = (UINT64)FixedPcdGet32 (PcdLegacyTopSize);
  CarveOuts[CarveCount].Name = "legacy top";
  CarveCount++;
  CarveOuts[CarveCount].Base = FdBase;
  CarveOuts[CarveCount].Size = FdSize;
  CarveOuts[CarveCount].Name = "firmware image";
  CarveCount++;
  CarveOuts[CarveCount].Base = TempRamBase;
  CarveOuts[CarveCount].Size = TempRamSize;
  CarveOuts[CarveCount].Name = "SEC/PEI temp RAM";
  CarveCount++;
  CarveOuts[CarveCount].Base = ConsoleBase;
  CarveOuts[CarveCount].Size = SIZE_4KB;
  CarveOuts[CarveCount].Name = "console state";
  CarveCount++;

  for (Index = 0; Index < CarveCount; Index++) {
    ValidateCarveOut (
      CarveOuts[Index].Base,
      CarveOuts[Index].Size,
      RamWindows,
      RamCount,
      CarveOuts[Index].Name
      );
  }

  //
  // The DXE Core promotes each allocation HOB by allocating the exact range
  // (EfiReservedMemoryType / AllocateAddress); the second and later of two
  // overlapping promotions hit pages that are already allocated and
  // CoreConvertPages() bails out with "incompatible memory types" - the
  // ConvertPages lesson recorded in the note below. Detect any such overlap
  // here instead of relying on it at hand-off time.
  //
  for (RamIndex = 0; RamIndex < CarveCount; RamIndex++) {
    for (MmioIndex = RamIndex + 1; MmioIndex < CarveCount; MmioIndex++) {
      if (MmapRangeOverlaps (
            CarveOuts[RamIndex].Base,
            CarveOuts[RamIndex].Size,
            CarveOuts[MmioIndex].Base,
            CarveOuts[MmioIndex].Size
            )) {
        DEBUG ((
          DEBUG_ERROR,
          "PlatformPei: carve-outs %a and %a overlap\n",
          CarveOuts[RamIndex].Name,
          CarveOuts[MmioIndex].Name
          ));
      }
    }
  }

  //
  // The firmware image itself must never be allocated over or freed: the BFV
  // and every XIP PEIM still live there when DXE starts. The SEC/PEI
  // temporary RAM (stack + heap used before permanent memory) sits inside the
  // main DRAM window below the permanent memory window, so it must be carved
  // out explicitly. The legacy carve-outs protect the IVT/BDA and the
  // VGA/ISA/option-ROM hole. The console-state page at PcdConsoleStateBase is
  // the shared FrameBufferSerialPortLib cursor state (see IntelPkg.dec) and
  // must never be handed to the OS: it is written by every phase
  // (SEC/PEI/DXE/BDS). All five are EfiReservedMemoryType for the above
  // reasons.
  //
  for (Index = 0; Index < CarveCount; Index++) {
    BuildMemoryAllocationHob (
      CarveOuts[Index].Base,
      CarveOuts[Index].Size,
      EfiReservedMemoryType
      );
  }

  //
  // NOTE: do NOT add a sixth allocation HOB covering the whole
  // 0x01000000..PeiMemBase window. It used to be reserved here as a
  // "bootloader owned" range, but that HOB fully contains the firmware image
  // and the SEC/PEI temporary RAM HOBs built above. The DXE Core promotes
  // memory allocation HOBs one by one with CoreAllocatePages
  // (EfiReservedMemoryType, AllocateAddress); the second and third
  // overlapping promotions hit pages that are already allocated, so
  // CoreConvertPages() bails out with
  //
  //   ConvertPages: incompatible memory types
  //   ConvertPages: range ... covers multiple entries
  //
  // and the corresponding descriptor is left inconsistent in the GCD/UEFI
  // memory map. That is exactly the message seen right before the intermittent
  // reset while loading bootia32.efi. The firmware image and the temporary
  // RAM - the only parts of this window that must survive into DXE - are
  // reserved individually and non-overlappingly above; the remainder of the
  // window is genuinely free once the primary bootloader has handed control
  // over, so leave it as ordinary system memory.
  //

  //
  // The whole 4 GiB is addressable on this IA-32 part.
  //
  BuildCpuHob (32, 16);
}

/**
  Entry point of the platform PEIM.

  The first three actions were moved here from the (now generic) SEC module:
  PlatformPei is the first PEIM dispatched ([Depex] TRUE), so it still runs
  before anything can stall the local APIC timer, before DxeIpl creates the
  NX page tables, and inside the SCU watchdog's self-reset window.
**/
EFI_STATUS
EFIAPI
PlatformPeiEntryPoint (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "PlatformPei: Clover Trail+ (Atom Z25xx)\n"));

  //
  // These must run before anything can stall (APIC timer), before DxeIpl
  // builds the NX-capable PAE page tables (XD) and within the SCU watchdog
  // window. As the [Depex] TRUE PEIM, PlatformPei dispatches before every
  // other PEIM.
  //
  EnableExecuteDisable ();

  //
  // TimerLib here is the local-APIC instance and every stall in PEI/DXE
  // (CpuDxe, Metronome, BDS) goes through it. The stock bootloader leaves
  // the APIC timer init count at 0, which makes InternalX86Delay() ASSERT.
  // Program it free-running (divide by 1, max init count, periodic) with the
  // interrupt masked so it is only ever used as a counter.
  //
  InitializeApicTimer (1, MAX_UINT32, TRUE, 0);
  DisableApicTimerInterrupt ();

  DisableScuWatchdog ();

  Status = PeiServicesSetBootMode (BOOT_WITH_FULL_CONFIGURATION);
  ASSERT_EFI_ERROR (Status);

  Status = PeiServicesInstallPpi (&mPpiBootMode);
  ASSERT_EFI_ERROR (Status);

  //------------------------------------------------------------------------------
  // Memory map (SFI-driven, firmware-owned regions from PCDs)
  //------------------------------------------------------------------------------
  PlatformPeiInstallMemoryMap ();

  Status = PeiServicesInstallPpi (&mPpiMemoryDiscovered);
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
