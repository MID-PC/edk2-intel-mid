/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/SfiMemoryMapLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#define SFI_SEARCH_BASE    0x000E0000ULL
#define SFI_SEARCH_SIZE    0x00020000ULL
#define SFI_SEARCH_STRIDE  16

#define SFI_SIG_SYST       "SYST"
#define SFI_SIG_MMAP       "MMAP"

// SYST pointer entries bounded by the search window.
#define SFI_SYST_MAX_POINTERS  \
  (SFI_SEARCH_SIZE / sizeof (UINT64))

/**
  Validate an SFI table header.

  @param[in] Header  Table header to validate.

  @retval TRUE   Length within the search window and correct checksum.
  @retval FALSE  Length, bounds or checksum failed.
**/
STATIC
BOOLEAN
SfiTableIsValid (
  IN CONST SFI_TABLE_HEADER  *Header
  )
{
  UINTN             TableAddr;
  UINTN             Index;
  CONST UINT8       *Bytes;
  UINT8             Sum;

  TableAddr = (UINTN)Header;

  // Table must fit inside the legacy SFI area
  if ((Header->Len < sizeof (*Header)) ||
      (Header->Len > (SFI_SEARCH_BASE + SFI_SEARCH_SIZE - TableAddr))) {
    return FALSE;
  }

  // SFI checksum: all bytes must sum to zero
  Bytes = (CONST UINT8 *)Header;
  Sum   = 0;
  for (Index = 0; Index < Header->Len; Index++) {
    Sum += Bytes[Index];
  }

  return (Sum == 0);
}

/**
  Check that a header carries the given signature and validates.

  @param[in] Header    Table header to check.
  @param[in] Signature Four-character table signature (e.g. "SYST").

  @retval TRUE   The header matches the signature and validates.
  @retval FALSE  Otherwise.
**/
STATIC
BOOLEAN
SfiTableIs (
  IN CONST SFI_TABLE_HEADER  *Header,
  IN CONST CHAR8             *Signature
  )
{
  return (CompareMem (Header->Sig, Signature, 4) == 0) &&
         SfiTableIsValid (Header);
}

/**
  Scan the legacy BIOS area for the first valid table with the given
  signature (16-byte-aligned walk of 0x000E0000-0x00100000).

  @param[in] Signature  Four-character table signature (e.g. "SYST").

  @return Pointer to the validated table, or NULL when not found.
**/
STATIC
CONST SFI_TABLE_HEADER *
SfiFindTable (
  IN CONST CHAR8  *Signature
  )
{
  UINTN                         Address;
  CONST SFI_TABLE_HEADER        *Header;

  for (Address = SFI_SEARCH_BASE;
       Address < (SFI_SEARCH_BASE + SFI_SEARCH_SIZE);
       Address += SFI_SEARCH_STRIDE) {
    Header = (CONST SFI_TABLE_HEADER *)(UINTN)Address;

    if (SfiTableIs (Header, Signature)) {
      return Header;
    }
  }

  return NULL;
}

