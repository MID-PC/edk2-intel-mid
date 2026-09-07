/** @file
  ResetSystemLib using the Cloverview/Penwell SCU IPC-1 reset commands.

  The Android kernel uses intel_scu_ipc_raw_cmd(IPCMSG_COLD_RESET, 0, ...)
  for Cloverview restart.  A zero-length raw IPC command is encoded as the
  command byte plus the four-bit subcommand at bits 12..15.

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

#define SCU_IPC_COMMAND_OFFSET  0x00U
#define SCU_IPC_STATUS_OFFSET   0x04U
#define SCU_IPC_STATUS_BUSY     BIT0
#define SCU_IPC_STATUS_ERROR    BIT1
#define SCU_IPC_WARM_RESET      0xF0U
#define SCU_IPC_COLD_RESET      0xF1U
#define SCU_IPC_POLL_LIMIT      3000000U
#define SCU_IPC_MMIO_SIZE       SIZE_4KB

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

//
// Kept for when the suppressed reset block below is re-enabled.
//
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

  //
  // Real SCU IPC reset is enabled again: Windows shutdown/restart goes through
  // gRT->ResetSystem, and with the command suppressed the call simply dead
  // looped, so "nothing happened" on reboot/shutdown from the OS.
  //
  if ((Base != 0) && (MmioRead32 (Base + SCU_IPC_STATUS_OFFSET) != MAX_UINT32)) {
    if (ScuIpcWaitIdle ()) {
      MmioWrite32 (Base + SCU_IPC_COMMAND_OFFSET, Command);

      // A successful reset command normally never becomes idle because the
      // SCU resets the platform first. Poll only to serialize the MMIO write.
      ScuIpcWaitIdle ();
    }
  }

  // ResetSystem must never return. If the SCU rejected the request, remain
  // stopped rather than falling back into the caller or triggering an ASSERT.
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

VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  // The Cloverview kernel's power-off path is PMU-specific. Until that path is
  // implemented, use the known-good SCU cold reset instead of asserting.
  ResetCold ();
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
