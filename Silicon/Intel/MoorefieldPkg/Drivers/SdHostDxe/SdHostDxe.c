/** @file
  Moorefield external SD card SDHCI registration.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/PcdLib.h>
#include <Protocol/SdMmcOverride.h>

//
// South-complex PMU register block (PcdPmuBase)
// 
#define PMU_STS_OFFSET      0x00
#define PMU_CMD_OFFSET      0x04
#define PMU_SSC_OFFSET      0x30
#define PMU_SSS_OFFSET      0x40
#define PMU_STS_BUSY        BIT8
#define PMU_CMD_SET_CFG     0x00002201  // u-boot "update modified PMU values"
#define PMU_POLL_LIMIT      500000U

//
// SDHCI Host Control register (offset 0x28) card-detect controls
//
#define SDHCI_HOST_CONTROL_OFFSET       0x28
#define SDHCI_CD_TEST_LEVEL             BIT6
#define SDHCI_CD_SIGNAL_SELECTION       BIT7

//
// Registers dumped by MofdDumpHcState() to tell "no card" apart from
// "card present but not answering".
//
#define SDHCI_PRESENT_STATE_OFFSET      0x24
#define SDHCI_POWER_CONTROL_OFFSET      0x29
#define SDHCI_CLOCK_CONTROL_OFFSET      0x2C
#define SDHCI_SOFTWARE_RESET_OFFSET     0x2F
#define SDHCI_NORMAL_INT_STATUS_OFFSET  0x30
#define SDHCI_ERROR_INT_STATUS_OFFSET   0x38

// Present State register bits
#define SDHCI_PS_CARD_INSERTED          BIT0
#define SDHCI_PS_CARD_POWER             BIT2
#define SDHCI_PS_CARD_STATE_STABLE      BIT3
#define SDHCI_PS_CARD_COMMAND_BUSY      BIT4
#define SDHCI_PS_CARD_DATA_BUSY         BIT5
#define SDHCI_PS_CLOCK_LINE_LEVEL       BIT8
#define SDHCI_PS_CLOCK_LINE_STABLE      BIT9
#define SDHCI_PS_DATA_LINE_LEVEL_MASK   0x0000FF00
#define SDHCI_PS_CD_SIGNAL_LEVEL        BIT16

//
// Power Control register bits
#define SDHCI_PC_SD_BUS_POWER           BIT0
#define SDHCI_PC_SD_VOLTAGE_MASK        0x0E
#define SDHCI_PC_VSEL_1_8V              0x0A

// Clock Control register bits
#define SDHCI_CC_INTERNAL_CLOCK_STABLE  BIT1
#define SDHCI_CC_SD_CLOCK_ENABLE        BIT2

// Raw SDHCI capability bits (SDHCI_CAPABILITIES register)
#define SDHCI_CAP_HIGH_SPEED  BIT21
#define SDHCI_CAP_VOLTAGE33   BIT24
#define SDHCI_CAP_VOLTAGE30   BIT25
#define SDHCI_CAP_VOLTAGE18   BIT26
#define SDHCI_CAP_SDR50       (1ULL << 32)
#define SDHCI_CAP_SDR104      (1ULL << 33)
#define SDHCI_CAP_DDR50       (1ULL << 34)

//
// Shady Cove PMIC VLDO (SD card VDD) rail control.
//
#define PMIC_VLDOCNT_ADDR       0x0AF
#define PMIC_VLDOCNT_VSWITCH    BIT1

// Conservative bus settings: 4-bit / 25 MHz default-speed SD, no UHS/HS tuning
#define MOFD_SD_BUS_WIDTH   4
#define MOFD_SD_CLOCK_MHZ   25
#define MOFD_SD_POWER_UP_SETTLE_US  2000

STATIC EFI_HANDLE  mSdControllerHandle;

/**
  Wait for the PMU to be idle.
**/
STATIC
EFI_STATUS
PmuWaitNotBusy (
  IN UINTN  PmuBase
  )
{
  UINT32  Retry;
  UINT32  Status;

  for (Retry = 0; Retry < PMU_POLL_LIMIT; Retry++) {
    Status = MmioRead32 (PmuBase + PMU_STS_OFFSET);
    if (Status == MAX_UINT32) {
      return EFI_DEVICE_ERROR;
    }

    if ((Status & PMU_STS_BUSY) == 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  return EFI_TIMEOUT;
}

/**
  Move a PMU LSS to D0 (on) or D3hot (off). Same as u-boot pmu_power_lss().
**/
STATIC
EFI_STATUS
PmuPowerLss (
  IN UINTN    PmuBase,
  IN UINT32   Lss,
  IN BOOLEAN  On
  )
{
  UINT32      Word;
  UINT32      Shift;
  UINT32      Ssc;
  EFI_STATUS  Status;

  Word  = (Lss * 2) / 32;
  Shift = (Lss * 2) % 32;

  Status = PmuWaitNotBusy (PmuBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Seed the SSC word from the current sub-system status.
  Ssc = MmioRead32 (PmuBase + PMU_SSS_OFFSET + (Word * sizeof (UINT32)));

  if (On) {
    Ssc &= ~(0x3U << Shift);  // D0
  } else {
    Ssc |= (0x3U << Shift);   // D3hot
  }

  MmioWrite32 (PmuBase + PMU_SSC_OFFSET + (Word * sizeof (UINT32)), Ssc);
  MmioWrite32 (PmuBase + PMU_CMD_OFFSET, PMU_CMD_SET_CFG);

  return PmuWaitNotBusy (PmuBase);
}

/**
  Power on the SD/SDIO0 power island via the PMU.
**/
STATIC
VOID
MoorefieldEnableSdPower (
  VOID
  )
{
  UINTN       PmuBase;
  UINT32      Lss;
  EFI_STATUS  Status;

  PmuBase = (UINTN)FixedPcdGet32 (PcdPmuBase);
  Lss     = FixedPcdGet32 (PcdSdHostPmuLss);

  Status = PmuPowerLss (PmuBase, Lss, TRUE);
  DEBUG ((
    EFI_ERROR (Status) ? DEBUG_WARN : DEBUG_INFO,
    "SdHostDxe: PMU LSS %u -> D0: %r\n",
    Lss,
    Status
    ));

  MicroSecondDelay (20000);
}

/**
  Capability override: keep the removable slot and drop High-Speed/UHS so the
  card comes up in the safest default-speed mode, and make sure the 1.8 V
  signalling bit survives.
**/
STATIC
EFI_STATUS
EFIAPI
MofdSdMmcCapability (
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
  *Capability &= ~(UINT64)(SDHCI_CAP_HIGH_SPEED | SDHCI_CAP_SDR50 |
                           SDHCI_CAP_SDR104 | SDHCI_CAP_DDR50);

  //
  // SdMmcHcInitPowerVoltage() asserts when no voltage is reported at all, and
  // this part is a 1.8 V bus, so make sure that bit is there.
  //
  *Capability |= SDHCI_CAP_VOLTAGE18;

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: capability 0x%Lx (3.3V %a, 3.0V %a, 1.8V %a, base clock %u MHz)\n",
    *Capability,
    ((*Capability & SDHCI_CAP_VOLTAGE33) != 0) ? "yes" : "no",
    ((*Capability & SDHCI_CAP_VOLTAGE30) != 0) ? "yes" : "no",
    ((*Capability & SDHCI_CAP_VOLTAGE18) != 0) ? "yes" : "no",
    *BaseClkFreq
    ));

  return EFI_SUCCESS;
}

/**
  Force software card detect. The CD pin is inverted on this board, so the
  generic stack has to be told to take the Host Control "card detect test
  level" as the card-detect source instead; the Present State "card inserted"
  bit it samples then follows that level.
**/
STATIC
VOID
MofdForceSoftwareCardDetect (
  VOID
  )
{
  UINTN  HcBase;
  UINT8  HostControl;

  HcBase      = (UINTN)FixedPcdGet32 (PcdSdHostBase);
  HostControl = MmioRead8 (HcBase + SDHCI_HOST_CONTROL_OFFSET);
  HostControl |= (SDHCI_CD_TEST_LEVEL | SDHCI_CD_SIGNAL_SELECTION);
  MmioWrite8 (HcBase + SDHCI_HOST_CONTROL_OFFSET, HostControl);

  DEBUG ((DEBUG_VERBOSE, "SdHostDxe: forced software card detect (HC=0x%02x)\n", HostControl));
}

/**
  Log the host-controller state that decides whether the card can be reached at
  all, and sample the real card-detect pin (bypassing the test level the override
  normally forces) so a missing card can be told apart from a card that is
  present but not answering.
**/
STATIC
VOID
MofdDumpHcState (
  VOID
  )
{
  UINTN    HcBase;
  UINT8    HostControl;
  UINT8    SavedHostControl;
  UINT16   ClockControl;
  UINT32   Present;
  UINT32   CdPresent;

  HcBase = (UINTN)FixedPcdGet32 (PcdSdHostBase);

  SavedHostControl = MmioRead8 (HcBase + SDHCI_HOST_CONTROL_OFFSET);
  MmioWrite8 (
    HcBase + SDHCI_HOST_CONTROL_OFFSET,
    (UINT8)(SavedHostControl & ~SDHCI_CD_SIGNAL_SELECTION)
    );
  Present = MmioRead32 (HcBase + SDHCI_PRESENT_STATE_OFFSET);
  MmioWrite8 (
    HcBase + SDHCI_HOST_CONTROL_OFFSET,
    (UINT8)(SavedHostControl | SDHCI_CD_TEST_LEVEL | SDHCI_CD_SIGNAL_SELECTION)
    );

  CdPresent     = Present & (SDHCI_PS_CARD_INSERTED | SDHCI_PS_CD_SIGNAL_LEVEL);
  Present       = MmioRead32 (HcBase + SDHCI_PRESENT_STATE_OFFSET);
  HostControl   = MmioRead8 (HcBase + SDHCI_HOST_CONTROL_OFFSET);
  ClockControl  = MmioRead16 (HcBase + SDHCI_CLOCK_CONTROL_OFFSET);

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state: CD pin card %a (CD level %u), forced-present %a\n",
    (CdPresent & SDHCI_PS_CARD_INSERTED) != 0 ? "inserted" : "absent",
    ((CdPresent & SDHCI_PS_CD_SIGNAL_LEVEL) != 0) ? 1U : 0U,
    (Present & SDHCI_PS_CARD_INSERTED) != 0 ? "yes" : "no"
    ));

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state: present=0x%08x (bus power %a, clk line %a/%a, data lines 0x%02x)\n",
    Present,
    (Present & SDHCI_PS_CARD_POWER) != 0 ? "high" : "low",
    (Present & SDHCI_PS_CLOCK_LINE_LEVEL) != 0 ? "high" : "low",
    (Present & SDHCI_PS_CLOCK_LINE_STABLE) != 0 ? "stable" : "moving",
    (UINT32)((Present & SDHCI_PS_DATA_LINE_LEVEL_MASK) >> 8)
    ));

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state: hostctrl=0x%02x, powerctrl=0x%02x (bus power %a, vsel %u)\n",
    HostControl,
    MmioRead8 (HcBase + SDHCI_POWER_CONTROL_OFFSET),
    (MmioRead8 (HcBase + SDHCI_POWER_CONTROL_OFFSET) & SDHCI_PC_SD_BUS_POWER) != 0 ? "on" : "off",
    (UINT32)(MmioRead8 (HcBase + SDHCI_POWER_CONTROL_OFFSET) & SDHCI_PC_SD_VOLTAGE_MASK) >> 1
    ));

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state: clockctrl=0x%04x (div %u, internal clock %a, SD clock %a), "
    "swreset=0x%02x, normal=0x%04x, error=0x%04x\n",
    ClockControl,
    (UINT32)(ClockControl >> 8),
    (ClockControl & SDHCI_CC_INTERNAL_CLOCK_STABLE) != 0 ? "on" : "off",
    (ClockControl & SDHCI_CC_SD_CLOCK_ENABLE) != 0 ? "on" : "off",
    MmioRead8 (HcBase + SDHCI_SOFTWARE_RESET_OFFSET),
    MmioRead16 (HcBase + SDHCI_NORMAL_INT_STATUS_OFFSET),
    MmioRead16 (HcBase + SDHCI_ERROR_INT_STATUS_OFFSET)
    ));
}

