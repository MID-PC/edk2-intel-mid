/** @file
  Decode the memory map a Simple Firmware Interface bootloader publishes.

  Locating the table is SfiTableLib's job; this library only knows what an MMAP
  entry means, and turns entries into the PlatformMemoryMapLib vocabulary.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SFI_MEMORY_MAP_LIB_H_
#define SFI_MEMORY_MAP_LIB_H_

#include <Uefi/UefiBaseType.h>
#include <Library/SfiTableLib.h>
#include <Library/PlatformMemoryMapLib.h>

// SFI 1.0 MMAP entry types
#define SFI_MMAP_TABLE_TYPE_RAM      7
#define SFI_MMAP_TABLE_TYPE_RESERVED 6
#define SFI_MMAP_TABLE_TYPE_MMIO     11
#define SFI_MMAP_TABLE_TYPE_IO       12

// Upper bound on MMAP entries per bootloader table
#define SFI_MAX_MMAP_ENTRIES  32

// Packed MMAP payload: an array of the 36-byte entries below.
#pragma pack(1)

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

/**
  Real-mode memory top. Entries ending at or below this are inside the legacy low
  window, which the caller installs separately from its own PCDs, so they are
  dropped here rather than by every caller.
**/
#define SFI_MMAP_SUB_MEGABYTE_LIMIT  0x00100000ULL

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
  Walk the SFI MMAP into PlatformMemoryMapLib's window list.

  The SFI half of turning a bootloader table into windows. Sub-megabyte, zero-sized
  and wrapping entries are dropped.

  @param[out] Regions      Window list; caller provides storage.
  @param[in]  Capacity     Entries Regions can hold.
  @param[out] RegionCount  Number of entries written, never more than Capacity.
**/
VOID
EFIAPI
SfiDiscoverMemoryRegions (
  OUT PLATFORM_MEMORY_REGION  *Regions,
  IN  UINTN                   Capacity,
  OUT UINTN                   *RegionCount
  );

#endif // SFI_MEMORY_MAP_LIB_H_
