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
#include <Library/SfiTableLib.h>
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

// Present State bits. BIT0 is Command Inhibit, not card detect; the latched
// "card inserted" bit is BIT2. BIT16 is what SdMmcHcCardDetect() samples.
#define SDHCI_PS_COMMAND_INHIBIT       BIT0
#define SDHCI_PS_CARD_INSERTED         BIT2
#define SDHCI_PS_CARD_STATE_STABLE     BIT3
#define SDHCI_PS_CARD_COMMAND_BUSY     BIT4
#define SDHCI_PS_CARD_DATA_BUSY        BIT5
#define SDHCI_PS_CARD_INTERRUPT        BIT7
#define SDHCI_PS_CLOCK_LINE_EDGE       BIT8
#define SDHCI_PS_CLOCK_LINE_STABLE     BIT9
#define SDHCI_PS_DATA_LINE_LEVEL_MASK  0x0000FF00
#define SDHCI_PS_CD_SIGNAL_LEVEL       BIT16

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

//
// langwell GPIO (TANGIER_GPIO), reached as gpio-langwell.c reaches it on Annedal:
// BAR0 of 00:0c.0, fixed by PcdGpioBase because firmware has no PCI bus to probe.
// GPLR[n] is at base + gplr_offset + (n / 32) * 4, and gplr_offset is 4 here, so
// GPLR[0] is at PcdGpioBase + 4, not at the base.
//
#define LNW_TNG_GPLR_OFFSET  4
#define LNW_TNG_NGPIO        192
#define LNW_TNG_BITS_PER_REG 32
#define LNW_TNG_CONTROLLER   "tangier_gpio"
#define LNW_TNG_REG_GPLR     0

//
// SFI GPIO table. Locating the table is SfiTableLib's job; only the GPIO-specific
// payload layout is this driver's business.
//
#define SFI_GPIO_SIG  "GPIO"
#define SFI_NAME_LEN  16

//
// The pin the SFI GPIO table is searched for by name.
//
#define MOFD_SD_CD_PIN_NAME    "sd_cd_pin"

// Card detect sense: a card pulls the line to ground, so the pin reads LOW when
// present and HIGH when the slot is empty. Getting this backwards is silent: the
// driver sets the controller's CD test level from this verdict, so an empty slot
// is reported as a card the stack keeps retrying, and a real card is reported as
// a disconnect and ignored. MofdCardDetectInit() logs both every boot: empty must
// say "absent".
#define MOFD_SD_CD_ACTIVE_HIGH  FALSE

#pragma pack(1)

//
// Packed SFI GPIO table payload: a flat array of the 34-byte entries below, with
// no count field of its own; (Len - 24) / 34 divides exactly for the tables this
// boot publishes. The 24-byte header is SFI_TABLE_HEADER, owned by SfiTableLib.
//
typedef struct {
  CHAR8  ControllerName[SFI_NAME_LEN];
  UINT16 PinNo;
  CHAR8  PinName[SFI_NAME_LEN];
} SFI_GPIO_TABLE_ENTRY;

#pragma pack()

//
// Conservative bus settings: 4-bit / 25 MHz default-speed SD, no UHS/HS tuning
//
#define MOFD_SD_BUS_WIDTH   4
#define MOFD_SD_CLOCK_MHZ   25
#define MOFD_SD_POWER_UP_SETTLE_US  2000

STATIC EFI_HANDLE  mSdControllerHandle;

//
// Card detect line, resolved once at entry from the SFI GPIO table. Cached
// because the phase callback re-runs on every enumeration retry.
//
STATIC UINTN  mSdCardCdPin;

