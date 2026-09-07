/** @file
  Minimal Real Time Clock architectural protocol and SCU watchdog keeper for
  Clover Trail+ (Atom Z25xx).

  There is no usable RTC available to us after the primary bootloader hands
  over, so time is emulated: a fixed epoch is advanced by a software counter.
  This is enough for the DXE Core, the variable stack and UiApp/Shell to run.

  In addition, this driver keeps the SCU kernel watchdog quiet. SEC stops it
  once through IPC-1, but the SCU re-arms the kernel watchdog whenever it
  services later IPC traffic (PCNTRL for VCCSDIO, reset library probes, etc.),
  which is why the board reboots roughly 40 s after control is handed to an OS
  loader that never talks to the SCU itself. The watchdog stop is therefore
  re-issued periodically during boot services, at ReadyToBoot, and finally at
  ExitBootServices, which is the last point at which this firmware runs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Protocol/RealTimeClock.h>
#include <Guid/EventGroup.h>

//
// Cloverview SCU IPC-1 block. Same encoding as the known-good SEC path:
// command 0xF8 (IPCMSG_WATCHDOG_TIMER) with sub-command 1 (STOP) placed in
// bits 15:12, i.e. 0x000010F8, and no payload at all (never touch WBUF0).
//
#define SCU_IPC_CMD_OFFSET      0x00u
#define SCU_IPC_STATUS_OFFSET   0x04u
#define SCU_IPC_STATUS_BUSY     BIT0
#define SCU_IPC_STATUS_ERROR    BIT1
#define SCU_IPC_POLL_LIMIT      1000000u
#define SCU_IPC_WATCHDOG_STOP   0x000010F8u

//
// Re-issue the watchdog stop every 5 seconds while boot services are alive.
//
#define WDT_KEEPER_PERIOD_100NS  (5 * 10 * 1000 * 1000ULL)

STATIC EFI_EVENT  mWdtTimerEvent      = NULL;
STATIC EFI_EVENT  mReadyToBootEvent   = NULL;
STATIC EFI_EVENT  mExitBootServicesEvent = NULL;
STATIC UINT32     mWdtStopCount       = 0;
STATIC UINT32     mWdtFailCount       = 0;

STATIC EFI_TIME  mTime = {
  2024,   // Year
  1,      // Month
  1,      // Day
  0,      // Hour
  0,      // Minute
  0,      // Second
  0,      // Pad1
  0,      // Nanosecond
  EFI_UNSPECIFIED_TIMEZONE,
  0,      // Daylight
  0       // Pad2
};

STATIC UINT32  mTicks = 0;

/**
  Wait until the SCU IPC-1 block is idle.

  @retval TRUE   The IPC block is idle.
  @retval FALSE  The IPC block stayed busy (or is not mapped).
**/
STATIC
BOOLEAN
ScuIpcWaitNotBusy (
  VOID
  )
{
  UINTN   IpcBase;
  UINT32  Status;
  UINT32  Retry;

  IpcBase = (UINTN)FixedPcdGet32 (PcdScuIpcBase);

  for (Retry = 0; Retry < SCU_IPC_POLL_LIMIT; Retry++) {
    Status = MmioRead32 (IpcBase + SCU_IPC_STATUS_OFFSET);
    if (Status == MAX_UINT32) {
      return FALSE;
    }

    if ((Status & SCU_IPC_STATUS_BUSY) == 0) {
      return TRUE;
    }

    CpuPause ();
  }

  return FALSE;
}

/**
  Stop the SCU kernel watchdog through IPC-1.

  @param[in]  Verbose  TRUE to log the outcome, FALSE for the silent periodic
                       keeper (which would otherwise flood the framebuffer log).
**/
STATIC
VOID
ScuStopWatchdog (
  IN BOOLEAN  Verbose
  )
{
  UINTN   IpcBase;
  UINT32  Status;

  IpcBase = (UINTN)FixedPcdGet32 (PcdScuIpcBase);

  if (MmioRead32 (IpcBase + SCU_IPC_STATUS_OFFSET) == MAX_UINT32) {
    if (Verbose) {
      DEBUG ((DEBUG_WARN, "PlatformRuntime: SCU IPC not present, watchdog left as-is\n"));
    }

    return;
  }

  if (!ScuIpcWaitNotBusy ()) {
    mWdtFailCount++;
    if (Verbose) {
      DEBUG ((DEBUG_ERROR, "PlatformRuntime: SCU IPC busy before watchdog stop\n"));
    }

    return;
  }

  MmioWrite32 (IpcBase + SCU_IPC_CMD_OFFSET, SCU_IPC_WATCHDOG_STOP);

  if (!ScuIpcWaitNotBusy ()) {
    mWdtFailCount++;
    if (Verbose) {
      DEBUG ((DEBUG_ERROR, "PlatformRuntime: SCU IPC timeout during watchdog stop\n"));
    }

    return;
  }

  Status = MmioRead32 (IpcBase + SCU_IPC_STATUS_OFFSET);
  if ((Status & SCU_IPC_STATUS_ERROR) != 0) {
    mWdtFailCount++;
    if (Verbose) {
      DEBUG ((
        DEBUG_ERROR,
        "PlatformRuntime: SCU rejected watchdog stop (status 0x%08x)\n",
        Status
        ));
    }

    return;
  }

  mWdtStopCount++;
  if (Verbose) {
    DEBUG ((
      DEBUG_INFO,
      "PlatformRuntime: SCU watchdog stopped (status 0x%08x, stops %u, failures %u)\n",
      Status,
      mWdtStopCount,
      mWdtFailCount
      ));
  }
}

