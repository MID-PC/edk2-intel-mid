#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/ResetSystemLib.h>
// ScuIpcLib is deliberately not linked here, as that breaks reboot
// The header is only used for message IDs
#include <Library/ScuIpcLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>

//
// PMU Registers
//
#define PMU_PM_STS_OFFSET          0x00U
#define PMU_PM_CMD_OFFSET          0x04U
#define PMU_PM_STS_BUSY            BIT8
#define PMU_S5_VALUE               0x309D2601U
#define PMU_BUSY_POLL_LIMIT        1000000U
#define PMU_MMIO_SIZE              SIZE_4KB

//
// SCU IPC Registers
//
#define SCU_IPC_CMD_OFFSET         0x00U
#define SCU_IPC_STS_OFFSET         0x04U
#define SCU_IPC_STS_BUSY           BIT0
#define SCU_IPC_POLL_LIMIT         10000000U
#define SCU_IPC_MMIO_SIZE          SIZE_4KB

//
// Global Variables
//
STATIC VOID       *mPmuBase;
STATIC VOID       *mScuIpcBase;
STATIC EFI_EVENT  mVirtualAddressChangeEvent;

STATIC
VOID
EFIAPI
ScuResetVirtualAddressChange (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  // Convert MMIO Base Addresses
  EfiConvertPointer (0, &mPmuBase);
  EfiConvertPointer (0, &mScuIpcBase);
}

EFI_STATUS
EFIAPI
ScuResetSystemLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_STATUS            ScuStatus;
  EFI_PHYSICAL_ADDRESS  PmuBase;
  EFI_PHYSICAL_ADDRESS  ScuIpcBase;

  // Get MMIO Base Addresses
  PmuBase     = (EFI_PHYSICAL_ADDRESS)FixedPcdGet32 (PcdPmuBase);
  ScuIpcBase  = (EFI_PHYSICAL_ADDRESS)FixedPcdGet32 (PcdScuIpcBase);
  mPmuBase    = (VOID *)(UINTN)PmuBase;
  mScuIpcBase = (VOID *)(UINTN)ScuIpcBase;

  // Mark PMU MMIO Runtime
  Status = gDS->SetMemorySpaceAttributes (
                  PmuBase,
                  PMU_MMIO_SIZE,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "SCU reset: failed to mark PMU MMIO runtime: %r\n", Status));
  }

  // Mark SCU IPC MMIO Runtime
  ScuStatus = gDS->SetMemorySpaceAttributes (
                       ScuIpcBase,
                       SCU_IPC_MMIO_SIZE,
                       EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                       );
  if (EFI_ERROR (ScuStatus)) {
    DEBUG ((DEBUG_WARN, "SCU reset: failed to mark SCU IPC MMIO runtime: %r\n", ScuStatus));
  }

  // Register Virtual Address Change Event
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
VOID
ScuIpcWaitIdle (
  IN UINTN  Ipc
  )
{
  UINT32  Retry;

  // Wait for SCU Idle
  for (Retry = 0; Retry < SCU_IPC_POLL_LIMIT; Retry++) {
    // Check Busy Bit
    if ((MmioRead32 (Ipc + SCU_IPC_STS_OFFSET) & SCU_IPC_STS_BUSY) == 0) {
      return;
    }

    CpuPause ();
  }
}

STATIC
VOID
ScuReset (
  IN UINT32  Command
  )
{
  UINTN  Ipc;

#ifdef SC_RESET_CUT_SD_POWER
  // Stop Moorefield microSD clock and bus power (SDHCI controller at 0xFF3FA000)
  MmioWrite16 (0xFF3FA000U + 0x2C, 0);
  MmioWrite8  (0xFF3FA000U + 0x29, 0);
#endif

  DEBUG ((DEBUG_ERROR, "SCU reset: issuing command 0x%02x\n", Command));

  // Write Directly, ScuIpcLib Does Not Fit the Runtime Stack
  Ipc = (UINTN)mScuIpcBase;
  if (Ipc != 0) {
#ifdef SC_RESET_CUT_SD_POWER
    // Cut PMIC VLDO rail (SD card VDD: register 0xAF, BIT1) via SCU IPC
    ScuIpcWaitIdle (Ipc);
    MmioWrite32 (Ipc + 0x80, 0x000000AFU | (0x02U << 24));
    MmioWrite32 (Ipc + SCU_IPC_CMD_OFFSET, (4 << 16) | (2 << 12) | 0xE8);
    ScuIpcWaitIdle (Ipc);

    // Settle delay for card capacitors to discharge
    for (volatile UINTN Index = 0; Index < 500000; Index++) {
      CpuPause ();
    }
#endif

    // Wait for Idle Interface
    ScuIpcWaitIdle (Ipc);

    // Send Reset Command
    MmioWrite32 (Ipc + SCU_IPC_CMD_OFFSET, Command & 0xFF);

    // Wait for Reset
    ScuIpcWaitIdle (Ipc);
  }

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

  // Get PMU Base
  Base = (UINTN)mPmuBase;

  // Verify PMU Present
  if ((Base == 0) ||
      (MmioRead32 (Base + PMU_PM_STS_OFFSET) == MAX_UINT32)) {
    return FALSE;
  }

  // Wait for PMU Idle
  for (Retry = 0; Retry < PMU_BUSY_POLL_LIMIT; Retry++) {
    Status = MmioRead32 (Base + PMU_PM_STS_OFFSET);

    // Check Busy Bit
    if ((Status & PMU_PM_STS_BUSY) == 0) {
      // Write S5
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
  // Power Off PMU
  if (PmuPowerOff ()) {
    DEBUG ((DEBUG_ERROR, "ResetShutdown: S5 written (0x309d2601), waiting for rails to drop\n"));
    CpuDeadLoop ();
  }

  // Do Cold Reset
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
