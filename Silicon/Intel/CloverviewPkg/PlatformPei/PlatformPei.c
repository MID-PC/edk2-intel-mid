/** @file
  Platform PEIM for Clover Trail+ (Atom Z25xx).

  The primary bootloader has already trained DRAM, configured the SCU and left
  the display engine scanning out of 0x3F000000. This PEIM therefore only:

    1. Reads the SFI MMAP table the bootloader left in the legacy BIOS area
       (0x000E0000-0x00100000) and reports DRAM and every MMIO window from
       it, falling back to the hard-coded layout below when the table is
       missing or invalid.
    2. Reports the permanent system memory to the PEI Core.
    3. Signals boot mode and memory discovery.

  Memory map (SFI MMAP from sfi-tables/MMAP, cross-checked with
  iomem_A502CG.txt).  SFI types: 7 = conventional, 6 = runtime data,
  11 = MMIO.

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
    40000000-4fffffff  MMIO          GPU (00:02.0)
    df800000-dfffffff  MMIO          00:03.0 / 00:02.0 (pvrsrvkm)
    fa000000-fdffffff  MMIO          00:06.0
    fec00000-fec00fff  MMIO          IOAPIC
    fee00000-fee00fff  MMIO          Local APIC
    ff000000-ffffffff  MMIO          south complex (SCU, HSU, SDHCI, I2C...)

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
// Fallback DRAM windows, used only when the live SFI MMAP table at 0x000E0000
// cannot be read. SFI normally drives these ranges; the firmware-owned regions
// it does not describe (firmware image, SEC/PEI temporary RAM, permanent PEI
// window, legacy AP-trampoline carve, shared console state) are PCD-defined.
//
#define CT_MAIN_MEMORY_BASE    0x01000000ULL
#define CT_MAIN_MEMORY_LIMIT   0x36FF0000ULL
#define CT_MAIN_MEMORY_SIZE    (CT_MAIN_MEMORY_LIMIT - CT_MAIN_MEMORY_BASE)

#define CT_LOW_MEMORY_BASE     0x00100000ULL
#define CT_LOW_MEMORY_SIZE     (0x00D00000ULL - CT_LOW_MEMORY_BASE)

//
// 379fd400-3eeff3ff System RAM, page aligned inwards.
//
#define CT_HIGH_MEMORY_BASE    0x379FE000ULL
#define CT_HIGH_MEMORY_SIZE    (0x3EEFF000ULL - CT_HIGH_MEMORY_BASE)

//
// SFI discovery moved to IntelPkg Library/SfiMemoryMapLib (see SfiGetMmap()).
//

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
// Every MMIO window of the platform, in ascending order (fallback list; the
// DRAM-side rows are duplicated by the type-11 entries of the live SFI MMAP
// table, completed with mDeviceMmioRegions below when SFI drives the map).
// None of these may overlap each other or a system memory descriptor, or the
// DXE Core GCD map initialisation will assert.
//
STATIC CONST SFI_MMIO_REGION  mMmioRegions[] = {
  { 0x00D00000ULL, 0x00300000ULL, "low reserved"      },
  { 0x36FF0000ULL, 0x00A0D000ULL, "reserved hole"     }, // 36ff0000-379fcfff
  { 0x379FD000ULL, 0x00001000ULL, "ram buffer"        }, // 379fd000-379fdfff
  { 0x3EF00000ULL, 0x00100000ULL, "PCI MMCONFIG"      },
  { 0x3F000000ULL, 0x01000000ULL, "framebuffer"       }, // GOP base lives here
  { 0x40000000ULL, 0x10000000ULL, "GPU 00:02.0"       },
  { 0xDF800000ULL, 0x00800000ULL, "00:03.0 / 00:02.0" },
  { 0xFA000000ULL, 0x04000000ULL, "00:06.0"           },
  { 0xFEC00000ULL, 0x00001000ULL, "IOAPIC"            },
  { 0xFEE00000ULL, 0x00001000ULL, "Local APIC"        },
  { 0xFF000000ULL, 0x01000000ULL, "south complex"     }
};

//
// Device MMIO windows the SFI MMAP table does not publish (their BARs were
// taken from the stock /proc/iomem dump). Merged into the MMIO descriptor
// list when the map comes from the live SFI table.
//
STATIC CONST SFI_MMIO_REGION  mDeviceMmioRegions[] = {
  { 0x40000000ULL, 0x10000000ULL, "GPU 00:02.0"       },
  { 0xDF800000ULL, 0x00800000ULL, "00:03.0 / 00:02.0" },
  { 0xFA000000ULL, 0x04000000ULL, "00:06.0"           }
};

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
      Model = ((RegEax >> 12) & 0xF) << 4 |         // extended model, bits 15:12
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

  The DRAM windows and the MMIO windows come from the live SFI MMAP table when
  it is present, with the hard-coded layout above kept as fallback. The
  firmware-owned regions SFI does not describe - the firmware image, the
  SEC/PEI temporary RAM, the permanent PEI window, the legacy AP-trampoline
  carve and the shared framebuffer console state - are always PCD-defined.
**/
STATIC
VOID
PlatformPeiInstallMemoryMap (
  VOID
  )
{
  EFI_STATUS         Status;
  UINTN              Index;
  UINTN              MMapCount;
  BOOLEAN            UseSfi;
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
  UINT64             MainSize;
  UINT64             LowBase;
  UINT64             LowLimit;
  UINT64             LowSize;
  UINT64             HighBase;
  UINT64             HighLimit;
  UINT64             HighSize;
  SFI_MMAP_TABLE     SfiMmap;
  UINTN              SfiCount;
  SFI_MMIO_REGION    MMap[SFI_MAX_MMAP_ENTRIES + ARRAY_SIZE (mDeviceMmioRegions)];

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
  // Derive the DRAM map from the SFI MMAP table. Each window is anchored to a
  // firmware address SFI does not know about but whose property the
  // bootloader guarantees: the main window must cover the loaded firmware
  // image, the low window must start right after the legacy window, and the
  // high island must cover the shared console state page.
  //
  UseSfi   = FALSE;
  SfiCount = 0;
  if (SfiGetMmap (&SfiMmap) == EFI_SUCCESS) {
    SfiCount = SfiMmap.EntryCount;
  }

  if (SfiCount > 0) {
    UseSfi = (SfiMmapFindConvRange (&SfiMmap, FdBase, &MainBase, &MainLimit)
              == EFI_SUCCESS);
  }
  if (UseSfi) {
    UseSfi = (SfiMmapFindConvRange (
                &SfiMmap,
                LegacyBase + LegacySize,
                &LowBase,
                &LowLimit
                ) == EFI_SUCCESS);
  }
  if (UseSfi) {
    UseSfi = (SfiMmapFindConvRange (
                &SfiMmap,
                ConsoleBase,
                &HighBase,
                &HighLimit
                ) == EFI_SUCCESS);
    if (UseSfi) {
      //
      // MMAP entries are not guaranteed page aligned; square the high island
      // up inwards.
      //
      HighBase  = ALIGN_VALUE (HighBase, EFI_PAGE_SIZE);
      HighLimit = HighLimit & ~(UINT64)(EFI_PAGE_SIZE - 1);
      UseSfi    = (HighBase < HighLimit);
    }
  }
  if (UseSfi) {
    UseSfi = (MainBase < MainLimit) &&
             (LowBase < LowLimit) &&
             (PeiMemBase < MainLimit);
  }

  if (UseSfi) {
    MainSize   = MainLimit - MainBase;
    LowSize    = LowLimit - LowBase;
    HighSize   = HighLimit - HighBase;
    PeiMemSize = MainLimit - PeiMemBase;

    DEBUG ((
      DEBUG_INIT,
      "PlatformPei: SFI MMAP: main %lx-%lx low %lx-%lx high %lx-%lx, PEI mem %lx-%lx\n",
      MainBase, MainLimit, LowBase, LowLimit, HighBase, HighLimit,
      PeiMemBase, PeiMemBase + PeiMemSize
      ));
  } else {
    MainBase   = CT_MAIN_MEMORY_BASE;
    MainLimit  = CT_MAIN_MEMORY_LIMIT;
    MainSize   = CT_MAIN_MEMORY_SIZE;
    LowBase    = CT_LOW_MEMORY_BASE;
    LowLimit   = LowBase + CT_LOW_MEMORY_SIZE;
    LowSize    = CT_LOW_MEMORY_SIZE;
    HighBase   = CT_HIGH_MEMORY_BASE;
    HighLimit  = HighBase + CT_HIGH_MEMORY_SIZE;
    HighSize   = CT_HIGH_MEMORY_SIZE;
    PeiMemSize = CT_MAIN_MEMORY_LIMIT - PeiMemBase;

    DEBUG ((DEBUG_WARN, "PlatformPei: SFI MMAP unavailable, using fallback layout\n"));
  }

  //
  // MMIO windows: when SFI drives the map, SfiMemoryMapLib merges the SFI
  // type-11 entries with the device windows the table omits and sorts the
  // result ascending; otherwise the full fallback list is used.
  //
  if (UseSfi) {
    Status = SfiMmapBuildMmioList (
               &SfiMmap,
               mDeviceMmioRegions,
               ARRAY_SIZE (mDeviceMmioRegions),
               MMap,
               ARRAY_SIZE (MMap),
               &MMapCount
               );
    ASSERT (Status == EFI_SUCCESS);
  } else {
    MMapCount = 0;
    for (Index = 0; Index < ARRAY_SIZE (mMmioRegions); Index++) {
      MMap[MMapCount++] = mMmioRegions[Index];
    }
  }

  //
  // Hand the window above the firmware image to the PEI Core as permanent
  // memory. The descriptor HOB below covers a larger range (from the main
  // window base) so that the image and the temporary RAM are described too.
  //
  Status = PeiServicesInstallPeiMemory (PeiMemBase, PeiMemSize);
  ASSERT_EFI_ERROR (Status);

  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    CT_SYSTEM_MEMORY_ATTRIBUTES,
    MainBase,
    MainSize
    );

  //
  // Legacy low memory, required for the OS AP startup trampoline (see the
  // PcdLegacyMemory* entries in IntelPkg.dec for the rationale).
  //
  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    CT_SYSTEM_MEMORY_ATTRIBUTES,
    LegacyBase,
    LegacySize
    );

  BuildMemoryAllocationHob (
    (EFI_PHYSICAL_ADDRESS)FixedPcdGet32 (PcdLegacyReservedBase),
    (UINT64)FixedPcdGet32 (PcdLegacyReservedSize),
    EfiReservedMemoryType
    );

  BuildMemoryAllocationHob (
    (EFI_PHYSICAL_ADDRESS)FixedPcdGet32 (PcdLegacyTopBase),
    (UINT64)FixedPcdGet32 (PcdLegacyTopSize),
    EfiReservedMemoryType
    );

  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    CT_SYSTEM_MEMORY_ATTRIBUTES,
    LowBase,
    LowSize
    );

  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    CT_SYSTEM_MEMORY_ATTRIBUTES,
    HighBase,
    HighSize
    );

  //
  // The firmware image itself must never be allocated over or freed: the BFV
  // and every XIP PEIM still live there when DXE starts. Reserved, not
  // BootServicesData.
  //
  BuildMemoryAllocationHob (
    FdBase,
    FdSize,
    EfiReservedMemoryType
    );

  //
  // SEC/PEI temporary RAM (stack + heap used before permanent memory). It sits
  // inside the main DRAM window and below the permanent memory window, so it
  // must be carved out explicitly.
  //
  BuildMemoryAllocationHob (
    TempRamBase,
    TempRamSize,
    EfiReservedMemoryType
    );

  //
  // Firmware console-state window at PcdConsoleStateBase (one 4 KiB page):
  //   FrameBufferSerialPortLib shared console state (FB_CONSOLE_STATE).
  //
  // RELOCATED from 0x020F0000. That address sits in the low DRAM window the
  // primary bootloader (OSIP loader / bootstub) uses as its own scratch before
  // it jumps to 0x01101000, and the Windows boot loader also allocates heavily
  // there, which is why the shared cursor state was corrupted on most boots.
  // The new base is the top page of the high DRAM island, which nothing on
  // this platform touches before or after the firmware runs, so the console
  // state survives the reset back into UEFI.
  //
  // This must be a real EfiReservedMemoryType range, not ordinary DRAM:
  // the console state is written by every phase (SEC/PEI/DXE/BDS) and must
  // not be handed to the OS.
  //
  // It is page aligned, 1 page long, and does not overlap the FD image
  // (0x01101000 + PcdFdSize) or the SEC/PEI temporary RAM (0x02000000 +
  // PcdSecPeiTemporaryRamSize), so the DXE Core promotes it exactly once and
  // no ConvertPages() conflict can occur.
  //
  BuildMemoryAllocationHob (
    (EFI_PHYSICAL_ADDRESS)FixedPcdGet32 (PcdConsoleStateBase),
    (UINT64)SIZE_4KB,
    EfiReservedMemoryType
    );

  //
  // NOTE: do NOT add a third allocation HOB covering the whole
  // 0x01000000..PeiMemBase window. It used to be reserved here as a
  // "bootloader owned" range, but that HOB fully contains both HOBs built
  // above (the FD image at 0x01101000 and the SEC/PEI temporary RAM at
  // 0x02000000). The DXE Core promotes memory allocation HOBs one by one with
  // CoreAllocatePages (EfiReservedMemoryType, AllocateAddress); the second and
  // third overlapping promotion hits pages that are already allocated, so
  // CoreConvertPages() bails out with
  //
  //   ConvertPages: incompatible memory types
  //   ConvertPages: range ... covers multiple entries
  //
  // and the corresponding descriptor is left in an inconsistent state in the
  // GCD/UEFI memory map. That is exactly the message seen right before the
  // intermittent reset while loading bootia32.efi (it also appears when
  // booting from the SD card, i.e. it is not USB specific): the OS loader
  // allocates from a range the memory map describes inconsistently.
  //
  // The FD image and the temporary RAM - the only parts of this window that
  // must survive into DXE - are already reserved individually and
  // non-overlappingly above. The remainder of the window is genuinely free
  // once the primary bootloader has handed control over, so leave it as
  // ordinary system memory.
  //

  //
  // MMIO windows. The framebuffer (0x3F000000) is one of them: it sits inside
  // the 3f000000-3fffffff reserved window of the SFI map.
  //
  for (Index = 0; Index < MMapCount; Index++) {
    DEBUG ((
      DEBUG_VERBOSE,
      "PlatformPei: MMIO %a %lx-%lx\n",
      MMap[Index].Name,
      MMap[Index].Base,
      MMap[Index].Base + MMap[Index].Size - 1
      ));

    BuildResourceDescriptorHob (
      EFI_RESOURCE_MEMORY_MAPPED_IO,
      CT_MMIO_ATTRIBUTES,
      MMap[Index].Base,
      MMap[Index].Size
      );
  }

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