/**
  Power the SD bus up with the clock gated, the way u-boot's tangier driver does
  it: sdhci_init() runs sdhci_reset() (which clears the clock control register),
  then sdhci_set_power(), and the SD clock is only programmed later from
  sdhci_set_ios().
**/
STATIC
VOID
MofdPowerUpWithClockGated (
  VOID
  )
{
  UINTN  HcBase;

  HcBase = (UINTN)FixedPcdGet32 (PcdSdHostBase);

  //
  // Gate the SD clock first: CLOCK_CONTROL BIT0 is the internal clock enable and
  // BIT2 the card clock enable, same layout as u-boot's sdhci.h.
  //
  MmioWrite16 (HcBase + SDHCI_CLOCK_CONTROL_OFFSET, 0);

  //
  // Then raise the bus at 1.8 V, which is what SdMmcHcInitPowerVoltage() would
  // select for this slot anyway (capabilities report Voltage18 only).
  //
  MmioWrite8 (
    HcBase + SDHCI_POWER_CONTROL_OFFSET,
    SDHCI_PC_VSEL_1_8V | SDHCI_PC_SD_BUS_POWER
    );

  DEBUG ((DEBUG_VERBOSE, "SdHostDxe: bus powered at 1.8V with the SD clock gated\n"));
}

