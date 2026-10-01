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

// Present State register bits. Note BIT0 is Command Inhibit, not card detect:
// the latched "card inserted" bit is BIT2, and BIT16 is the one the generic
// stack's SdMmcHcCardDetect() samples.
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
// langwell GPIO controller (TANGIER_GPIO), reached the way gpio-langwell.c
// reaches it on an Annedal part: register base is BAR0 of the GPIO PCI function
// 0000:00:0c.0, mapped at 0xFF008000 (iomem.txt). There is no PCI bus in
// firmware to probe that BAR, so it comes from PcdGpioBase.
//
// lnw_gpio_ddata[TANGIER_GPIO] sets gplr_offset = 4, and gpio_reg() computes
//
//   reg_gplr = reg_base + gplr_offset
//   GPLR[n]  = reg_gplr + (n / 32) * 4      (GPLR is enum value 0, so the
//                                           reg_type * nreg * 4 term drops out)
//
// which is why GPLR[0] lands at 0xFF008004 rather than at the base. The
// reg_type < GITR test in gpio_reg() selects reg_gplr for GPLR, and that is
// exactly the 0xFF008300 / 0xFF008004 case its comment calls out for TNG.
//
#define LNW_TNG_GPLR_OFFSET  4
#define LNW_TNG_NGPIO        192
#define LNW_TNG_BITS_PER_REG 32
#define LNW_TNG_CONTROLLER   "tangier_gpio"
#define LNW_TNG_REG_GPLR     0

//
// SFI tables. Same window and validation rules SfiMemoryMapLib applies: a
// 16-byte-aligned walk of 0x000E0000-0x00100000 matching on the four-character
// signature, with the table's declared length required to fit the window and
// its bytes required to sum to zero.
//
#define SFI_GPIO_SEARCH_BASE   0x000E0000
#define SFI_GPIO_SEARCH_SIZE   0x00020000
#define SFI_GPIO_SEARCH_STRIDE 16
#define SFI_GPIO_SIG           "GPIO"
#define SFI_NAME_LEN           16

//
// The pin the SFI GPIO table is searched for by name.
//
#define MOFD_SD_CD_PIN_NAME    "sd_cd_pin"

//
// Card detect sense.
//
// A card in the slot pulls the CD line to ground, so the pin reads LOW for
// "present" and HIGH for an empty slot. That is the plain SD socket sense, and
// it is what this board measures: with the slot empty the pin reads high, and
// fitting a card pulls it low.
//
// Getting this backwards is silent and self-inflicting, because the driver sets
// the host controller's card-detect test level from this verdict: assume
// active-high on an active-low board and an empty slot is reported to the stack
// as "card present", so the stack keeps initialising and retrying a slot with
// nothing in it, while a card actually fitted is reported as a disconnect and is
// ignored with no log at all.
//
// It stays a named constant because the level that means "in" is a board wiring
// property the source does not record, and the two failure modes above are worth
// exactly one edit rather than a hunt. MofdCardDetectInit() logs the raw level
// and the verdict on every boot: with the slot EMPTY the driver must report
// "absent", and with a card fitted "present".
//
#define MOFD_SD_CD_ACTIVE_HIGH  FALSE

#pragma pack(1)

//
// Packed SFI GPIO table. The 24-byte header matches the layout SfiMemoryMapLib
// already validates: signature, length, revision, checksum, OEM id, OEM table id.
// The payload is a flat array of the 34-byte entries below, with no count field
// of its own; (Len - 24) / 34 divides exactly for the tables this boot
// publishes.
//
typedef struct {
  CHAR8  Sig[4];
  UINT32 Len;
  UINT8  Rev;
  UINT8  Csum;
  CHAR8  OemId[6];
  CHAR8  OemTableId[8];
} SFI_GPIO_TABLE_HEADER;

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
// because the phase callback is re-entered on every enumeration retry and the
// lookup walks the whole SFI window to find the table.
//
STATIC UINTN  mSdCardCdPin;