// One-shot reports for diagnostics the stack re-enters on every retry. An empty
// slot never comes up and SdMmcPciHcDxe retries it from a 100 ms timer, so an
// unrepeated report floods the console. Each re-arms when its state moves.
STATIC BOOLEAN  mHcStateReported;
STATIC UINT32   mHcSignature;
STATIC BOOLEAN  mCdOverrideReported;
STATIC UINT8    mCdOverrideValue;
STATIC BOOLEAN  mNoCardReported;
STATIC BOOLEAN  mCapabilityReported;
STATIC UINT64   mLastCapability;
STATIC UINT32   mLastBaseClkFreq;

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

  // SdMmcHcInitPowerVoltage() asserts when no voltage is reported, and this is
  // a 1.8 V bus.
  *Capability |= SDHCI_CAP_VOLTAGE18;

  // Runs on every init retry, so print only when the negotiated value moved.
  if (mCapabilityReported && (*Capability == mLastCapability) && (*BaseClkFreq == mLastBaseClkFreq)) {
    return EFI_SUCCESS;
  }

  mCapabilityReported = TRUE;
  mLastCapability     = *Capability;
  mLastBaseClkFreq    = *BaseClkFreq;

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
  Compare one fixed-width SFI name field against a NUL-terminated string.

  The field is SFI_NAME_LEN bytes and only NUL-padded by convention, so it cannot go
  to AsciiStrCmp() without risking a read past the table. The end of Name is tested
  before each byte: a name filling its field exactly would otherwise have its
  terminator compared against the next field's first byte and be rejected.
**/
STATIC
BOOLEAN
MofdSfiNameIs (
  IN CONST CHAR8  *Field,
  IN CONST CHAR8  *Name
  )
{
  UINTN  Index;

  for (Index = 0; Index < SFI_NAME_LEN; Index++) {
    if (Name[Index] == '\0') {
      return TRUE;
    }

    if (Field[Index] != Name[Index]) {
      return FALSE;
    }
  }

  return FALSE;
}

/**
  Locate the SFI GPIO table and return its entries. SfiFindTable() has already
  validated the header; the payload must be a whole number of entries so a short
  or padded table cannot make the walk run off the end.
**/
STATIC
CONST SFI_GPIO_TABLE_ENTRY *
MofdFindSfiGpioTable (
  OUT UINTN  *EntryCount
  )
{
  CONST SFI_TABLE_HEADER    *Header;
  CONST SFI_GPIO_TABLE_ENTRY  *Entry;

  *EntryCount = 0;

  Header = SfiFindTable (SFI_GPIO_SIG);
  if (Header == NULL) {
    return NULL;
  }

  if (((Header->Len - sizeof (*Header)) % sizeof (SFI_GPIO_TABLE_ENTRY)) != 0) {
    DEBUG ((
      DEBUG_ERROR,
      "SdHostDxe: SFI GPIO table length %u is not a whole number of %u-byte entries\n",
      Header->Len,
      (UINT32)sizeof (SFI_GPIO_TABLE_ENTRY)
      ));
    return NULL;
  }

  Entry       = (CONST SFI_GPIO_TABLE_ENTRY *)(Header + 1);
  *EntryCount = (Header->Len - sizeof (*Header)) / sizeof (SFI_GPIO_TABLE_ENTRY);

  return Entry;
}

/**
  Resolve the SD card detect pin. The SFI GPIO table names the line and its
  controller, which is what keeps the pin number out of this file;
  PcdSdCardCdPin is only the fallback for a boot that publishes no table.
**/
STATIC
UINTN
MofdCardDetectPin (
  OUT BOOLEAN  *FromSfi
  )
{
  CONST SFI_GPIO_TABLE_ENTRY  *Entry;
  UINTN                        Count;
  UINTN                        Index;

  *FromSfi = FALSE;

  Entry = MofdFindSfiGpioTable (&Count);

  for (Index = 0; Index < Count; Index++) {
    if (MofdSfiNameIs (Entry[Index].ControllerName, LNW_TNG_CONTROLLER) &&
        MofdSfiNameIs (Entry[Index].PinName, MOFD_SD_CD_PIN_NAME))
    {
      *FromSfi = TRUE;
      DEBUG ((
        DEBUG_INFO,
        "SdHostDxe: %a.%a resolved from the SFI GPIO table: pin %u\n",
        LNW_TNG_CONTROLLER,
        MOFD_SD_CD_PIN_NAME,
        (UINT32)Entry[Index].PinNo
        ));
      return (UINTN)Entry[Index].PinNo;
    }
  }

  DEBUG ((
    DEBUG_WARN,
    "SdHostDxe: no %a.%a in the SFI GPIO table, falling back to PcdSdCardCdPin %u\n",
    LNW_TNG_CONTROLLER,
    MOFD_SD_CD_PIN_NAME,
    (UINT32)FixedPcdGet32 (PcdSdCardCdPin)
    ));

  return (UINTN)FixedPcdGet32 (PcdSdCardCdPin);
}

