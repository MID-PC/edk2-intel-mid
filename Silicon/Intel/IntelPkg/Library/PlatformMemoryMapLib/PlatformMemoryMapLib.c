/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PlatformMemoryMapLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/PeiServicesLib.h>

//
// I/O port addressability advertised in the CPU HOB. Every SoC in this tree
// decodes the full 16-bit port space.
#define PLATFORM_IO_SPACE_BITS  16

#define PLATFORM_SYSTEM_MEMORY_ATTRIBUTES  (                  \
  EFI_RESOURCE_ATTRIBUTE_PRESENT                      | \
  EFI_RESOURCE_ATTRIBUTE_INITIALIZED                  | \
  EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE                  | \
  EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE            | \
  EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE      | \
  EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE         | \
  EFI_RESOURCE_ATTRIBUTE_TESTED                         \
  )

#define PLATFORM_MMIO_ATTRIBUTES  (                           \
  EFI_RESOURCE_ATTRIBUTE_PRESENT                      | \
  EFI_RESOURCE_ATTRIBUTE_INITIALIZED                  | \
  EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE                  | \
  EFI_RESOURCE_ATTRIBUTE_TESTED                         \
  )

STATIC
BOOLEAN
PlatformRangeOverlaps (
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
PlatformRangeIsInside (
  IN UINT64  InnerBase,
  IN UINT64  InnerSize,
  IN UINT64  OuterBase,
  IN UINT64  OuterSize
  )
{
  return (InnerBase >= OuterBase) &&
         (InnerBase + InnerSize <= OuterBase + OuterSize);
}

/**
  Map a region kind onto its EDK2 resource type and attributes.
**/
STATIC
VOID
PlatformKindToResource (
  IN  PLATFORM_MEMORY_KIND   Kind,
  OUT EFI_RESOURCE_TYPE      *ResourceType,
  OUT UINT64                 *Attributes
  )
{
  switch (Kind) {
  case PlatformMemorySystemMemory:
    *ResourceType = EFI_RESOURCE_SYSTEM_MEMORY;
    *Attributes   = PLATFORM_SYSTEM_MEMORY_ATTRIBUTES;
    return;

  case PlatformMemoryMappedIo:
    *ResourceType = EFI_RESOURCE_MEMORY_MAPPED_IO;
    *Attributes   = PLATFORM_MMIO_ATTRIBUTES;
    return;

  case PlatformMemoryIo:
    *ResourceType = EFI_RESOURCE_IO;
    *Attributes   = PLATFORM_MMIO_ATTRIBUTES;
    return;

  default:
    *ResourceType = EFI_RESOURCE_MEMORY_RESERVED;
    *Attributes   = PLATFORM_MMIO_ATTRIBUTES;
    return;
  }
}

/**
  Round a window to page boundaries, rejecting one that cannot be described.

  Discovery tables are not required to be page aligned, and a window that wraps
  the address space would make BuildResourceDescriptorHob operate on garbage.

  @retval TRUE   Base and Size describe a usable window.
  @retval FALSE  The window is empty, wraps, or vanishes at page alignment.
**/
STATIC
BOOLEAN
PlatformNormalizeRegion (
  IN  CONST PLATFORM_MEMORY_REGION  *Region,
  OUT UINT64                        *Base,
  OUT UINT64                        *Size
  )
{
  UINT64  End;
  UINT64  Limit;

  if (Region->Size == 0) {
    return FALSE;
  }

  if (Region->Base > (MAX_UINT64 - Region->Size)) {
    DEBUG ((
      DEBUG_ERROR,
      "PlatformMemoryMapLib: region %a at %lx wraps the address space\n",
      Region->Name,
      Region->Base
      ));
    return FALSE;
  }

  // Discovery tables are not page aligned. Round base up and end down, then drop
  // what the two roundings meet in the middle on, which would otherwise yield a
  // zero-length descriptor.
  End    = Region->Base + Region->Size;
  *Base  = ALIGN_VALUE (Region->Base, EFI_PAGE_SIZE);
  Limit  = End & ~(UINT64)(EFI_PAGE_SIZE - 1);

  if (*Base >= Limit) {
    DEBUG ((
      DEBUG_ERROR,
      "PlatformMemoryMapLib: region %a (%lx-%lx) vanishes at page alignment\n",
      Region->Name,
      Region->Base,
      End - 1
      ));
    return FALSE;
  }

  *Size = Limit - *Base;

  return TRUE;
}

/**
  Report one window as an EFI_RESOURCE_* descriptor.
**/
STATIC
VOID
PlatformReportWindow (
  IN PLATFORM_MEMORY_KIND  Kind,
  IN UINT64                Base,
  IN UINT64                Size,
  IN CONST CHAR8           *Name
  )
{
  EFI_RESOURCE_TYPE  ResourceType;
  UINT64             Attributes;

  DEBUG ((
    DEBUG_VERBOSE,
    "PlatformMemoryMapLib: %a %lx-%lx\n",
    Name,
    Base,
    (Base + Size) - 1
    ));

  PlatformKindToResource (Kind, &ResourceType, &Attributes);

  //
  // BuildResourceDescriptorHob is VOID; it asserts when the HOB pool cannot
  // take another descriptor, which is the failure worth stopping for.
  //
  BuildResourceDescriptorHob (ResourceType, Attributes, Base, Size);
}

/**
  Install permanent PEI memory inside the discovered window holding the FD.

  PcdPeiMemoryBase is honoured when it falls inside that window. Otherwise the
  window is consumed from the top down, which is what a board whose PCD was
  written for a different DRAM configuration needs.
**/
STATIC
EFI_STATUS
PlatformInstallPeiMemory (
  IN  CONST PLATFORM_MEMORY_REGION  *Discovered,
  IN  UINTN                         DiscoveredCount,
  OUT UINT64                        *MainBase,
  OUT UINT64                        *MainLimit
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  UINT64      Base;
  UINT64      Size;
  UINT64      FdBase;
  UINT64      PeiMemBase;
  UINT64      PeiMemSize;

  FdBase     = (UINT64)FixedPcdGet32 (PcdFdBaseAddress) &
                ~(UINT64)EFI_PAGE_MASK;
  PeiMemBase = (UINT64)FixedPcdGet32 (PcdPeiMemoryBase);

  *MainBase  = 0;
  *MainLimit = 0;

  for (Index = 0; Index < DiscoveredCount; Index++) {
    if (Discovered[Index].Kind != PlatformMemorySystemMemory) {
      continue;
    }
    if (!PlatformNormalizeRegion (&Discovered[Index], &Base, &Size)) {
      continue;
    }

    if ((FdBase >= Base) && (FdBase < (Base + Size))) {
      *MainBase  = Base;
      *MainLimit = Base + Size;
      break;
    }
  }

  if (*MainLimit == 0) {
    DEBUG ((
      DEBUG_ERROR,
      "PlatformMemoryMapLib: no discovered system memory window covers the FD at %lx, "
      "memory map unusable\n",
      FdBase
      ));
    ASSERT (FALSE);
    return EFI_NOT_FOUND;
  }

  if ((PeiMemBase < *MainBase) || (PeiMemBase >= *MainLimit)) {
    DEBUG ((
      DEBUG_ERROR,
      "PlatformMemoryMapLib: PcdPeiMemoryBase 0x%lx outside main window, placing PEI memory dynamically\n",
      PeiMemBase
      ));
    PeiMemBase = (*MainLimit - SIZE_64MB) & ~(UINT64)(EFI_PAGE_SIZE - 1);
  }

  PeiMemSize = *MainLimit - PeiMemBase;

  DEBUG ((
    DEBUG_INIT,
    "PlatformMemoryMapLib: main window %lx-%lx, permanent PEI memory %lx-%lx\n",
    *MainBase,
    *MainLimit - 1,
    PeiMemBase,
    (PeiMemBase + PeiMemSize) - 1
    ));

  Status = PeiServicesInstallPeiMemory (PeiMemBase, PeiMemSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "PlatformMemoryMapLib: installing PEI memory %lx-%lx failed: %r\n",
      PeiMemBase,
      (PeiMemBase + PeiMemSize) - 1,
      Status
      ));
  }

  return Status;
}