//
// Diagnostic de-duplication for the diagnostics the stack re-enters on every
// retry of a slot that will not come up. An empty slot never does come up, and
// SdMmcPciHcDxe retries it from a 100 ms periodic enumeration timer for the rest
// of the run, so without these the same block of lines repeats ten times a
// second forever and, on the serial console this board is debugged over, that
// flood is the only thing visible. Each is suppressed once reported and re-armed
// only when the state it describes actually moves, so a card being inserted or
// removed is still reported.
//
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

  //
  // SdMmcHcInitPowerVoltage() asserts when no voltage is reported at all, and
  // this part is a 1.8 V bus, so make sure that bit is there.
  //
  *Capability |= SDHCI_CAP_VOLTAGE18;

  //
  // This runs on every init retry, and the masked-off bits above are applied
  // fresh each time, so print only when the negotiated value has moved.
  //
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

  The field is SFI_NAME_LEN bytes and is only NUL-padded by convention, so it
  cannot be handed to AsciiStrCmp() without risking a read past the end of the
  table. The end of Name has to be tested before the byte is compared: a name
  that fills its field exactly -- which is the case for the four-character "GPIO"
  signature -- would otherwise be compared against the first byte of the next
  field and rejected, so the table could never be found.
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
  Locate and validate the SFI GPIO table.

  The scan mirrors what SfiMemoryMapLib already does over the same window: walk
  it 16 bytes at a time looking for the signature, then require the declared
  length to fit the window and the low byte of the table's byte sum to be zero.
  The length check is what keeps a garbage match from turning into a wild read of
  the payload.
**/
STATIC
CONST SFI_GPIO_TABLE_HEADER *
MofdSfiFindGpioTable (
  VOID
  )
{
  CONST SFI_GPIO_TABLE_HEADER  *Header;
  UINT8                        Sum;
  UINTN                        Index;
  UINTN                        Address;

  for (Address = SFI_GPIO_SEARCH_BASE;
       Address < (SFI_GPIO_SEARCH_BASE + SFI_GPIO_SEARCH_SIZE);
       Address += SFI_GPIO_SEARCH_STRIDE)
  {
    Header = (CONST SFI_GPIO_TABLE_HEADER *)Address;

    if (!MofdSfiNameIs (Header->Sig, SFI_GPIO_SIG)) {
      continue;
    }

    //
    // The table has to fit inside the SFI window from where it was found, and
    // its payload has to be a whole number of entries. This is what keeps a
    // garbage signature match from turning into a wild read of the payload.
    //
    if ((Header->Len < sizeof (SFI_GPIO_TABLE_HEADER)) ||
        (Header->Len > (SFI_GPIO_SEARCH_BASE + SFI_GPIO_SEARCH_SIZE - Address)) ||
        ((Header->Len - sizeof (SFI_GPIO_TABLE_HEADER)) % sizeof (SFI_GPIO_TABLE_ENTRY) != 0))
    {
      continue;
    }

    //
    // SFI checksum: the bytes of the whole table sum to zero, low byte only.
    // The accumulator is deliberately UINT8 so the addition wraps, matching the
    // UINT8 Sum in SfiMemoryMapLib's SfiTableIsValid(); accumulating in a wider
    // type looks like it works and silently rejects every real table.
    //
    Sum = 0;
    for (Index = 0; Index < Header->Len; Index++) {
      Sum = (UINT8)(Sum + ((CONST UINT8 *)Header)[Index]);
    }

    if (Sum == 0) {
      return Header;
    }
  }

  return NULL;
}

/**
  Resolve the SD card detect GPIO pin number.

  The SFI GPIO table this boot publishes is the authority: it names the line
  ("sd_cd_pin") and the controller it lives on ("tangier_gpio"), and it is what
  keeps the pin number from being a magic constant in this file. PcdSdCardCdPin
  is only a fallback for a boot that does not publish the table.
**/
STATIC
UINTN
MofdCardDetectPin (
  OUT BOOLEAN  *FromSfi
  )
{
  CONST SFI_GPIO_TABLE_HEADER  *Header;
  CONST SFI_GPIO_TABLE_ENTRY   *Entry;
  UINTN                        Count;
  UINTN                        Index;

  *FromSfi = FALSE;

  Header = MofdSfiFindGpioTable ();
  if (Header != NULL) {
    Entry = (CONST SFI_GPIO_TABLE_ENTRY *)(Header + 1);
    Count = (Header->Len - sizeof (SFI_GPIO_TABLE_HEADER)) / sizeof (SFI_GPIO_TABLE_ENTRY);

    for (Index = 0; Index < Count; Index++) {
      if (MofdSfiNameIs (Entry[Index].ControllerName, LNW_TNG_CONTROLLER) &&
          MofdSfiNameIs (Entry[Index].PinName, MOFD_SD_CD_PIN_NAME))
      {
        *FromSfi = TRUE;
        return Entry[Index].PinNo;
      }
    }
  }

  return (UINTN)FixedPcdGet32 (PcdSdCardCdPin);
}

