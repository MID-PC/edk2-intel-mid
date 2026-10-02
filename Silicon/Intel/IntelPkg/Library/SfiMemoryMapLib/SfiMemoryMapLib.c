/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/SfiMemoryMapLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#define SFI_SIG_MMAP  "MMAP"

/**
  Halt on a missing MMAP table.

  All platforms linking this library are SFI platforms; a bootloader that hands
  over without MMAP is a firmware defect. Log, assert, and stop rather than
  fabricate a memory map. Non-SFI SoCs link a different library, so this branch
  is never expected.

  Never returns.
**/
STATIC
VOID
SfiMmapMissing (
  VOID
  )
{
  DEBUG ((
    DEBUG_ERROR,
    "SfiMemoryMapLib: no SFI MMAP table in 0x%llx-0x%llx; "
    "the bootloader must publish SFI tables on this platform\n",
    (UINT64)SFI_SEARCH_BASE,
    (UINT64)SFI_SEARCH_END
    ));

  ASSERT (FALSE);
  CpuDeadLoop ();
}

EFI_STATUS
EFIAPI
SfiGetMmap (
  OUT SFI_MMAP_TABLE  *Mmap
  )
{
  CONST SFI_TABLE_HEADER  *MmapTable;
  UINTN                   NumEntries;

  Mmap->EntryCount = 0;

  MmapTable = SfiFindTable (SFI_SIG_MMAP);
  if (MmapTable == NULL) {
    SfiMmapMissing ();
  }

  NumEntries = (MmapTable->Len - sizeof (*MmapTable)) / sizeof (SFI_MMAP_ENTRY);
  if (NumEntries > SFI_MAX_MMAP_ENTRIES) {
    NumEntries = SFI_MAX_MMAP_ENTRIES;
  }

  CopyMem (
    Mmap->Entry,
    (CONST UINT8 *)MmapTable + sizeof (*MmapTable),
    NumEntries * sizeof (SFI_MMAP_ENTRY)
    );
  Mmap->EntryCount = NumEntries;
  return EFI_SUCCESS;
}

/**
  Translate one SFI MMAP entry type into a memory region kind.
**/
STATIC
PLATFORM_MEMORY_KIND
SfiEntryToKind (
  IN UINT32  Type
  )
{
  switch (Type) {
  case SFI_MMAP_TABLE_TYPE_RAM:
    return PlatformMemorySystemMemory;

  case SFI_MMAP_TABLE_TYPE_MMIO:
    return PlatformMemoryMappedIo;

  case SFI_MMAP_TABLE_TYPE_IO:
    return PlatformMemoryIo;

  default:
    //
    // Type 6 (reserved) and type 2 (ACPI) both mean "there is a reason this is
    // not RAM" and neither has a more specific kind to report.
    //
    return PlatformMemoryReserved;
  }
}

VOID
EFIAPI
SfiDiscoverMemoryRegions (
  OUT PLATFORM_MEMORY_REGION  *Regions,
  IN  UINTN                   Capacity,
  OUT UINTN                   *RegionCount
  )
{
  SFI_MMAP_TABLE  Mmap;
  UINTN           Index;
  UINTN           Count;
  UINT64          Start;
  UINT64          Size;
  UINT64          Limit;

  SfiGetMmap (&Mmap);

  Count = 0;
  for (Index = 0; Index < Mmap.EntryCount; Index++) {
    Start = Mmap.Entry[Index].PhysStart;
    Size  = Mmap.Entry[Index].Pages << EFI_PAGE_SHIFT;

    if (Size == 0) {
      continue;
    }
    if (Start > (MAX_UINT64 - Size)) {
      DEBUG ((
        DEBUG_ERROR,
        "SfiMemoryMapLib: entry %d wraps the address space\n",
        (UINT32)Index
        ));
      continue;
    }
    Limit = Start + Size;

    //
    // Omit real-mode entries; the legacy window is added by the caller
    //
    if (Limit <= SFI_MMAP_SUB_MEGABYTE_LIMIT) {
      DEBUG ((
        DEBUG_VERBOSE,
        "SfiMemoryMapLib: entry %d (%lx-%lx) covered by legacy window\n",
        (UINT32)Index,
        Start,
        Limit
        ));
      continue;
    }

    if (Count >= Capacity) {
      DEBUG ((
        DEBUG_ERROR,
        "SfiMemoryMapLib: MMAP has more windows than the buffer holds, truncating\n"
        ));
      break;
    }

    Regions[Count].Base = Start;
    Regions[Count].Size = Size;
    Regions[Count].Kind = SfiEntryToKind (Mmap.Entry[Index].Type);
    Regions[Count].Name = "SFI";
    Count++;
  }

  DEBUG ((
    DEBUG_INIT,
    "SfiMemoryMapLib: MMAP %d entries -> %d windows\n",
    (UINT32)Mmap.EntryCount,
    (UINT32)Count
    ));

  *RegionCount = Count;
}