/**
  Read a pin's raw level from the langwell level registers, as gpio_get() does
  for a direction-input pin.
**/
STATIC
BOOLEAN
MofdGpioPinLevel (
  IN UINTN  Pin
  )
{
  UINTN  RegGplr;
  UINTN  Bit;

  if (Pin >= LNW_TNG_NGPIO) {
    return FALSE;
  }

  // GPLR is 192 bits wide, so the pin splits into a register index and a bit at 32.
  // LNW_TNG_NGPIO / 32 is the count of these registers, not the pins in one.
  RegGplr = (UINTN)FixedPcdGet32 (PcdGpioBase) + LNW_TNG_GPLR_OFFSET;
  Bit     = Pin % LNW_TNG_BITS_PER_REG;

  return (MmioRead32 (RegGplr + ((Pin / LNW_TNG_BITS_PER_REG) * sizeof (UINT32))) &
          ((UINT32)1 << Bit)) != 0;
}

/**
  Is a card in the slot, per the card detect GPIO?
**/
STATIC
BOOLEAN
MofdCardPresent (
  IN UINTN  Pin
  )
{
  BOOLEAN  Level;

  Level = MofdGpioPinLevel (Pin);

  return MOFD_SD_CD_ACTIVE_HIGH ? Level : !Level;
}

/**
  Point the host controller's card detect at the GPIO. Host Control BIT7 makes
  BIT6 the card detect source instead of the physical pad, and the generic stack
  samples Present State BIT16, which follows that selection.
**/
STATIC
VOID
MofdApplyCardDetect (
  IN UINTN     Pin,
  IN BOOLEAN   Present
  )
{
  UINTN    HcBase;
  UINT8    HostControl;

  HcBase      = (UINTN)FixedPcdGet32 (PcdSdHostBase);
  HostControl = MmioRead8 (HcBase + SDHCI_HOST_CONTROL_OFFSET);

  HostControl |= SDHCI_CD_SIGNAL_SELECTION;
  if (Present) {
    HostControl |= SDHCI_CD_TEST_LEVEL;
  } else {
    HostControl &= (UINT8)~SDHCI_CD_TEST_LEVEL;
  }

  MmioWrite8 (HcBase + SDHCI_HOST_CONTROL_OFFSET, HostControl);

  // A software reset clears Host Control, so this reapplies every attempt. The
  // write is idempotent, so only report when the value moves.
  if (mCdOverrideReported && (HostControl == mCdOverrideValue)) {
    return;
  }

  mCdOverrideReported = TRUE;
  mCdOverrideValue    = HostControl;

  DEBUG ((
    DEBUG_INFO,
    "SdHostDxe: card detect from GPIO %Lu (level %u, %a sense) -> %a (HC=0x%02x)\n",
    (UINT64)Pin,
    MofdGpioPinLevel (Pin) ? 1U : 0U,
    MOFD_SD_CD_ACTIVE_HIGH ? "active-high" : "active-low",
    Present ? "present" : "absent",
    HostControl
    ));
}

/**
  Resolve the card detect pin and publish it to the host controller. Runs once
  at entry, before registration, so the stack's first SdMmcHcCardDetect() already
  sees the real slot state.
**/
STATIC
UINTN
MofdCardDetectInit (
  VOID
  )
{
  BOOLEAN  FromSfi;
  UINTN    Pin;

  Pin = MofdCardDetectPin (&FromSfi);

  DEBUG ((
    FromSfi ? DEBUG_INFO : DEBUG_WARN,
    "SdHostDxe: card detect pin %Lu (%a SFI GPIO table, \"%a\"/\"%a\"), controller base 0x%x\n",
    (UINT64)Pin,
    FromSfi ? "resolved from the" : "falling back to PcdSdCardCdPin, no",
    LNW_TNG_CONTROLLER,
    MOFD_SD_CD_PIN_NAME,
    FixedPcdGet32 (PcdGpioBase)
    ));

  MofdApplyCardDetect (Pin, MofdCardPresent (Pin));

  // Raw level and verdict in one line so the sense is checkable from the log:
  // empty must read level 1 / absent, card fitted level 0 / present.
  DEBUG ((
    DEBUG_WARN,
    "SdHostDxe: CD pin %Lu level %u -> card %a (%a)\n",
    (UINT64)Pin,
    MofdGpioPinLevel (Pin) ? 1U : 0U,
    MofdCardPresent (Pin) ? "present" : "absent",
    MOFD_SD_CD_ACTIVE_HIGH ? "active-high" : "active-low"
    ));

  return Pin;
}