/**
  Read the raw level of a pin in the langwell GPIO controller's level
  registers, the way gpio-langwell.c reads it with gpio_get() for a
  direction-input pin.
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

  //
  // GPLR is a 192-bit level register, so a pin number splits into a register
  // index and a bit within it at 32. LNW_TNG_NGPIO / 32 is the *count* of these
  // registers (6), not the pins in one, and using it here would walk the pin
  // number into the wrong register entirely.
  //
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
  Point the host controller's card detect at the GPIO's answer.

  Host Control BIT7 (SD Bus Card Detect Signal Selection) makes the controller
  report the BIT6 test level as the card detect source instead of the physical
  pad; the generic stack samples Present State BIT16 ("SD Bus Inserted"), which
  follows that selection. So setting the test level to the GPIO's answer is what
  the stack actually reads, and it is the difference between the stack knowing
  the slot is empty and the stack talking to a card that is not there.
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

  //
  // A software reset clears Host Control, so this has to be reapplied on every
  // attempt; the write is idempotent, so only report it when the value moves.
  //
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
  Resolve the card detect pin and publish it to the host controller.

  Runs once at entry, before the device is registered, so the very first
  SdMmcHcCardDetect() the stack performs already sees the real slot state
  instead of a hardcoded "card present".
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

  //
  // One unconditional line carrying the raw level and the verdict, so the sense
  // can be confirmed from the log without having to correlate the dump lines.
  // With the slot empty this must read level 1 / absent; with a card fitted,
  // level 0 / present.
  //
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
  Log the host-controller state that decides whether the card can be reached at
  all, alongside what the card detect GPIO says.

  Called from the init-host-post phase, which the stack reaches again on every
  100 ms enumeration tick for as long as the slot will not come up. The dump is
  therefore emitted once per distinct state rather than once per attempt: an
  empty slot is the case that retries forever, and repeating an identical dump
  ten times a second says nothing the first copy did not.
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

  //
  // The signature deliberately covers only what decides whether a card is
  // reachable: the card detect answer and the view the controller is reporting,
  // the bus power and voltage, and the clock. The data-line levels and the
  // interrupt status registers are left out because every failed command moves
  // them, and including those would put the dump straight back into a
  // per-attempt loop.
  //
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
    //
    // The software reset the stack just ran cleared Host Control, so the card
    // detect selection has to go back in before the stack looks at it.
    //
    MofdApplyCardDetect (mSdCardCdPin, MofdCardPresent (mSdCardCdPin));
  }

  if (PhaseType == EdkiiSdMmcInitHostPre) {
    CardPresent = MofdCardPresent (mSdCardCdPin);

    //
    // Refuse the init when the slot is empty, here, before anything is powered
    // or clocked.
    //
    // SdMmcHcInitHost() propagates this status back to its caller without
    // touching the bus, so no card command is ever issued, no TRB is ever
    // built, and the 100 ms enumeration timer retries only this cheap GPIO read
    // instead of issuing CMD0/CMD8 at a slot with nothing in it once every
    // 100 ms for the rest of the run -- which is what produced the endless
    // "TRB failed" flood this detection path exists to stop.
    //
    if (!CardPresent) {
      //
      // This branch runs on every 100 ms enumeration tick for as long as the slot
      // stays empty, so only the first pass is worth a line; reporting each one
      // would replace the TRB flood with a quieter flood.
      //
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

    //
    // Re-arm, so a later removal is reported again after a card has been seen.
    //
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
  // Resolve the card detect line and tell the controller what it says before the
  // device is registered, so the stack's first SdMmcHcCardDetect() reads the real
  // slot state rather than a hardcoded "card present".
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
