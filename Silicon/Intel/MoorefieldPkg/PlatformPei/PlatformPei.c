/** @file
  Moorefield / Silvermont platform PEIM.

  Discovers the memory map from the SFI tables droidboot publishes, then hands
  it to PlatformMemoryMapLib, which owns descriptors, carve-outs and the CPU HOB.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>
#include <Library/PeimEntryPoint.h>
#include <Library/PeiServicesLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/LocalApicLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformMemoryMapLib.h>
#include <Library/ScuIpcLib.h>
#include <Library/SfiMemoryMapLib.h>
#include <Ppi/MasterBootMode.h>
#include <Ppi/MemoryDiscovered.h>

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

/**
  Stop the SCU kernel watchdog. Runs after the APIC timer is started, which
  ScuIpcLib's polling needs.
**/
STATIC
VOID
DisableScuWatchdog (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = ScuIpcSimpleCommand (
             SCU_IPC_MSG_WATCHDOG_TIMER,
             SCU_IPC_WDT_SUB_STOP
             );
  switch (Status) {
  case EFI_SUCCESS:
    DEBUG ((DEBUG_INFO, "PlatformPei: SCU watchdog disabled\n"));
    break;

  case EFI_NOT_FOUND:
    DEBUG ((DEBUG_WARN, "PlatformPei: SCU IPC not present, skipping watchdog disable\n"));
    break;

  default:
    DEBUG ((DEBUG_ERROR, "PlatformPei: SCU watchdog disable failed: %r\n", Status));
    break;
  }
}

/**
  SfiMemoryMapLib discovery, then PlatformMemoryMapLib construction.

  The one place this PEIM knows its bootloader speaks SFI, which is the only thing
  neither library can know. A SoC that discovers memory otherwise replaces this.
**/
STATIC
VOID
PlatformPeiInstallMemoryMap (
  VOID
  )
{
  EFI_STATUS                 Status;
  UINTN                      SfiCount;
  UINTN                      DiscoveredCount;
  UINTN                      DeviceCount;
  UINTN                      CarveCount;
  PLATFORM_MEMORY_REGION     Discovered[PLATFORM_MAX_MEMORY_REGIONS];
  PLATFORM_MEMORY_REGION     DeviceMmio[PLATFORM_MAX_DEVICE_MMIO];
  PLATFORM_MEMORY_CARVE_OUT  CarveOuts[PLATFORM_MAX_CARVE_OUTS];

  // Room for the legacy window added below.
  SfiDiscoverMemoryRegions (
    Discovered,
    ARRAY_SIZE (Discovered) - 1,
    &SfiCount
    );

  // Real-mode low memory for the OS AP startup trampoline. Added to the
  // discovered list so it takes part in carve-out validation like any other
  // system memory window.
  Discovered[SfiCount].Base = FixedPcdGet32 (PcdLegacyMemoryBase);
  Discovered[SfiCount].Size = FixedPcdGet32 (PcdLegacyMemorySize);
  Discovered[SfiCount].Kind = PlatformMemorySystemMemory;
  Discovered[SfiCount].Name = "legacy real-mode";
  DiscoveredCount = SfiCount + 1;

  // Device windows need their own buffer; folding them into Discovered would let
  // the overlap check compare a window against itself.
  DeviceCount = CollectPlatformDeviceMmio (DeviceMmio, ARRAY_SIZE (DeviceMmio));
  CarveCount  = CollectPlatformCarveOuts (CarveOuts, ARRAY_SIZE (CarveOuts));

  Status = BuildPlatformMemoryMap (
             Discovered,
             DiscoveredCount,
             DeviceMmio,
             DeviceCount,
             CarveOuts,
             CarveCount
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PlatformPei: memory map construction failed: %r\n", Status));
    ASSERT (FALSE);
    CpuDeadLoop ();
  }
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

  DEBUG ((DEBUG_INFO, "PlatformPei: Moorefield PEIM entry\n"));

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