/**
  Report device windows the discovery pass cannot know about.

  Anything overlapping an already-reported window is dropped: the bootloader
  already described that address range, and a second descriptor for it would
  leave the OS with an overlapping memory map.
**/
STATIC
VOID
PlatformReportDeviceWindows (
  IN CONST PLATFORM_MEMORY_REGION  *Discovered,
  IN UINTN                         DiscoveredCount,
  IN CONST PLATFORM_MEMORY_REGION  *DeviceMmio,
  IN UINTN                         DeviceMmioCount
  )
{
  UINTN       Index;
  UINTN       Other;
  UINT64      Base;
  UINT64      Size;
  UINT64      WindowBase;
  UINT64      WindowSize;
  BOOLEAN     Found;

  if (DeviceMmio == NULL) {
    return;
  }

  for (Index = 0; Index < DeviceMmioCount; Index++) {
    if (!PlatformNormalizeRegion (&DeviceMmio[Index], &WindowBase, &WindowSize)) {
      continue;
    }

    Found = FALSE;
    for (Other = 0; !Found && (Other < DiscoveredCount); Other++) {
      if (!PlatformNormalizeRegion (&Discovered[Other], &Base, &Size)) {
        continue;
      }

      if (PlatformRangeOverlaps (WindowBase, WindowSize, Base, Size)) {
        Found = TRUE;
      }
    }

    if (Found) {
      DEBUG ((
        DEBUG_ERROR,
        "PlatformMemoryMapLib: device window %a (%lx-%lx) overlaps a discovered window, skipped\n",
        DeviceMmio[Index].Name,
        WindowBase,
        (WindowBase + WindowSize) - 1
        ));
      continue;
    }

    PlatformReportWindow (
      DeviceMmio[Index].Kind,
      WindowBase,
      WindowSize,
      DeviceMmio[Index].Name
      );
  }
}

