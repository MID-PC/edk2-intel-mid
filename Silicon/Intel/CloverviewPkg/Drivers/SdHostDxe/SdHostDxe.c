/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/ScuIpcLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/PcdLib.h>
#include <Protocol/SdMmcOverride.h>

#define VCCSDIO_ADDR    0xD5
#define VCCSDIO_NORMAL  0x07

// Langwell GPIO CORE bank (PcdGpioCoreBase)
#define CLV_GPIO_GAFR_OFFSET  0x54
#define CLV_GPIO_GPDR_OFFSET  0x0C
#define CLV_SD_DAT0_LOCAL     42
#define CLV_SD_DAT1_LOCAL     43
#define CLV_SD_DAT2_LOCAL     44
#define CLV_SD_DAT3_LOCAL     45
#define CLV_SD_CMD_LOCAL      50
#define CLV_GPIO_GPSR_OFFSET  0x18
// WLAN_EN: global GPIO 170 = GPIO CORE pin 74
#define CLV_WLAN_EN_LOCAL     74

// Raw SDHCI capability bits
#define SDHCI_CAP_HIGH_SPEED  BIT21
#define SDHCI_CAP_SLOT_MASK   (BIT30 | BIT31)
#define SDHCI_CAP_EMBEDDED    BIT30
#define SDHCI_CAP_SDR50       (1ULL << 32)
#define SDHCI_CAP_SDR104      (1ULL << 33)
#define SDHCI_CAP_DDR50       (1ULL << 34)

// Bus settings: 4-bit / 25 MHz is the fastest legal mode; no 1.8 V rail on this board
#define CT_SD_BUS_WIDTH   4
#define CT_SD_CLOCK_MHZ   25

STATIC EFI_HANDLE  mSdControllerHandle;

/**
  The SD card rail is powered by the PMIC, which only the SCU can reach.
**/
STATIC
VOID
CloverviewEnableSdPower (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       Value;

  Value  = 0;
  Status = ScuIpcPmicRead (VCCSDIO_ADDR, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "SdHostDxe: VCCSDIO read failed: %r; using inherited power state\n", Status));
    return;
  }

  Status = ScuIpcPmicWrite (VCCSDIO_ADDR, VCCSDIO_NORMAL);
  DEBUG ((
    EFI_ERROR (Status) ? DEBUG_WARN : DEBUG_VERBOSE,
    "SdHostDxe: VCCSDIO 0x%02x -> 0x%02x: %r\n",
    Value,
    VCCSDIO_NORMAL,
    Status
    ));
  MicroSecondDelay (20000);
}

STATIC
VOID
SetGpioAlt1Input (
  IN UINTN  GpioBase,
  IN UINTN  LocalPin
  )
{
  UINTN   Register;
  UINT32  Shift;
  UINT32  Value;

  Register = GpioBase + CLV_GPIO_GAFR_OFFSET + ((LocalPin / 16) * sizeof (UINT32));
  Shift    = (UINT32)((LocalPin % 16) * 2);
  Value    = MmioRead32 (Register);
  Value    = (Value & ~(3U << Shift)) | (1U << Shift);
  MmioWrite32 (Register, Value);

  Register = GpioBase + CLV_GPIO_GPDR_OFFSET + ((LocalPin / 32) * sizeof (UINT32));
  Value    = MmioRead32 (Register) & ~((UINT32)BIT0 << (LocalPin % 32));
  MmioWrite32 (Register, Value);
}

STATIC
VOID
SetGpioOutputHigh (
  IN UINTN  GpioBase,
  IN UINTN  LocalPin
  )
{
  UINTN   Register;
  UINT32  Shift;
  UINT32  Value;

  Register = GpioBase + CLV_GPIO_GAFR_OFFSET + ((LocalPin / 16) * sizeof (UINT32));
  Shift    = (UINT32)((LocalPin % 16) * 2);
  Value    = MmioRead32 (Register) & ~(3U << Shift);
  MmioWrite32 (Register, Value);

  MmioWrite32 (
    GpioBase + CLV_GPIO_GPSR_OFFSET + ((LocalPin / 32) * sizeof (UINT32)),
    (UINT32)BIT0 << (LocalPin % 32)
    );

  Register = GpioBase + CLV_GPIO_GPDR_OFFSET + ((LocalPin / 32) * sizeof (UINT32));
  Value    = MmioRead32 (Register) | ((UINT32)BIT0 << (LocalPin % 32));
  MmioWrite32 (Register, Value);
}

STATIC
VOID
CloverviewEnableWlan (
  VOID
  )
{
  UINTN   GpioBase;
  UINT32  Level;

  GpioBase = (UINTN)FixedPcdGet32 (PcdGpioCoreBase);
  SetGpioOutputHigh (GpioBase, CLV_WLAN_EN_LOCAL);
  MicroSecondDelay (50000);

  Level = MmioRead32 (GpioBase + ((CLV_WLAN_EN_LOCAL / 32) * sizeof (UINT32)));
  DEBUG ((
    DEBUG_VERBOSE,
    "SdHostDxe: WLAN_EN (bank 0x%08x pin %u) asserted, GPLR=%u\n",
    GpioBase,
    (UINT32)CLV_WLAN_EN_LOCAL,
    ((Level >> (CLV_WLAN_EN_LOCAL % 32)) & 1)
    ));
}

