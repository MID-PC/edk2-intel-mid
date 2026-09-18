/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SFI_MEMORY_MAP_LIB_H_
#define SFI_MEMORY_MAP_LIB_H_

#include <Uefi/UefiBaseType.h>

// SFI 1.0 MMAP entry types
#define SFI_MMAP_TABLE_TYPE_RAM      7
#define SFI_MMAP_TABLE_TYPE_RESERVED 6
#define SFI_MMAP_TABLE_TYPE_MMIO     11

// Upper bound on MMAP entries per bootloader table
#define SFI_MAX_MMAP_ENTRIES  32

// Packed bootloader tables: header + payload of SFI_MMAP_ENTRYs
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

typedef struct {
  UINTN             EntryCount;
  SFI_MMAP_ENTRY    Entry[SFI_MAX_MMAP_ENTRIES];
} SFI_MMAP_TABLE;

// One MMIO window; used for device windows and the merged descriptor list.
typedef struct {
  UINT64          Base;
  UINT64          Size;
  CONST CHAR8     *Name;
} SFI_MMIO_REGION;

/**
  Locate and decode the SFI MMAP table

  @param[out] Mmap  Decoded MMAP table; caller provides storage.

  @retval EFI_SUCCESS  MMAP table found and decoded.
**/
EFI_STATUS
EFIAPI
SfiGetMmap (
  OUT SFI_MMAP_TABLE  *Mmap
  );

/**
  Find the conventional memory (type RAM) entry covering an anchor address

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

  Merges SFI MMAP type-MMIO entries with the platform's own device windows
  (SFI does not publish them) and sorts ascending by base address.

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