/**
  Check every carve-out sits inside reported system memory and that no two of
  them overlap, then reserve them all.
**/
STATIC
VOID
PlatformReserveCarveOuts (
  IN CONST PLATFORM_MEMORY_REGION    *Discovered,
  IN UINTN                           DiscoveredCount,
  IN CONST PLATFORM_MEMORY_CARVE_OUT  *CarveOuts,
  IN UINTN                           CarveOutCount
  )
{
  UINTN       Index;
  UINTN       Other;
  UINTN       RamCount;
  UINT64      Base;
  UINT64      Size;
  UINT64      WindowBase;
  UINT64      WindowSize;
  BOOLEAN     Found;
  UINT64      RamBase[PLATFORM_MAX_MEMORY_REGIONS];
  UINT64      RamSize[PLATFORM_MAX_MEMORY_REGIONS];

  if (CarveOuts == NULL) {
    return;
  }

  RamCount = 0;
  for (Index = 0; Index < DiscoveredCount; Index++) {
    if (Discovered[Index].Kind != PlatformMemorySystemMemory) {
      continue;
    }
    if (!PlatformNormalizeRegion (&Discovered[Index], &Base, &Size)) {
      continue;
    }
    if (RamCount >= ARRAY_SIZE (RamBase)) {
      break;
    }

    RamBase[RamCount] = Base;
    RamSize[RamCount] = Size;
    RamCount++;
  }

  for (Index = 0; Index < CarveOutCount; Index++) {
    if (CarveOuts[Index].Size == 0) {
      continue;
    }

    WindowBase = ALIGN_VALUE (CarveOuts[Index].Base, EFI_PAGE_SIZE);
    WindowSize = CarveOuts[Index].Size & ~(UINT64)(EFI_PAGE_SIZE - 1);
    if (WindowSize == 0) {
      continue;
    }

    Found = FALSE;
    for (Other = 0; Other < RamCount; Other++) {
      if (PlatformRangeIsInside (
            WindowBase,
            WindowSize,
            RamBase[Other],
            RamSize[Other]
            ))
      {
        Found = TRUE;
        break;
      }
    }

    if (!Found) {
      DEBUG ((
        DEBUG_ERROR,
        "PlatformMemoryMapLib: carve-out %a (%lx-%lx) lies outside every RAM window\n",
        CarveOuts[Index].Name,
        CarveOuts[Index].Base,
        (CarveOuts[Index].Base + CarveOuts[Index].Size) - 1
        ));
    }
  }

  for (Index = 0; Index < CarveOutCount; Index++) {
    for (Other = Index + 1; Other < CarveOutCount; Other++) {
      if ((CarveOuts[Index].Size == 0) || (CarveOuts[Other].Size == 0)) {
        continue;
      }

      if (PlatformRangeOverlaps (
            CarveOuts[Index].Base,
            CarveOuts[Index].Size,
            CarveOuts[Other].Base,
            CarveOuts[Other].Size
            ))
      {
        DEBUG ((
          DEBUG_ERROR,
          "PlatformMemoryMapLib: carve-outs %a and %a overlap\n",
          CarveOuts[Index].Name,
          CarveOuts[Other].Name
          ));
      }
    }
  }

  //
  // Reserve last: every window has been checked by now, so nothing can claim
  // firmware memory after this point.
  //
  for (Index = 0; Index < CarveOutCount; Index++) {
    if (CarveOuts[Index].Size == 0) {
      continue;
    }

    DEBUG ((
      DEBUG_VERBOSE,
      "PlatformMemoryMapLib: reserve %a %lx-%lx\n",
      CarveOuts[Index].Name,
      CarveOuts[Index].Base,
      (CarveOuts[Index].Base + CarveOuts[Index].Size) - 1
      ));

    BuildMemoryAllocationHob (
      CarveOuts[Index].Base,
      CarveOuts[Index].Size,
      CarveOuts[Index].MemoryType
      );
  }
}

