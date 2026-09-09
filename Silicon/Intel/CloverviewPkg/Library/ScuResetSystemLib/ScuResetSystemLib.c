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

#define SCU_IPC_COMMAND_OFFSET     0x00U
#define SCU_IPC_STATUS_OFFSET      0x04U
#define SCU_IPC_STATUS_BUSY        BIT0
#define SCU_IPC_STATUS_ERROR       BIT1
#define SCU_IPC_WARM_RESET         0xF0U
#define SCU_IPC_COLD_RESET         0xF1U
#define SCU_IPC_POLL_LIMIT         3000000U
#define SCU_IPC_MMIO_SIZE          SIZE_4KB

//
// Cloverview south-complex PMU ("intel_pmu_driver", 0xff11d000-0xff11dfff).
// Mirror the Linux pmu_power_off(): wait for pm_sts bit 8 (pmu_busy) to clear,
// then write S5_VALUE to pm_cmd. Registers per arch/x86/.../intel_soc_pmu.h:
//   struct mrst_pmu_reg { u32 pm_sts; u32 pm_cmd; u32 pm_ics; ... } // 0x00,0x04,0x08
//
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
  DEBUG ((DEBUG_ERROR, "SCU reset: issuing command 0x%02x\n", Command));
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
  //
  // Windows "Restart" arrives here as EfiResetCold. The SCU cold reset (0xf1)
  // is the only way this SoC resets itself; a raw SCU reset at runtime leaves
  // the OSIP first-stage loader powering off on the re-boot, so seamless
  // restart does not come back up on this platform. Kept as the designed SCU
  // cold reset (matches the Linux restart command 0xf1) per current scope.
  //
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

  //
  // Mirror the Linux pmu_power_off (intel_soc_pmu.c): wait for the PMU to
  // become idle, then write the S5 command to pm_cmd. Returns TRUE if the
  // S5 write was attempted, FALSE if the PMU was unreachable or busy.
  //
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
  //
  // Real PMU S5 power-off. Windows "Shutdown" (Fast Startup enabled) writes
  // the hibernation state and then issues gRT->ResetSystem(EfiResetShutdown);
  // previously this mapped to ResetCold(), so the machine rebooted instead of
  // powering off and the next boot "restored from the previous location".
  // The true S5 path powers the SoC down asynchronously after the write.
  //
  // If PmuPowerOff writes S5 successfully the platform will power off on its
  // own; we must NOT follow up with a SCU cold reset — doing so races the
  // power-off and causes a reboot instead (the exact symptom observed before
  // this fix).  Deadloop here to let the PMU finish.
  //
  if (PmuPowerOff ()) {
    DEBUG ((DEBUG_ERROR, "ResetShutdown: S5 written (0x309d2601), waiting for rails to drop\n"));
    CpuDeadLoop ();
  }

  //
  // PMU never became ready (busy never cleared, or register space unmapped).
  // Fall back to the SCU IPC cold reset as a last resort so the ResetSystem
  // call still terminates the OS session instead of hanging forever.
  //
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