/**
  Periodic keeper: silently re-issue the watchdog stop while boot services run.
**/
STATIC
VOID
EFIAPI
WdtKeeperTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  ScuStopWatchdog (FALSE);
}

/**
  ReadyToBoot: log the state once so the on-screen log shows it.
**/
STATIC
VOID
EFIAPI
WdtReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  ScuStopWatchdog (TRUE);
}

/**
  ExitBootServices: last chance to silence the watchdog before the OS, which
  does not talk to the SCU at all, takes over.
**/
STATIC
VOID
EFIAPI
WdtExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  //
  // No DEBUG() here: the framebuffer console may not be safe to touch once the
  // OS loader has taken the display, and this callback runs at TPL_NOTIFY.
  //
  if (mWdtTimerEvent != NULL) {
    gBS->SetTimer (mWdtTimerEvent, TimerCancel, 0);
  }

  ScuStopWatchdog (FALSE);
  ScuStopWatchdog (FALSE);
}

/**
  Return the current (emulated) time.
**/
EFI_STATUS
EFIAPI
PlatformGetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities  OPTIONAL
  )
{
  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Advance one second per call so callers that wait on time make progress.
  //
  mTicks++;
  if ((mTicks % 4) == 0) {
    mTime.Second++;
    if (mTime.Second >= 60) {
      mTime.Second = 0;
      mTime.Minute++;
      if (mTime.Minute >= 60) {
        mTime.Minute = 0;
        mTime.Hour++;
        if (mTime.Hour >= 24) {
          mTime.Hour = 0;
          mTime.Day++;
        }
      }
    }
  }

  CopyMem (Time, &mTime, sizeof (EFI_TIME));

  if (Capabilities != NULL) {
    Capabilities->Resolution = 1;
    Capabilities->Accuracy   = 0;
    Capabilities->SetsToZero = FALSE;
  }

  return EFI_SUCCESS;
}

/**
  Set the current (emulated) time.
**/
EFI_STATUS
EFIAPI
PlatformSetTime (
  IN EFI_TIME  *Time
  )
{
  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (&mTime, Time, sizeof (EFI_TIME));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
PlatformGetWakeupTime (
  OUT BOOLEAN   *Enabled,
  OUT BOOLEAN   *Pending,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
PlatformSetWakeupTime (
  IN BOOLEAN   Enabled,
  IN EFI_TIME  *Time  OPTIONAL
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Install the Real Time Clock architectural protocol and arm the SCU watchdog
  keeper.
**/
EFI_STATUS
EFIAPI
PlatformRuntimeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  gRT->GetTime       = PlatformGetTime;
  gRT->SetTime       = PlatformSetTime;
  gRT->GetWakeupTime = PlatformGetWakeupTime;
  gRT->SetWakeupTime = PlatformSetWakeupTime;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiRealTimeClockArchProtocolGuid,
                  NULL,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "PlatformRuntimeDxe: emulated RTC installed\n"));

  //
  // Stop the watchdog now, then keep stopping it: SEC's single stop is undone
  // whenever the SCU services later IPC traffic.
  //
  ScuStopWatchdog (TRUE);

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  WdtKeeperTick,
                  NULL,
                  &mWdtTimerEvent
                  );
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (mWdtTimerEvent, TimerPeriodic, WDT_KEEPER_PERIOD_100NS);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PlatformRuntime: watchdog keeper timer failed: %r\n", Status));
  }

  Status = EfiCreateEventReadyToBootEx (
             TPL_CALLBACK,
             WdtReadyToBoot,
             NULL,
             &mReadyToBootEvent
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PlatformRuntime: ReadyToBoot event failed: %r\n", Status));
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  WdtExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PlatformRuntime: ExitBootServices event failed: %r\n", Status));
  }

  return EFI_SUCCESS;
}
