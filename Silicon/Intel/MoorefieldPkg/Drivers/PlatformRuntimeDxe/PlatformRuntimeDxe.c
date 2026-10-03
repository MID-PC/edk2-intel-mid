/** @file
  Moorefield platform runtime driver.

  Provides the emulated Real Time Clock Architectural Protocol required by the
  DXE Core. 

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Protocol/RealTimeClock.h>

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

  return EFI_SUCCESS;
}
