/** @file
  Platform memory map construction.

  Turns discovered memory windows into EDK2 resource descriptors and memory
  allocation HOBs. Discovery is the caller's job: this library has no opinion
  about where the map came from, so an SFI platform and a device-tree platform
  can each feed it their own walk.

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

  Name is diagnostic only: it appears in DEBUG output to identify a window that
  fails validation, and is never handed to the OS.
**/
typedef struct {
  CONST CHAR8            *Name;
  UINT64                 Base;
  UINT64                 Size;
  PLATFORM_MEMORY_KIND   Kind;
} PLATFORM_MEMORY_REGION;

/**
  EFI_MEMORY_TYPE to allocate for a carve-out.

  Most firmware regions are EfiReservedMemoryType. The legacy real-mode block
  is sometimes handed over as EfiBootServicesData instead so a boot-time int10h
  shim can claim it.
**/
typedef struct {
  CONST CHAR8      *Name;
  UINT64           Base;
  UINT64           Size;
  EFI_MEMORY_TYPE  MemoryType;
} PLATFORM_MEMORY_CARVE_OUT;

/**
  Build the platform memory map and install permanent PEI memory.

  Every window in Discovered is reported as a resource descriptor in the order
  given. DeviceMmio windows the bootloader did not publish are appended after
  them, skipping any that overlap something already reported. CarveOuts are
  validated against the reported system memory, then reserved.

  @param[in] Discovered        Windows found by the caller's discovery pass.
  @param[in] DiscoveredCount   Number of entries in Discovered.
  @param[in] DeviceMmio        Device windows the discovery pass cannot know
                               about, typically PCI BARs. May be NULL when
                               DeviceMmioCount is 0.
  @param[in] DeviceMmioCount   Number of entries in DeviceMmio.
  @param[in] CarveOuts         Firmware-owned windows to reserve.
  @param[in] CarveOutCount     Number of entries in CarveOuts.

  @retval EFI_SUCCESS            The map was built and PEI memory installed.
  @retval EFI_INVALID_PARAMETER  Discovered is NULL or empty.
  @retval EFI_NOT_FOUND          No discovered system memory window contains
                                 PcdFdBaseAddress, so there is nowhere to place
                                 permanent PEI memory. Nothing was reserved and
                                 no CPU HOB was built.
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

  The bootloader map does not describe device BARs, so these come from PCDs: a
  transcription of what each device decodes, with the PCI address in the name
  recording how that was established. A zero-size PCD means "this platform has no
  such device" and yields no window, which is how one DSC can describe a SoC
  missing an ISP or an EMAC.

  This is the default set, not the only possible one: a caller needing a
  different device set can pass its own DeviceMmio to BuildPlatformMemoryMap.

  @param[out] DeviceMmio  Window list; caller provides storage.
  @param[in]  Capacity    Entries DeviceMmio can hold. Windows past this are
                          dropped with a DEBUG warning.

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

  All PCD-driven: the legacy real-mode and reserved blocks, the firmware image,
  SEC/PEI temporary RAM, and the console state page. All of it falls inside a
  DRAM window the bootloader reported, so none of it would be reclaimed by simply
  not describing it. Zero-length entries are skipped by BuildPlatformMemoryMap,
  which is how a platform whose PCDs describe an absent window avoids reserving
  over a hole in its own map.

  This is the default set, not the only possible one: a caller needing a
  different carve-out set can pass its own CarveOuts to BuildPlatformMemoryMap.

  @param[out] CarveOuts  Window list; caller provides storage.
  @param[in]  Capacity   Entries CarveOuts can hold. Windows past this are
                         dropped with a DEBUG warning. Dropping one costs the OS
                         a reserved range but does not corrupt the map.

  @return Number of entries written to CarveOuts.
**/
UINTN
EFIAPI
CollectPlatformCarveOuts (
  OUT PLATFORM_MEMORY_CARVE_OUT  *CarveOuts,
  IN  UINTN                      Capacity
  );

#endif // PLATFORM_MEMORY_MAP_LIB_H_