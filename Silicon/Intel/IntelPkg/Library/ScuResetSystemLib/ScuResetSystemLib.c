/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/ResetSystemLib.h>
#include <Library/ScuIpcLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>

// South-complex PMU register block (PcdPmuBase: ff11d000 on Cloverview,
// ff00b000 on Moorefield)
// Mirrors Linux pmu_power_off()
#define PMU_PM_STS_OFFSET          0x00U
#define PMU_PM_CMD_OFFSET          0x04U
#define PMU_PM_STS_BUSY            BIT8
#define PMU_S5_VALUE               0x309D2601U
#define PMU_BUSY_POLL_LIMIT        1000000U
#define PMU_MMIO_SIZE              SIZE_4KB

STATIC VOID       *mPmuBase;
STATIC EFI_EVENT  mVirtualAddressChangeEvent;

STATIC
VOID
EFIAPI
ScuResetVirtualAddressChange (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EfiConvertPointer (0, &mPmuBase);
}

EFI_STATUS
EFIAPI
ScuResetSystemLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Base;

  // ScuIpcLib makes its own aperture uncacheable on first use. The PMU block does
  // not help itself, so map it here; SetMemorySpaceAttributes only records a GCD
  // attribute, and this is the only path we have across ExitBootServices.
  Base     = (EFI_PHYSICAL_ADDRESS)FixedPcdGet32 (PcdPmuBase);
  mPmuBase = (VOID *)(UINTN)Base;

  Status = gDS->SetMemorySpaceAttributes (
                  Base,
                  PMU_MMIO_SIZE,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "SCU reset: failed to mark PMU MMIO runtime: %r\n", Status));
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  ScuResetVirtualAddressChange,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mVirtualAddressChangeEvent
                  );
  return Status;
}

/**
  Ask the SCU to reset, then wait for it to happen.

  Whether the command was accepted makes no difference: if the SCU did not take it
  nothing else resets the board, so the alternatives are a hang either way.
**/
STATIC
VOID
ScuReset (
  IN UINT32  Command
  )
{
  DEBUG ((DEBUG_ERROR, "SCU reset: issuing command 0x%02x\n", Command));
  ScuIpcSimpleCommand (Command, 0);

  CpuDeadLoop ();
}

VOID
EFIAPI
ResetCold (
  VOID
  )
{
  ScuReset (SCU_IPC_MSG_COLD_RESET);
}

VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  ScuReset (SCU_IPC_MSG_WARM_RESET);
}

STATIC
BOOLEAN
EFIAPI
PmuPowerOff (
  VOID
  )
{
  UINTN   Base;
  UINT32  Status;
  UINT32  Retry;

  Base = (UINTN)mPmuBase;
  if ((Base == 0) ||
      (MmioRead32 (Base + PMU_PM_STS_OFFSET) == MAX_UINT32)) {
    return FALSE;
  }

  for (Retry = 0; Retry < PMU_BUSY_POLL_LIMIT; Retry++) {
    Status = MmioRead32 (Base + PMU_PM_STS_OFFSET);
    if ((Status & PMU_PM_STS_BUSY) == 0) {
      MmioWrite32 (Base + PMU_PM_CMD_OFFSET, PMU_S5_VALUE);
      return TRUE;
    }

    CpuPause ();
  }

  return FALSE;
}

VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  if (PmuPowerOff ()) {
    DEBUG ((DEBUG_ERROR, "ResetShutdown: S5 written (0x309d2601), waiting for rails to drop\n"));
    CpuDeadLoop ();
  }

  ScuReset (SCU_IPC_MSG_COLD_RESET);
}

VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID   *ResetData
  )
{
  ResetCold ();
}

VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  switch (ResetType) {
    case EfiResetWarm:
      ResetWarm ();
      break;
    case EfiResetShutdown:
      ResetShutdown ();
      break;
    case EfiResetPlatformSpecific:
      ResetPlatformSpecific (DataSize, ResetData);
      break;
    case EfiResetCold:
    default:
      ResetCold ();
      break;
  }
}
