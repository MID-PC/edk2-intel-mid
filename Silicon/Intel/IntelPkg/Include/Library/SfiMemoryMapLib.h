/** @file
  Library interface to discover and classify the SFI (Simple Firmware
  Interface) memory map published by an Intel MID primary bootloader.

  Implements the SFI 1.0 discovery walk common to every Intel Atom MID SoC
  generation (Menlow/Moorestown, Medfield, Cloverview, ...): a 16-byte scan of
  0x000E0000-0x00100000 for the SYST table, length- and checksum-based
  validation, then a walk of the SYST pointer list to the MMAP table. The MMAP
  entries describe the entire DRAM/MMIO layout (types 7 = RAM, 6 = reserved,
  11 = MMIO) exactly as the Linux kernel drivers/sfi/sfi_core.c reference
  implementation reads them.

  The library is firmware-independent: it consumes no PCDs and only returns
  the parsed table plus classification helpers. It is up to the platform
  firmware to turn the result into resource HOBs (which windows to report,
  which firmware-owned ranges to carve out, and so on).

  The library is SFI-mandatory: SfiGetMmap() never returns a failure. When
  the bootloader has not published the SYST/MMAP tables in the legacy BIOS
  area the library logs an error and asserts (CpuDeadLoop stop), because
  every platform that links this library is an SFI platform. A MID SoC that
  does not publish SFI tables (e.g. SoFIA) supplies its own memory map
  library instance instead of linking this one.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SFI_MEMORY_MAP_LIB_H_
#define SFI_MEMORY_MAP_LIB_H_

#include <Uefi/UefiBaseType.h>

//
// SFI MMAP entry types (SFI 1.0, see sfi.h in the Linux kernel).
//
#define SFI_MMAP_TABLE_TYPE_RAM      7
#define SFI_MMAP_TABLE_TYPE_RESERVED 6
#define SFI_MMAP_TABLE_TYPE_MMIO     11

//
// Upper bound on the number of MMAP entries a bootloader table may carry.
//
#define SFI_MAX_MMAP_ENTRIES  32

//
// Packed layout of the bootloader's tables. The header is followed by the
// table's own payload; in MMAP tables every 36-byte entry is a
// SFI_MMAP_ENTRY.
//
#pragma pack(1)

typedef struct {
  CHAR8   Sig[4];
  UINT32  Len;
  UINT8   Rev;
  UINT8   Csum;
  CHAR8   OemId[6];
  CHAR8   OemTableId[8];
} SFI_TABLE_HEADER;

typedef struct {
  UINT32  Type;
  UINT64  PhysStart;
  UINT64  VirtStart;
  UINT64  Pages;
  UINT64  Attrib;
} SFI_MMAP_ENTRY;

#pragma pack()

//
// A decoded MMAP table.
//
typedef struct {
  UINTN             EntryCount;
  SFI_MMAP_ENTRY    Entry[SFI_MAX_MMAP_ENTRIES];
} SFI_MMAP_TABLE;

//
// A single MMIO window (base + size + a name for debug output). Used both for
// the platform's own device windows and for the merged descriptor list.
//
typedef struct {
  UINT64          Base;
  UINT64          Size;
  CONST CHAR8     *Name;
} SFI_MMIO_REGION;

/**
  Locate and decode the SFI MMAP table.

  Scans the legacy BIOS area for the SYST table and follows its pointer list
  to the MMAP table, copying the memory entries out.

  This platform is an SFI platform, so a missing SYST/MMAP table is a fatal
  firmware defect: the function logs an error and asserts (CpuDeadLoop stop)
  instead of returning. It only returns when the table was decoded.

  @param[out] Mmap  Decoded MMAP table. The caller provides the storage.

  @retval EFI_SUCCESS  The MMAP table was found and decoded.
**/
EFI_STATUS
EFIAPI
SfiGetMmap (
  OUT SFI_MMAP_TABLE  *Mmap
  );

/**
  Find the conventional-memory (type RAM) entry covering an anchor address.

  @param[in]  Mmap     Decoded MMAP table from SfiGetMmap().
  @param[in]  Anchor   Address that must fall inside the entry.
  @param[out] Base     Base of the matched window.
  @param[out] Limit    One past the end of the matched window.

  @retval EFI_SUCCESS        A conventional entry covers Anchor.
  @retval EFI_NOT_FOUND      No conventional entry covers Anchor.
**/
EFI_STATUS
EFIAPI
SfiMmapFindConvRange (
  IN  CONST SFI_MMAP_TABLE  *Mmap,
  IN  UINT64                Anchor,
  OUT UINT64                *Base,
  OUT UINT64                *Limit
  );

/**
  Build the platform MMIO descriptor list.

  Merges the type-MMIO entries of the SFI MMAP table with the platform's own
  device windows (which SFI does not publish) and sorts the result ascending
  by base address so debug output reads in address order.

  @param[in]  Mmap          Decoded MMAP table from SfiGetMmap().
  @param[in]  DeviceRegions Platform device windows not described by SFI.
                            May be NULL when DeviceCount is 0.
  @param[in]  DeviceCount   Number of entries in DeviceRegions.
  @param[out] Mmio          Caller-provided buffer receiving the merged,
                            sorted list.
  @param[in]  Capacity      Number of entries Mmio can hold.
  @param[out] MmioCount     Number of entries written to Mmio.

  @retval EFI_SUCCESS           The list was built.
  @retval EFI_BUFFER_TOO_SMALL  Capacity is smaller than the number of merged
                                entries.
**/
EFI_STATUS
EFIAPI
SfiMmapBuildMmioList (
  IN  CONST SFI_MMAP_TABLE  *Mmap,
  IN  CONST SFI_MMIO_REGION *DeviceRegions,
  IN  UINTN                 DeviceCount,
  OUT SFI_MMIO_REGION       *Mmio,
  IN  UINTN                 Capacity,
  OUT UINTN                 *MmioCount
  );

#endif // SFI_MEMORY_MAP_LIB_H_