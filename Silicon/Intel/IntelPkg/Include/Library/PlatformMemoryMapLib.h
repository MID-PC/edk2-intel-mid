/** @file
  Platform memory map construction: turns discovered memory windows into
  resource descriptors and allocation HOBs. Discovery is the caller's job, so an
  SFI board and a device-tree board can each feed this their own walk.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef PLATFORM_MEMORY_MAP_LIB_H_
#define PLATFORM_MEMORY_MAP_LIB_H_

#include <PiPei.h>

#define PLATFORM_MAX_MEMORY_REGIONS  64

// Windows the platform must reserve on its own account. Sized with slack over
// what any single platform currently claims, so an added region is not a
// buffer-size change in every caller.
#define PLATFORM_MAX_DEVICE_MMIO   4
#define PLATFORM_MAX_CARVE_OUTS    10

/**
  How a discovered window is reported to the firmware and the OS.
**/
typedef enum {
  //
  // Available to the OS as ordinary memory.
  //
  PlatformMemorySystemMemory,

  //
  // Register aperture. Reported as EFI_RESOURCE_MEMORY_MAPPED_IO.
  //
  PlatformMemoryMappedIo,

  //
  // Port I/O aperture. Reported as EFI_RESOURCE_IO.
  //
  PlatformMemoryIo,

  //
  // Reserved but not device memory. Reported as EFI_RESOURCE_MEMORY_RESERVED.
  //
  PlatformMemoryReserved
} PLATFORM_MEMORY_KIND;

/**
  One window in the memory map.

  Name is diagnostic only, appearing in DEBUG output to identify a window that
  fails validation. It is never handed to the OS.
**/
typedef struct {
  CONST CHAR8            *Name;
  UINT64                 Base;
  UINT64                 Size;
  PLATFORM_MEMORY_KIND   Kind;
} PLATFORM_MEMORY_REGION;

/**
  EFI_MEMORY_TYPE to allocate for a carve-out.

  Most firmware regions are EfiReservedMemoryType; the legacy real-mode block is
  sometimes EfiBootServicesData so a boot-time int10h shim can claim it.
**/
typedef struct {
  CONST CHAR8      *Name;
  UINT64           Base;
  UINT64           Size;
  EFI_MEMORY_TYPE  MemoryType;
} PLATFORM_MEMORY_CARVE_OUT;

/**
  Build the platform memory map and install permanent PEI memory.
  Discovered is reported in order, then unpublished DeviceMmio windows that do not
  overlap it. CarveOuts are checked against the reported memory, then reserved.

  @param[in] Discovered        Windows found by the caller's discovery pass.
  @param[in] DiscoveredCount   Number of entries in Discovered.
  @param[in] DeviceMmio        Device windows discovery cannot know about, typically PCI BARs. NULL when DeviceMmioCount is 0.
  @param[in] DeviceMmioCount   Number of entries in DeviceMmio.
  @param[in] CarveOuts         Firmware-owned windows to reserve.
  @param[in] CarveOutCount     Number of entries in CarveOuts.
  @retval EFI_SUCCESS            The map was built and PEI memory installed.
  @retval EFI_INVALID_PARAMETER  Discovered is NULL or empty.
  @retval EFI_NOT_FOUND          No discovered system memory window contains PcdFdBaseAddress, so there is nowhere to put permanent PEI memory. Nothing was reserved and no CPU HOB was built.
**/
EFI_STATUS
EFIAPI
BuildPlatformMemoryMap (
  IN CONST PLATFORM_MEMORY_REGION  *Discovered,
  IN UINTN                         DiscoveredCount,
  IN CONST PLATFORM_MEMORY_REGION  *DeviceMmio,
  IN UINTN                         DeviceMmioCount,
  IN CONST PLATFORM_MEMORY_CARVE_OUT  *CarveOuts,
  IN UINTN                         CarveOutCount
  );

/**
  Collect this platform's PCI-device MMIO windows.
  The bootloader map does not describe BARs, so these come from PCDs, with the PCI
  address in the name recording how. A zero-size PCD means "no such device here".

  @param[out] DeviceMmio  Window list; caller provides storage.
  @param[in]  Capacity    Entries DeviceMmio can hold. Windows past this are dropped with a DEBUG warning.
  @return Number of entries written to DeviceMmio.
**/
UINTN
EFIAPI
CollectPlatformDeviceMmio (
  OUT PLATFORM_MEMORY_REGION  *DeviceMmio,
  IN  UINTN                   Capacity
  );

/**
  Collect the windows the firmware must keep off limits to the OS.
  All PCD-driven, and all inside DRAM the bootloader already reported. Zero-length
  entries are skipped, which is how an absent window avoids a false hole.

  @param[out] CarveOuts  Window list; caller provides storage.
  @param[in]  Capacity   Entries CarveOuts can hold. Windows past this are dropped with a DEBUG warning.
  @return Number of entries written to CarveOuts.
**/
UINTN
EFIAPI
CollectPlatformCarveOuts (
  OUT PLATFORM_MEMORY_CARVE_OUT  *CarveOuts,
  IN  UINTN                      Capacity
  );

#endif // PLATFORM_MEMORY_MAP_LIB_H_