EFI_STATUS
EFIAPI
BuildPlatformMemoryMap (
  IN CONST PLATFORM_MEMORY_REGION  *Discovered,
  IN UINTN                         DiscoveredCount,
  IN CONST PLATFORM_MEMORY_REGION  *DeviceMmio,
  IN UINTN                         DeviceMmioCount,
  IN CONST PLATFORM_MEMORY_CARVE_OUT  *CarveOuts,
  IN UINTN                         CarveOutCount
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  UINT64      Base;
  UINT64      Size;
  UINT64      MainBase;
  UINT64      MainLimit;

  if ((Discovered == NULL) || (DiscoveredCount == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((
    DEBUG_INIT,
    "PlatformMemoryMapLib: building map from %d discovered, %d device, %d carve-out windows\n",
    (UINT32)DiscoveredCount,
    (UINT32)DeviceMmioCount,
    (UINT32)CarveOutCount
    ));

  for (Index = 0; Index < DiscoveredCount; Index++) {
    if (!PlatformNormalizeRegion (&Discovered[Index], &Base, &Size)) {
      continue;
    }

    PlatformReportWindow (Discovered[Index].Kind, Base, Size, Discovered[Index].Name);
  }

  Status = PlatformInstallPeiMemory (
             Discovered,
             DiscoveredCount,
             &MainBase,
             &MainLimit
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  PlatformReportDeviceWindows (
    Discovered,
    DiscoveredCount,
    DeviceMmio,
    DeviceMmioCount
    );

  PlatformReserveCarveOuts (
    Discovered,
    DiscoveredCount,
    CarveOuts,
    CarveOutCount
    );

  BuildCpuHob (
    FixedPcdGet8 (PcdCpuMaxPhysAddrBits),
    PLATFORM_IO_SPACE_BITS
    );

  return EFI_SUCCESS;
}

/**
  Append one device window, dropping it if the caller's buffer is full.
**/
STATIC
VOID
PlatformAppendDeviceMmio (
  IN OUT PLATFORM_MEMORY_REGION  *DeviceMmio,
  IN     UINTN                   Capacity,
  IN OUT UINTN                   *Count,
  IN     CONST CHAR8             *Name,
  IN     UINT64                  Base,
  IN     UINT64                  Size
  )
{
  if (*Count >= Capacity) {
    DEBUG ((DEBUG_ERROR, "PlatformMemoryMapLib: device MMIO buffer full, dropping %a\n", Name));
    return;
  }

  DeviceMmio[*Count].Base = Base;
  DeviceMmio[*Count].Size = Size;
  DeviceMmio[*Count].Kind = PlatformMemoryMappedIo;
  DeviceMmio[*Count].Name = Name;
  (*Count)++;
}

UINTN
EFIAPI
CollectPlatformDeviceMmio (
  OUT PLATFORM_MEMORY_REGION  *DeviceMmio,
  IN  UINTN                   Capacity
  )
{
  UINTN  Count;

  Count = 0;

  if (FixedPcdGet64 (PcdPciGpuMmioSize) != 0) {
    PlatformAppendDeviceMmio (
      DeviceMmio,
      Capacity,
      &Count,
      "GPU 00:02.0",
      FixedPcdGet64 (PcdPciGpuMmioBase),
      FixedPcdGet64 (PcdPciGpuMmioSize)
      );
  }

  if (FixedPcdGet64 (PcdPciIspMmioSize) != 0) {
    PlatformAppendDeviceMmio (
      DeviceMmio,
      Capacity,
      &Count,
      "ISP 00:03.0",
      FixedPcdGet64 (PcdPciIspMmioBase),
      FixedPcdGet64 (PcdPciIspMmioSize)
      );
  }

  if (FixedPcdGet64 (PcdPciEmacMmioSize) != 0) {
    PlatformAppendDeviceMmio (
      DeviceMmio,
      Capacity,
      &Count,
      "EMAC 00:06.0",
      FixedPcdGet64 (PcdPciEmacMmioBase),
      FixedPcdGet64 (PcdPciEmacMmioSize)
      );
  }

  return Count;
}

/**
  Append one carve-out, dropping it if the caller's buffer is full.

  Dropping one costs the OS a reserved range but does not corrupt the map, which
  is why this reports rather than stops.
**/
STATIC
VOID
PlatformAppendCarveOut (
  IN OUT PLATFORM_MEMORY_CARVE_OUT  *CarveOuts,
  IN     UINTN                      Capacity,
  IN OUT UINTN                      *Count,
  IN     CONST CHAR8                *Name,
  IN     UINT64                     Base,
  IN     UINT64                     Size,
  IN     EFI_MEMORY_TYPE            MemoryType
  )
{
  if (*Count >= Capacity) {
    DEBUG ((DEBUG_ERROR, "PlatformMemoryMapLib: carve-out buffer full, dropping %a\n", Name));
    return;
  }

  CarveOuts[*Count].Name       = Name;
  CarveOuts[*Count].Base       = Base;
  CarveOuts[*Count].Size       = Size;
  CarveOuts[*Count].MemoryType = MemoryType;
  (*Count)++;
}

UINTN
EFIAPI
CollectPlatformCarveOuts (
  OUT PLATFORM_MEMORY_CARVE_OUT  *CarveOuts,
  IN  UINTN                      Capacity
  )
{
  UINTN   Count;
  UINT64  FdBase;
  UINT64  TempRamBase;

  FdBase      = (UINT64)FixedPcdGet32 (PcdFdBaseAddress) &
                ~(UINT64)EFI_PAGE_MASK;
  TempRamBase = (UINT64)FixedPcdGet32 (PcdSecPeiTemporaryRamBase) &
                ~(UINT64)EFI_PAGE_MASK;

  Count = 0;

  PlatformAppendCarveOut (
    CarveOuts,
    Capacity,
    &Count,
    "legacy reserved",
    FixedPcdGet32 (PcdLegacyReservedBase),
    FixedPcdGet32 (PcdLegacyReservedSize),
    (EFI_MEMORY_TYPE)FixedPcdGet32 (PcdLegacyReservedType)
    );

  PlatformAppendCarveOut (
    CarveOuts,
    Capacity,
    &Count,
    "legacy top",
    FixedPcdGet32 (PcdLegacyTopBase),
    FixedPcdGet32 (PcdLegacyTopSize),
    EfiReservedMemoryType
    );

  // EXPERIMENT: hand the FD back to the OS rather than holding it reserved. The
  // bootloader loads the whole FD into DRAM and transfers control there, so this
  // is ordinary RAM, not a flash mapping, and nothing in it is live once Boot
  // Services start: the images are shadowed into RAM at load time, the FV is only
  // their source, and AuthVariableLibNull leaves no NVRAM. BDS does read the FV to
  // register the Shell boot option by FvFile GUID, but that is long finished by
  // ExitBootServices.
  //
  // Revert to EfiReservedMemoryType if anything misbehaves. A late FV read fails
  // as a hang under memory pressure rather than cleanly at boot.
  PlatformAppendCarveOut (
    CarveOuts,
    Capacity,
    &Count,
    "firmware image",
    FdBase,
    ALIGN_VALUE (
      (UINT64)FixedPcdGet32 (PcdFdBaseAddress) +
      (UINT64)FixedPcdGet32 (PcdFdSize) - FdBase,
      EFI_PAGE_SIZE
      ),
    EfiBootServicesData
    );

  PlatformAppendCarveOut (
    CarveOuts,
    Capacity,
    &Count,
    "SEC/PEI temp RAM",
    TempRamBase,
    ALIGN_VALUE (
      (UINT64)FixedPcdGet32 (PcdSecPeiTemporaryRamBase) +
      (UINT64)FixedPcdGet32 (PcdSecPeiTemporaryRamSize) - TempRamBase,
      EFI_PAGE_SIZE
      ),
    EfiReservedMemoryType
    );

  PlatformAppendCarveOut (
    CarveOuts,
    Capacity,
    &Count,
    "console state",
    FixedPcdGet32 (PcdConsoleStateBase),
    SIZE_4KB,
    EfiReservedMemoryType
    );

  return Count;
}