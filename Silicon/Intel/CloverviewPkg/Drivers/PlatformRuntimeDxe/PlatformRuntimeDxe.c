/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/ScuIpcLib.h>
#include <Protocol/RealTimeClock.h>
#include <Guid/EventGroup.h>

// Re-issue the watchdog stop every 5s while boot services are alive
#define WDT_KEEPER_PERIOD_100NS  (5 * 10 * 1000 * 1000ULL)

STATIC EFI_EVENT  mWdtTimerEvent      = NULL;
STATIC EFI_EVENT  mReadyToBootEvent   = NULL;
STATIC EFI_EVENT  mExitBootServicesEvent = NULL;
STATIC UINT32     mWdtStopCount       = 0;
STATIC UINT32     mWdtFailCount       = 0;

STATIC EFI_TIME  mTime = {
  2024,
  1,
  1,
  0,
  0,
  0,
  0,
  0,
  EFI_UNSPECIFIED_TIMEZONE,
  0,
  0
};

STATIC UINT32  mTicks = 0;

/**
  Stop the SCU kernel watchdog; Verbose=FALSE keeps the periodic keeper quiet
**/
STATIC
VOID
ScuStopWatchdog (
  IN BOOLEAN  Verbose
  )
{
  EFI_STATUS  Status;

  Status = ScuIpcSimpleCommand (
             SCU_IPC_MSG_WATCHDOG_TIMER,
             SCU_IPC_WDT_SUB_STOP
             );
  switch (Status) {
  case EFI_SUCCESS:
    mWdtStopCount++;
    if (Verbose) {
      DEBUG ((
        DEBUG_INFO,
        "PlatformRuntime: SCU watchdog stopped (stops %u, failures %u)\n",
        mWdtStopCount,
        mWdtFailCount
        ));
    }

    break;

  case EFI_NOT_FOUND:
    //
    // Not a failure: this counts the SCU as running, which is what the keeper
    // is reporting, but it will never succeed so it should not look like a
    // regression either.
    //
    if (Verbose) {
      DEBUG ((DEBUG_WARN, "PlatformRuntime: SCU IPC not present, watchdog left as-is\n"));
    }

    break;

  default:
    mWdtFailCount++;
    if (Verbose) {
      DEBUG ((DEBUG_ERROR, "PlatformRuntime: SCU watchdog stop failed: %r\n", Status));
    }

    break;
  }
}

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
  ExitBootServices: last chance to stop the watchdog; the OS never talks to the SCU.
**/
STATIC
VOID
EFIAPI
WdtExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  // No DEBUG(); display may be OS-owned and this runs at TPL_NOTIFY
  if (mWdtTimerEvent != NULL) {
    gBS->SetTimer (mWdtTimerEvent, TimerCancel, 0);
  }

  ScuStopWatchdog (FALSE);
  ScuStopWatchdog (FALSE);
}

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

  // Advance the counter so callers that wait on time make progress
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

  // Stop now and keep stopping (SEC's single stop is undone by later IPC traffic)
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