/**
  Phase override: match the u-boot power-up order (MofdPowerUpWithClockGated()),
  force software card detect (the slot is cd-inverted, and the generic stack
  cannot invert the hardware CD line), and give the card the startup window the
  SD spec requires before CMD0 goes out.
**/
STATIC
EFI_STATUS
EFIAPI
MofdSdMmcNotifyPhase (
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

  if (PhaseType == EdkiiSdMmcResetPost) {
    MofdForceSoftwareCardDetect ();
  }

  if (PhaseType == EdkiiSdMmcInitHostPre) {
    MofdPowerUpWithClockGated ();
  }
  
  if (PhaseType == EdkiiSdMmcInitHostPost) {
    MofdForceSoftwareCardDetect ();
    MofdDumpHcState ();
    gBS->Stall (MOFD_SD_POWER_UP_SETTLE_US);
  }

  if ((PhaseType == EdkiiSdMmcGetOperatingParam) && (PhaseData != NULL)) {
    Params                    = (EDKII_SD_MMC_OPERATING_PARAMETERS *)PhaseData;
    Params->BusWidth          = MOFD_SD_BUS_WIDTH;
    Params->ClockFreq         = MOFD_SD_CLOCK_MHZ;
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

STATIC EDKII_SD_MMC_OVERRIDE  mMofdSdMmcOverride = {
  EDKII_SD_MMC_OVERRIDE_PROTOCOL_VERSION,
  MofdSdMmcCapability,
  MofdSdMmcNotifyPhase
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

  MoorefieldEnableSdPower ();

  OverrideHandle = NULL;
  Status         = gBS->InstallProtocolInterface (
                          &OverrideHandle,
                          &gEdkiiSdMmcOverrideProtocolGuid,
                          EFI_NATIVE_INTERFACE,
                          &mMofdSdMmcOverride
                          );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SdHostDxe: override install failed - %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "SdHostDxe: registering SD slot at 0x%08x + 0x%x\n", Base, Size));

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
           &mMofdSdMmcOverride
           );
    return Status;
  }

  return EFI_SUCCESS;
}
