/** @file
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

// Real-mode memory top
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

// Cloverview/Penwell SCU IPC-1 block
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
  Stop the SCU kernel watchdog with IPCMSG_WATCHDOG_TIMER / sub-command 0
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
  Enable the XD (execute-disable) feature before any page tables exist
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
  SfiMemoryMapLib memory map install
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

  // RAM windows for carve-out validation
  RamWindows[0].Base = LegacyBase;
  RamWindows[0].Size = LegacySize;
  RamWindows[0].Name = "legacy real-mode";
  RamCount = 1;

  // PCI-device MMIO windows from per-board PCDs
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

  // Walk the live SFI MMAP table
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

    // Omit real-mode entries
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

    // MMAP entries are not guaranteed page aligned
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

    case 12: // SFI type 12 = I/O window
      ResourceType = EFI_RESOURCE_IO;
      Attr         = CT_MMIO_ATTRIBUTES;
      TypeName     = "I/O";
      break;

    default:
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

  // Check if UEFI FD is not covered by the SFI map
  if (MainLimit == 0) {
    DEBUG ((
      DEBUG_ERROR,
      "PlatformPei: no SFI RAM entry covers the FD at %lx, "
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

  Status = PeiServicesInstallPeiMemory (PeiMemBase, PeiMemSize);
  ASSERT_EFI_ERROR (Status);

  // Real-mode low memory, required for the OS AP startup trampoline
  ReportResourceWindow (
    EFI_RESOURCE_SYSTEM_MEMORY,
    CT_SYSTEM_MEMORY_ATTRIBUTES,
    LegacyBase,
    LegacySize,
    "legacy real-mode"
    );

  // Skip any MMIO window that overlaps an SFI-derived RAM or MMIO descriptor
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

  // Firmware carve-outs
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

  // Prevent overwriting/freeing the UEFI FD region
  for (Index = 0; Index < CarveCount; Index++) {
    BuildMemoryAllocationHob (
      CarveOuts[Index].Base,
      CarveOuts[Index].Size,
      EfiReservedMemoryType
      );
  }

  BuildCpuHob (32, 16);
}

/**
  Entry point of the platform PEIM
**/
EFI_STATUS
EFIAPI
PlatformPeiEntryPoint (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "PlatformPei: Cloverview PEIM entry\n"));

  EnableExecuteDisable ();

  // IAFW leaves the APIC timer init count at 0
  InitializeApicTimer (1, MAX_UINT32, TRUE, 0);
  DisableApicTimerInterrupt ();

  DisableScuWatchdog ();

  Status = PeiServicesSetBootMode (BOOT_WITH_FULL_CONFIGURATION);
  ASSERT_EFI_ERROR (Status);

  Status = PeiServicesInstallPpi (&mPpiBootMode);
  ASSERT_EFI_ERROR (Status);

  PlatformPeiInstallMemoryMap ();

  Status = PeiServicesInstallPpi (&mPpiMemoryDiscovered);
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
