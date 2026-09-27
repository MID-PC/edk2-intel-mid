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
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>

#define SCU_IPC_COMMAND_OFFSET     0x00U
#define SCU_IPC_STATUS_OFFSET      0x04U
#define SCU_IPC_STATUS_BUSY        BIT0
#define SCU_IPC_STATUS_ERROR       BIT1
#define SCU_IPC_WARM_RESET         0xF0U
#define SCU_IPC_COLD_RESET         0xF1U
#define SCU_IPC_POLL_LIMIT         3000000U
#define SCU_IPC_MMIO_SIZE          SIZE_4KB

// South-complex PMU (0xff11d000)
// Mirrors Linux pmu_power_off()
#define PMU_PM_STS_OFFSET          0x00U
#define PMU_PM_CMD_OFFSET          0x04U
#define PMU_PM_STS_BUSY            BIT8
#define PMU_S5_VALUE               0x309D2601U
#define PMU_BUSY_POLL_LIMIT        1000000U
#define PMU_MMIO_SIZE              SIZE_4KB

STATIC VOID       *mScuIpcBase;
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
  EfiConvertPointer (0, &mScuIpcBase);
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

  Base        = (EFI_PHYSICAL_ADDRESS)FixedPcdGet32 (PcdScuIpcBase);
  mScuIpcBase = (VOID *)(UINTN)Base;

  Status = gDS->SetMemorySpaceAttributes (
                  Base,
                  SCU_IPC_MMIO_SIZE,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "SCU reset: failed to mark IPC MMIO runtime: %r\n", Status));
  }

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

STATIC
BOOLEAN
ScuIpcWaitIdle (
  VOID
  );

#pragma GCC diagnostic ignored "-Wunused-function"

STATIC
BOOLEAN
ScuIpcWaitIdle (
  VOID
  )
{
  UINT32  Retry;
  UINT32  Status;
  UINTN   Base;

  Base = (UINTN)mScuIpcBase;
  for (Retry = 0; Retry < SCU_IPC_POLL_LIMIT; Retry++) {
    Status = MmioRead32 (Base + SCU_IPC_STATUS_OFFSET);
    if ((Status & SCU_IPC_STATUS_BUSY) == 0) {
      return (BOOLEAN)((Status & SCU_IPC_STATUS_ERROR) == 0);
    }

    CpuPause ();
  }

  return FALSE;
}

STATIC
VOID
ScuReset (
  IN UINT8  Command
  )
{
  UINTN  Base;

  Base = (UINTN)mScuIpcBase;
  DEBUG ((DEBUG_ERROR, "SCU reset: issuing command 0x%02x\n", Command));
  if ((Base != 0) && (MmioRead32 (Base + SCU_IPC_STATUS_OFFSET) != MAX_UINT32)) {
    if (ScuIpcWaitIdle ()) {
      MmioWrite32 (Base + SCU_IPC_COMMAND_OFFSET, Command);
      ScuIpcWaitIdle ();
    }
  }

  CpuDeadLoop ();
}

VOID
EFIAPI
ResetCold (
  VOID
  )
{
  ScuReset (SCU_IPC_COLD_RESET);
}

VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  ScuReset (SCU_IPC_WARM_RESET);
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

  ScuReset (SCU_IPC_COLD_RESET);
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
