/** @file
  SFI (Simple Firmware Interface) memory map discovery.

  The primary bootloader of an Intel MID publishes a SYST table in the legacy
  BIOS area (0x000E0000-0x00100000) whose pointer list leads to its other SFI
  tables, among them MMAP. The MMAP table describes the whole DRAM/MMIO layout
  (types 7 = conventional RAM, 6 = reserved, 11 = MMIO). Reading it lets the
  reported firmware memory map follow the bootloader's view of the board (RAM
  size, reserved windows) instead of a hard-coded layout.

  The search procedure, table layout and validation follow the SFI 1.0
  specification and mirror the Linux kernel reference implementation
  (drivers/sfi/sfi_core.c), which is shared across all Intel Atom MID SoC
  generations (Menlow/Moorestown, Medfield, Cloverview, ...).

  This library REQUIRES a bootloader-published SFI table: when the SYST/MMAP
  tables cannot be found it logs an error and asserts (CpuDeadLoop stop).
  Every platform that links this library is an SFI platform; a MID SoC that
  does not publish SFI tables (e.g. SoFIA) supplies its own memory map
  library instead.

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

//
// Number of pointer entries a SYST table can carry given the search-window
// bound enforced on every followed pointer.
//
#define SFI_SYST_MAX_POINTERS  \
  (SFI_SEARCH_SIZE / sizeof (UINT64))

/**
  Validate an SFI table header.

  @param[in] Header  Table header to validate.

  @retval TRUE   The header is a self-consistent SFI table (length within the
                 search window and a correct 8-bit checksum).
  @retval FALSE  Length, bounds or checksum sanity failed.
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

  //
  // The whole table must fit inside the legacy SFI area and be no smaller
  // than its own header.
  //
  if ((Header->Len < sizeof (*Header)) ||
      (Header->Len > (SFI_SEARCH_BASE + SFI_SEARCH_SIZE - TableAddr))) {
    return FALSE;
  }

  //
  // SFI tables end with a checksum that makes all bytes sum to zero.
  //
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
  Scan the legacy BIOS area for a table with the requested SFI signature.

  The spec requires a 16-byte-aligned search of 0x000E0000-0x00100000,
  starting at the low address, stopping at the first valid table.

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

  The platforms that link this library are SFI platforms; a primary
  bootloader that hands over without publishing SYST/MMAP in the legacy BIOS
  area is a firmware defect and the platform cannot describe its own memory.
  Log, assert, and stop instead of letting a consumer fall into a fabricated
  map. Devices that genuinely have no SFI (e.g. SoFIA) link a different memory
  map library, so this branch is never expected.

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

    //
    // Only follow pointers that stay inside the legacy SFI area; anything
    // else could be garbage pointing at unimplemented IO space.
    //
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

  //
  // SYST was found but no pointer led to a valid MMAP table: the bootloader
  // published an incomplete table set. Same firmware defect as a missing
  // table - stop.
  //
  SfiMmapMissing ();

  //
  // Unreachable: SfiMmapMissing() never returns. Kept so the EFI_STATUS
  // return contract is explicit to tools/compilers.
  //
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

  //
  // Keep the merged list sorted by base so debug output reads in address
  // order. Stable insertion sort (equal bases keep their insertion order).
  //
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