STATIC
VOID
CloverviewConfigureSdPins (
  VOID
  )
{
  UINTN  GpioBase;

  // GPIOs 138..141 and 146 are local CORE-bank pins 42..45 and 50
  GpioBase = (UINTN)FixedPcdGet32 (PcdGpioCoreBase);
  SetGpioAlt1Input (GpioBase, CLV_SD_DAT0_LOCAL);
  SetGpioAlt1Input (GpioBase, CLV_SD_DAT1_LOCAL);
  SetGpioAlt1Input (GpioBase, CLV_SD_DAT2_LOCAL);
  SetGpioAlt1Input (GpioBase, CLV_SD_DAT3_LOCAL);
  SetGpioAlt1Input (GpioBase, CLV_SD_CMD_LOCAL);
  DEBUG ((DEBUG_VERBOSE, "SdHostDxe: DAT0..3/CMD set to Langwell ALT1 at 0x%08x\n", GpioBase));
}

/**
  Capability override: embedded (no card detect), no HighSpeed/UHS, base clock
  untouched.
**/
STATIC
EFI_STATUS
EFIAPI
CtSdMmcCapability (
  IN     EFI_HANDLE  ControllerHandle,
  IN     UINT8       Slot,
  IN OUT VOID        *SdMmcHcSlotCapability,
  IN OUT UINT32      *BaseClkFreq
  )
{
  UINT64  *Capability;

  if ((ControllerHandle != mSdControllerHandle) || (Slot != 0) ||
      (SdMmcHcSlotCapability == NULL) || (BaseClkFreq == NULL))
  {
    return EFI_SUCCESS;
  }

  Capability  = (UINT64 *)SdMmcHcSlotCapability;
  *Capability &= ~(UINT64)(SDHCI_CAP_HIGH_SPEED | SDHCI_CAP_SLOT_MASK |
                           SDHCI_CAP_SDR50 | SDHCI_CAP_SDR104 | SDHCI_CAP_DDR50);
  *Capability |= SDHCI_CAP_EMBEDDED;

  DEBUG ((
    DEBUG_VERBOSE,
    "SdHostDxe: conservative capability 0x%Lx, physical base clock %u MHz\n",
    *Capability,
    *BaseClkFreq
    ));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
CtSdMmcNotifyPhase (
  IN     EFI_HANDLE               ControllerHandle,
  IN     UINT8                    Slot,
  IN     EDKII_SD_MMC_PHASE_TYPE  PhaseType,
  IN OUT VOID                     *PhaseData
  )
{
  EDKII_SD_MMC_OPERATING_PARAMETERS  *Params;

  if ((ControllerHandle != mSdControllerHandle) || (Slot != 0)) {
    return EFI_SUCCESS;
  }

  if ((PhaseType == EdkiiSdMmcGetOperatingParam) && (PhaseData != NULL)) {
    Params                    = (EDKII_SD_MMC_OPERATING_PARAMETERS *)PhaseData;
    Params->BusWidth          = CT_SD_BUS_WIDTH;
    Params->ClockFreq         = CT_SD_CLOCK_MHZ;
    Params->DriverStrength.Sd = SdDriverStrengthTypeB;

    DEBUG ((
      DEBUG_VERBOSE,
      "SdHostDxe: operating params forced to %d-bit / %dMHz\n",
      Params->BusWidth,
      Params->ClockFreq
      ));
  }

  return EFI_SUCCESS;
}

STATIC EDKII_SD_MMC_OVERRIDE  mCtSdMmcOverride = {
  EDKII_SD_MMC_OVERRIDE_PROTOCOL_VERSION,
  CtSdMmcCapability,
  CtSdMmcNotifyPhase
};

EFI_STATUS
EFIAPI
SdHostDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  OverrideHandle;
  UINTN       Base;
  UINTN       Size;

  Base = (UINTN)FixedPcdGet32 (PcdSdHostBase);
  Size = (UINTN)FixedPcdGet32 (PcdSdHostSize);

  CloverviewEnableSdPower ();
  CloverviewConfigureSdPins ();
  CloverviewEnableWlan ();

  OverrideHandle = NULL;
  Status         = gBS->InstallProtocolInterface (
                          &OverrideHandle,
                          &gEdkiiSdMmcOverrideProtocolGuid,
                          EFI_NATIVE_INTERFACE,
                          &mCtSdMmcOverride
                          );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SdHostDxe: override install failed - %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "SdHostDxe: registering SD slot at 0x%08x + 0x%x (eMMC disabled)\n", Base, Size));

  mSdControllerHandle = NULL;
  Status              = RegisterNonDiscoverableMmioDevice (
                          NonDiscoverableDeviceTypeSdhci,
                          NonDiscoverableDeviceDmaTypeCoherent,
                          NULL,
                          &mSdControllerHandle,
                          1,
                          Base,
                          Size
                          );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SdHostDxe: registration failed - %r\n", Status));
    gBS->UninstallProtocolInterface (
           OverrideHandle,
           &gEdkiiSdMmcOverrideProtocolGuid,
           &mCtSdMmcOverride
           );
    return Status;
  }

  return EFI_SUCCESS;
}