/**
  Halt on a missing SFI table.

  All platforms linking this library are SFI platforms; a bootloader that
  hands over without SYST/MMAP is a firmware defect. Log, assert, and stop
  rather than fabricate a memory map. Non-SFI SoCs link a
  different library, so this branch is never expected.

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
    "SfiMemoryMapLib: no SFI SYST/MMAP table in 0x%llx-0x%llx; "
    "the bootloader must publish SFI tables on this platform\n",
    (UINT64)SFI_SEARCH_BASE,
    (UINT64)(SFI_SEARCH_BASE + SFI_SEARCH_SIZE)
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
  CONST SFI_TABLE_HEADER  *Syst;
  CONST SFI_TABLE_HEADER  *MmapTable;
  CONST UINT64            *Pointer;
  UINTN                   PointerCount;
  UINTN                   Index;
  UINTN                   NumEntries;
  UINT64                  TableAddr;

  Mmap->EntryCount = 0;

  Syst = SfiFindTable (SFI_SIG_SYST);
  if (Syst == NULL) {
    SfiMmapMissing ();
  }

  PointerCount = (Syst->Len - sizeof (*Syst)) / sizeof (UINT64);
  if (PointerCount > SFI_SYST_MAX_POINTERS) {
    PointerCount = SFI_SYST_MAX_POINTERS;
  }

  for (Index = 0; Index < PointerCount; Index++) {
    Pointer = (CONST UINT64 *)((UINTN)Syst + sizeof (*Syst) +
                               Index * sizeof (UINT64));

    // Only follow pointers inside the legacy SFI area; others could be garbage.
    TableAddr = *Pointer;
    if ((TableAddr < SFI_SEARCH_BASE) ||
        (TableAddr > (SFI_SEARCH_BASE + SFI_SEARCH_SIZE -
                      sizeof (*MmapTable)))) {
      continue;
    }

    MmapTable = (CONST SFI_TABLE_HEADER *)(UINTN)TableAddr;
    if (!SfiTableIs (MmapTable, SFI_SIG_MMAP)) {
      continue;
    }

    NumEntries = (MmapTable->Len - sizeof (*MmapTable)) /
                 sizeof (SFI_MMAP_ENTRY);
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

  // SYST found but no pointer led to a valid MMAP =  incomplete table set
  SfiMmapMissing ();

  // Unreachable (SfiMmapMissing never returns); kept for the return contract
  return EFI_NOT_FOUND;
}

EFI_STATUS
EFIAPI
SfiMmapFindConvRange (
  IN  CONST SFI_MMAP_TABLE  *Mmap,
  IN  UINT64                Anchor,
  OUT UINT64                *Base,
  OUT UINT64                *Limit
  )
{
  UINTN   Index;
  UINT64  Start;
  UINT64  End;

  for (Index = 0; Index < Mmap->EntryCount; Index++) {
    if (Mmap->Entry[Index].Type == SFI_MMAP_TABLE_TYPE_RAM) {
      Start = Mmap->Entry[Index].PhysStart;
      End   = Start + (Mmap->Entry[Index].Pages << 12);

      if ((Anchor >= Start) && (Anchor < End)) {
        *Base  = Start;
        *Limit = End;
        return EFI_SUCCESS;
      }
    }
  }

  return EFI_NOT_FOUND;
}

EFI_STATUS
EFIAPI
SfiMmapBuildMmioList (
  IN  CONST SFI_MMAP_TABLE  *Mmap,
  IN  CONST SFI_MMIO_REGION *DeviceRegions,
  IN  UINTN                 DeviceCount,
  OUT SFI_MMIO_REGION       *Mmio,
  IN  UINTN                 Capacity,
  OUT UINTN                 *MmioCount
  )
{
  UINTN         Index;
  UINTN         SortPos;
  UINTN         Count;
  SFI_MMIO_REGION  Key;

  Count = 0;

  for (Index = 0; Index < Mmap->EntryCount; Index++) {
    if (Mmap->Entry[Index].Type == SFI_MMAP_TABLE_TYPE_MMIO) {
      if (Count >= Capacity) {
        *MmioCount = 0;
        return EFI_BUFFER_TOO_SMALL;
      }

      Mmio[Count].Base = Mmap->Entry[Index].PhysStart;
      Mmio[Count].Size = Mmap->Entry[Index].Pages << 12;
      Mmio[Count].Name = "sfi mmio";
      Count++;
    }
  }

  for (Index = 0; Index < DeviceCount; Index++) {
    if (Count >= Capacity) {
      *MmioCount = 0;
      return EFI_BUFFER_TOO_SMALL;
    }

    Mmio[Count++] = DeviceRegions[Index];
  }

  // Stable insertion sort by base so equal bases keep insertion order.
  for (Index = 1; Index < Count; Index++) {
    Key     = Mmio[Index];
    SortPos = (INTN)(Index - 1);
    while ((SortPos >= 0) && (Mmio[SortPos].Base > Key.Base)) {
      Mmio[SortPos + 1] = Mmio[SortPos];
      SortPos--;
    }
    Mmio[SortPos + 1] = Key;
  }

  *MmioCount = Count;
  return EFI_SUCCESS;
}