/**
  Log the host controller state that decides whether the card is reachable at
  all, alongside what the card detect GPIO says. Called from the init-host-post
  phase, which the stack re-enters on every 100 ms tick for as long as the slot
  will not come up, so this emits once per distinct state.
**/
STATIC
VOID
MofdDumpHcState (
  VOID
  )
{
  UINTN    HcBase;
  UINT8    HostControl;
  UINT8    PowerControl;
  UINT8    SoftwareReset;
  UINT16   ClockControl;
  UINT16   NormalIntStatus;
  UINT16   ErrorIntStatus;
  UINT32   Present;
  UINT32   Signature;
  BOOLEAN  Changed;
  BOOLEAN  CardPresent;

  HcBase = (UINTN)FixedPcdGet32 (PcdSdHostBase);

  Present         = MmioRead32 (HcBase + SDHCI_PRESENT_STATE_OFFSET);
  HostControl     = MmioRead8 (HcBase + SDHCI_HOST_CONTROL_OFFSET);
  PowerControl    = MmioRead8 (HcBase + SDHCI_POWER_CONTROL_OFFSET);
  ClockControl    = MmioRead16 (HcBase + SDHCI_CLOCK_CONTROL_OFFSET);
  SoftwareReset   = MmioRead8 (HcBase + SDHCI_SOFTWARE_RESET_OFFSET);
  NormalIntStatus = MmioRead16 (HcBase + SDHCI_NORMAL_INT_STATUS_OFFSET);
  ErrorIntStatus  = MmioRead16 (HcBase + SDHCI_ERROR_INT_STATUS_OFFSET);
  CardPresent     = MofdCardPresent (mSdCardCdPin);

  // Signature covers only what decides reachability: card detect, bus power,
  // voltage and clock. The data lines and interrupt status move on every failed
  // command, so including them would put the dump back in a per-attempt loop.
  Signature = ((UINT32)CardPresent ? BIT0 : 0) |
              (Present & (SDHCI_PS_CARD_INSERTED | SDHCI_PS_CD_SIGNAL_LEVEL)) |
              (UINT32)(PowerControl & (SDHCI_PC_SD_BUS_POWER | SDHCI_PC_SD_VOLTAGE_MASK)) |
              (UINT32)(ClockControl & (SDHCI_CC_INTERNAL_CLOCK_STABLE | SDHCI_CC_SD_CLOCK_ENABLE));

  if (mHcStateReported && (Signature == mHcSignature)) {
    return;
  }

  Changed          = mHcStateReported;
  mHcStateReported = TRUE;
  mHcSignature     = Signature;

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state dump (%a)\n",
    Changed ? "state changed" : "first sample"
    ));

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state: card detect GPIO %Lu says %a, controller reports card %a (CD level %u)\n",
    (UINT64)mSdCardCdPin,
    CardPresent ? "present" : "absent",
    (Present & SDHCI_PS_CARD_INSERTED) != 0 ? "inserted" : "absent",
    ((Present & SDHCI_PS_CD_SIGNAL_LEVEL) != 0) ? 1U : 0U
    ));

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state: present=0x%08x (card interrupt %a, clk line %a/%a, data lines 0x%02x)\n",
    Present,
    (Present & SDHCI_PS_CARD_INTERRUPT) != 0 ? "asserted" : "deasserted",
    (Present & SDHCI_PS_CLOCK_LINE_EDGE) != 0 ? "edge" : "steady",
    (Present & SDHCI_PS_CLOCK_LINE_STABLE) != 0 ? "stable" : "moving",
    (UINT32)((Present & SDHCI_PS_DATA_LINE_LEVEL_MASK) >> 8)
    ));

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state: hostctrl=0x%02x, powerctrl=0x%02x (bus power %a, vsel %u)\n",
    HostControl,
    PowerControl,
    (PowerControl & SDHCI_PC_SD_BUS_POWER) != 0 ? "on" : "off",
    (UINT32)(PowerControl & SDHCI_PC_SD_VOLTAGE_MASK) >> 1
    ));

  DEBUG ((
    DEBUG_ERROR,
    "SdHostDxe: HC state: clockctrl=0x%04x (div %u, internal clock %a, SD clock %a), "
    "swreset=0x%02x, normal=0x%04x, error=0x%04x\n",
    ClockControl,
    (UINT32)(ClockControl >> 8),
    (ClockControl & SDHCI_CC_INTERNAL_CLOCK_STABLE) != 0 ? "on" : "off",
    (ClockControl & SDHCI_CC_SD_CLOCK_ENABLE) != 0 ? "on" : "off",
    SoftwareReset,
    NormalIntStatus,
    ErrorIntStatus
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

  // Gate the SD clock first: CLOCK_CONTROL BIT0 is the internal clock enable and
  // BIT2 the card clock enable, same layout as u-boot's sdhci.h.
  MmioWrite16 (HcBase + SDHCI_CLOCK_CONTROL_OFFSET, 0);

  // Then raise the bus at 1.8 V, which is what SdMmcHcInitPowerVoltage() selects
  // for this slot anyway (capabilities report Voltage18 only).
  MmioWrite8 (
    HcBase + SDHCI_POWER_CONTROL_OFFSET,
    SDHCI_PC_VSEL_1_8V | SDHCI_PC_SD_BUS_POWER
    );

  DEBUG ((DEBUG_VERBOSE, "SdHostDxe: bus powered at 1.8V with the SD clock gated\n"));
}

/**
  Phase override: match the u-boot power-up order (MofdPowerUpWithClockGated()),
  report the card detect GPIO to the host controller, and give the card the
  startup window the SD spec requires before CMD0 goes out.
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
  BOOLEAN                             CardPresent;

  if ((ControllerHandle != mSdControllerHandle) || (Slot != 0)) {
    return EFI_SUCCESS;
  }

  if (PhaseType == EdkiiSdMmcResetPost) {
    // The software reset the stack just ran cleared Host Control.
    MofdApplyCardDetect (mSdCardCdPin, MofdCardPresent (mSdCardCdPin));
  }

  if (PhaseType == EdkiiSdMmcInitHostPre) {
    CardPresent = MofdCardPresent (mSdCardCdPin);

    // Refuse the init before anything is powered or clocked. SdMmcHcInitHost()
    // propagates this without touching the bus, so no card command is issued and
    // the 100 ms enumeration timer retries only this GPIO read instead of sending
    // CMD0/CMD8 at an empty slot forever, which is the "TRB failed" flood this
    // path exists to stop.
    if (!CardPresent) {
      // Runs on every enumeration tick while the slot stays empty, so report once.
      if (!mNoCardReported) {
        mNoCardReported = TRUE;

        DEBUG ((
          DEBUG_INFO,
          "SdHostDxe: no card in the slot (card detect GPIO %Lu), skipping host init\n",
          (UINT64)mSdCardCdPin
          ));
      }

      return EFI_NO_MEDIA;
    }

    // Re-arm so a later removal is reported again.
    mNoCardReported = FALSE;

    MofdPowerUpWithClockGated ();
  }

  if (PhaseType == EdkiiSdMmcInitHostPost) {
    MofdApplyCardDetect (mSdCardCdPin, MofdCardPresent (mSdCardCdPin));
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

  //
  // Tell the controller the slot state before registration, so the stack's first
  // SdMmcHcCardDetect() does not read a hardcoded "card present".
  //
  mSdCardCdPin = MofdCardDetectInit ();

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
