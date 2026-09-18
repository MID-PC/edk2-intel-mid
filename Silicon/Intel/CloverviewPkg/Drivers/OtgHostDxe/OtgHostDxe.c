/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Guid/EventGroup.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/NonDiscoverableDevice.h>



// Chipidea vendor block (offsets from the raw BAR base)
#define CT_CI_ID              0x000
// BAR+0x000 reads 0x01100030 (CAPLENGTH=0x30, HCIVERSION=0x0110) - capabilities at BAR+0x000, not 0x100; OpBase = BAR+0x30
#define CT_CI_CAP_OFFSET      0x000  // EHCI CAPLENGTH/HCIVERSION
#define CT_CI_UFOR            0x054  // PHY eye-diagram calibration (unused)
#define CT_CI_UFOT            0x0B4  // PHY/power control (DSDT: clear bit 0)
#define CT_CI_UFOS            0x0F8  // PHY control byte (DSDT: write 0x23)

#define CT_UFOS_INIT          0x23
#define CT_UFOT_PHY_SUSPEND   BIT0

//
// EHCI capability/operational registers, relative to CapBase
//
#define CT_EHCI_CAPLENGTH     0x00
#define CT_EHCI_HCIVERSION    0x02
#define CT_EHCI_HCSPARAMS     0x04

//
// Operational registers, relative to OpBase
//
#define CT_EHCI_USBCMD        0x00
#define CT_EHCI_USBSTS        0x04
#define CT_EHCI_USBMODE       0xC8  // Chipidea LPM map: CM field in bits 1:0

#define CT_USBCMD_RUN         BIT0
#define CT_USBCMD_RESET       BIT1
#define CT_USBSTS_HALTED      BIT12

#define CT_USBMODE_CM_MASK    (BIT0 | BIT1)
#define CT_USBMODE_CM_IDLE    0
#define CT_USBMODE_CM_DEVICE  2
#define CT_USBMODE_CM_HOST    3

#define CT_RESET_TIMEOUT_US   500000

// Reconfigure the USB controller in host mode

// TUSB1211/TUSB1210 ULPI repeater ("PHY") control lines
#define CT_GPIO_GPLR_OFFSET   0x00  // level (read)
#define CT_GPIO_GPDR_OFFSET   0x0C  // direction (1 = output)
#define CT_GPIO_GPSR_OFFSET   0x18  // set-output
#define CT_GPIO_GPCR_OFFSET   0x24  // clear-output
#define CT_GPIO_GAFR_OFFSET   0x54  // alternate function, 2 bits per pin

#define CT_OTG_PHY_RST_LOCAL  78    // clv_gpio_0 @ PcdGpioAuxBase
#define CT_OTG_PHY_CS_LOCAL   75    // clv_gpio_1 @ PcdGpioCoreBase (171 - 96)
#define CT_OTG_VBUS_LOCAL     92    // clv_gpio_0 CHG_OTG @ PcdGpioAuxBase

STATIC
VOID
CtGpioSetOutput (
  IN UINTN    GpioBase,
  IN UINTN    LocalPin,
  IN BOOLEAN  High
  )
{
  UINTN   Register;
  UINT32  Shift;
  UINT32  Value;

  // GAFR 0 = alternate function 0 = GPIO
  Register = GpioBase + CT_GPIO_GAFR_OFFSET + ((LocalPin / 16) * sizeof (UINT32));
  Shift    = (UINT32)((LocalPin % 16) * 2);
  Value    = MmioRead32 (Register) & ~(3U << Shift);
  MmioWrite32 (Register, Value);

  // Level first, then output direction, so the pad never glitches opposite
  MmioWrite32 (
    GpioBase + (High ? CT_GPIO_GPSR_OFFSET : CT_GPIO_GPCR_OFFSET) +
    ((LocalPin / 32) * sizeof (UINT32)),
    (UINT32)BIT0 << (LocalPin % 32)
    );

  Register = GpioBase + CT_GPIO_GPDR_OFFSET + ((LocalPin / 32) * sizeof (UINT32));
  Value    = MmioRead32 (Register) | ((UINT32)BIT0 << (LocalPin % 32));
  MmioWrite32 (Register, Value);
}

/**
  Select the TUSB121x repeater and release it from reset
**/
STATIC
VOID
CtOtgEnablePhyRepeater (
  VOID
  )
{
  UINTN  CsBase;
  UINTN  RstBase;

  CsBase  = (UINTN)FixedPcdGet32 (PcdGpioCoreBase);
  RstBase = (UINTN)FixedPcdGet32 (PcdGpioAuxBase);

  // CS high = ULPI mode
  CtGpioSetOutput (CsBase, CT_OTG_PHY_CS_LOCAL, TRUE);
  MicroSecondDelay (1000);

  // Reset pulse (active low), then release and wait for the ULPI clock
  CtGpioSetOutput (RstBase, CT_OTG_PHY_RST_LOCAL, FALSE);
  MicroSecondDelay (2000);
  CtGpioSetOutput (RstBase, CT_OTG_PHY_RST_LOCAL, TRUE);
  MicroSecondDelay (20000);

  // VBUS 5v boost is PMIC-switched via CHG_OTG (pin 92, aux bank)
  // Without it no peripheral can pull D+ and PORTSC.CCS stays 0 forever
  CtGpioSetOutput (RstBase, CT_OTG_VBUS_LOCAL, TRUE);
  MicroSecondDelay (100000);

  // Read GPLR back so a stuck/ignored write is visible in the log
  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: repeater CS (bank 0x%08x pin %u) GPLR=%u, RST (bank 0x%08x pin %u) GPLR=%u, CHG_OTG (pin %u) GPLR=%u\n",
    CsBase,
    CT_OTG_PHY_CS_LOCAL,
    (MmioRead32 (CsBase + CT_GPIO_GPLR_OFFSET + ((CT_OTG_PHY_CS_LOCAL / 32) * sizeof (UINT32))) >> (CT_OTG_PHY_CS_LOCAL % 32)) & 1,
    RstBase,
    CT_OTG_PHY_RST_LOCAL,
    (MmioRead32 (RstBase + CT_GPIO_GPLR_OFFSET + ((CT_OTG_PHY_RST_LOCAL / 32) * sizeof (UINT32))) >> (CT_OTG_PHY_RST_LOCAL % 32)) & 1,
    CT_OTG_VBUS_LOCAL,
    (MmioRead32 (RstBase + CT_GPIO_GPLR_OFFSET + ((CT_OTG_VBUS_LOCAL / 32) * sizeof (UINT32))) >> (CT_OTG_VBUS_LOCAL % 32)) & 1
    ));
}

//
// South-complex PMU
//
#define CT_PMU_PM_STS            0x00
#define CT_PMU_PM_CMD            0x04
#define CT_PMU_PM_ICS            0x08
#define CT_PMU_PM_SSC            0x20
#define CT_PMU_PM_SSS            0x30
#define CT_PMU_BUSY              BIT8
#define CT_PMU_INTERACTIVE_CMD   0x00002201
#define CT_PMU_LSS_USB_OTG       6
#define CT_PMU_LSS_BITS          2

/**
  Force LSS 6 into D0i0 so the core gets its clocks
**/
STATIC
VOID
CtOtgPmuUngate (
  VOID
  )
{
  UINTN   Pmu;
  UINT32  Sss[4];
  UINT32  Ssc0;
  UINT32  Mask;
  UINT32  Elapsed;

  Pmu = (UINTN)FixedPcdGet32 (PcdPmuBase);
  if (Pmu == 0) {
    return;
  }

  Sss[0] = MmioRead32 (Pmu + CT_PMU_PM_SSS + 0);
  Sss[1] = MmioRead32 (Pmu + CT_PMU_PM_SSS + 4);
  Sss[2] = MmioRead32 (Pmu + CT_PMU_PM_SSS + 8);
  Sss[3] = MmioRead32 (Pmu + CT_PMU_PM_SSS + 12);

  Mask = (UINT32)3 << (CT_PMU_LSS_USB_OTG * CT_PMU_LSS_BITS);

  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: PMU 0x%08x STS 0x%08x SSS %08x %08x %08x %08x (OTG LSS6 state %u)\n",
    Pmu,
    MmioRead32 (Pmu + CT_PMU_PM_STS),
    Sss[0], Sss[1], Sss[2], Sss[3],
    (Sss[0] & Mask) >> (CT_PMU_LSS_USB_OTG * CT_PMU_LSS_BITS)
    ));

  // Keep the current state of everything except LSS 6, which goes to D0i0
  Ssc0 = Sss[0] & ~Mask;
  MmioWrite32 (Pmu + CT_PMU_PM_SSC + 0, Ssc0);
  MmioWrite32 (Pmu + CT_PMU_PM_SSC + 4, Sss[1]);
  MmioWrite32 (Pmu + CT_PMU_PM_SSC + 8, Sss[2]);
  MmioWrite32 (Pmu + CT_PMU_PM_SSC + 12, Sss[3]);

  MmioWrite32 (Pmu + CT_PMU_PM_CMD, CT_PMU_INTERACTIVE_CMD);

  for (Elapsed = 0; Elapsed < 500000; Elapsed += 100) {
    if ((MmioRead32 (Pmu + CT_PMU_PM_STS) & CT_PMU_BUSY) == 0) {
      break;
    }

    MicroSecondDelay (100);
  }

  MicroSecondDelay (10000);

  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: after PMU D0i0 request: STS 0x%08x ICS 0x%08x SSS0 0x%08x (OTG LSS6 state %u)\n",
    MmioRead32 (Pmu + CT_PMU_PM_STS),
    MmioRead32 (Pmu + CT_PMU_PM_ICS),
    MmioRead32 (Pmu + CT_PMU_PM_SSS + 0),
    (MmioRead32 (Pmu + CT_PMU_PM_SSS + 0) & Mask) >> (CT_PMU_LSS_USB_OTG * CT_PMU_LSS_BITS)
    ));
}

STATIC
EFI_STATUS
CtOtgEnterHostMode (
  IN UINTN  BarBase
  )
{
  UINTN   CapBase;
  UINTN   OpBase;
  UINT8   CapLength;
  UINT32  Value;
  UINT32  Elapsed;

  CapBase = BarBase + CT_CI_CAP_OFFSET;

  // Clocks first: nothing works while the island is gated
  CtOtgPmuUngate ();

  // The repeater must be alive before the core is touched
  CtOtgEnablePhyRepeater ();

  // PHY bring-up
  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: UFOT before force-device clear 0x%08x (bit23 force-device=%u bit22 pwrdn=%u bit0 susp=%u)\n",
    MmioRead32 (BarBase + CT_CI_UFOT),
    (MmioRead32 (BarBase + CT_CI_UFOT) >> 23) & 1,
    (MmioRead32 (BarBase + CT_CI_UFOT) >> 22) & 1,
    MmioRead32 (BarBase + CT_CI_UFOT) & 1
    ));

  MmioAnd32 (BarBase + CT_CI_UFOT, 0xFF3FFFFEU);
  MicroSecondDelay (10000);

  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: UFOT after force-device clear 0x%08x (bit23=%u)\n",
    MmioRead32 (BarBase + CT_CI_UFOT),
    (MmioRead32 (BarBase + CT_CI_UFOT) >> 23) & 1
    ));
  MmioWrite8 (BarBase + CT_CI_UFOS, CT_UFOS_INIT);
  MmioAnd32 (BarBase + CT_CI_UFOT, 0xFFFFFFFEU);
  Value = (MmioRead32 (BarBase + CT_CI_UFOR) & 0xFFC0FFFFU) | 0x000B0000U;
  MmioWrite32 (BarBase + CT_CI_UFOR, Value);
  MicroSecondDelay (20000);

  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: PHY per DSDT: UFOT 0x%08x UFOS 0x%02x UFOR 0x%08x\n",
    MmioRead32 (BarBase + CT_CI_UFOT),
    MmioRead8 (BarBase + CT_CI_UFOS),
    MmioRead32 (BarBase + CT_CI_UFOR)
    ));

  // ULPI host prep
  {
    UINTN                UlpiOp;
    UINT32               UlpiIdx;
    UINT32               UlpiElapsed;
    STATIC CONST UINT32  CtUlpiHostInit[][2] = {
      { 0x06, 0x20 },
      { 0x05, 0x40 },
      { 0x0B, 0x66 }
    };

    UlpiOp = BarBase + MmioRead8 (BarBase);  // OpBase = BAR + CAPLENGTH

    for (UlpiIdx = 0; UlpiIdx < ARRAY_SIZE (CtUlpiHostInit); UlpiIdx++) {
      // Clear PHCD (PORTSC bit 23)
      MmioAnd32 (UlpiOp + 0x44, ~(UINT32)(BIT23 | BIT1));

      MmioWrite32 (UlpiOp + 0x30, BIT31);   // ULPIWU
      for (UlpiElapsed = 0; UlpiElapsed < 100000; UlpiElapsed += 100) {
        if ((MmioRead32 (UlpiOp + 0x30) & BIT31) == 0) {
          break;
        }

        MicroSecondDelay (100);
      }

      MmioWrite32 (
        UlpiOp + 0x30,
        BIT30 | BIT29 | (CtUlpiHostInit[UlpiIdx][0] << 16) | (CtUlpiHostInit[UlpiIdx][1] & 0xFF)
        );
      for (UlpiElapsed = 0; UlpiElapsed < 100000; UlpiElapsed += 100) {
        if ((MmioRead32 (UlpiOp + 0x30) & BIT30) == 0) {
          break;
        }

        MicroSecondDelay (100);
      }

      DEBUG ((
        DEBUG_ERROR,
        "OtgHostDxe: pre-reset ULPI write 0x%02x = 0x%02x (%a) ULPIVP 0x%08x\n",
        CtUlpiHostInit[UlpiIdx][0],
        CtUlpiHostInit[UlpiIdx][1],
        ((MmioRead32 (UlpiOp + 0x30) & BIT30) == 0) ? "done" : "TIMEOUT",
        MmioRead32 (UlpiOp + 0x30)
        ));
    }

    MicroSecondDelay (50000);
  }

  CapLength = MmioRead8 (CapBase + CT_EHCI_CAPLENGTH);
  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: ID 0x%08x CAPLENGTH 0x%02x HCIVERSION 0x%04x HCSPARAMS 0x%08x\n",
    MmioRead32 (BarBase + CT_CI_ID),
    CapLength,
    MmioRead16 (CapBase + CT_EHCI_HCIVERSION),
    MmioRead32 (CapBase + CT_EHCI_HCSPARAMS)
    ));

  if ((CapLength < 0x10) || (CapLength > 0x40)) {
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: implausible CAPLENGTH 0x%02x, aborting\n", CapLength));
    return EFI_DEVICE_ERROR;
  }

  OpBase = CapBase + CapLength;

  // USBMODE is write-once after reset. Stop it, then reset it
  MmioAnd32 (OpBase + CT_EHCI_USBCMD, ~(UINT32)CT_USBCMD_RUN);
  for (Elapsed = 0; Elapsed < CT_RESET_TIMEOUT_US; Elapsed += 100) {
    if ((MmioRead32 (OpBase + CT_EHCI_USBSTS) & CT_USBSTS_HALTED) != 0) {
      break;
    }

    MicroSecondDelay (100);
  }

  MmioOr32 (OpBase + CT_EHCI_USBCMD, CT_USBCMD_RESET);
  for (Elapsed = 0; Elapsed < CT_RESET_TIMEOUT_US; Elapsed += 100) {
    if ((MmioRead32 (OpBase + CT_EHCI_USBCMD) & CT_USBCMD_RESET) == 0) {
      break;
    }

    MicroSecondDelay (100);
  }

  if ((MmioRead32 (OpBase + CT_EHCI_USBCMD) & CT_USBCMD_RESET) != 0) {
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: controller reset timed out\n"));
    return EFI_TIMEOUT;
  }

  // Dump the op window
  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: OpBase 0x%08x USBCMD 0x%08x USBSTS 0x%08x +0x60 0x%08x +0x64 0x%08x +0x68 0x%08x +0x6c 0x%08x BAR+0x1a8 0x%08x\n",
    OpBase,
    MmioRead32 (OpBase + CT_EHCI_USBCMD),
    MmioRead32 (OpBase + CT_EHCI_USBSTS),
    MmioRead32 (OpBase + 0x60),
    MmioRead32 (OpBase + 0x64),
    MmioRead32 (OpBase + 0x68),
    MmioRead32 (OpBase + 0x6C),
    MmioRead32 (BarBase + 0x1A8)
    ));

  // Program USBMODE.CM = host, retrying
  // Fallback: the BAR+0x1A8 alias on shifted steppings.
  for (Elapsed = 0; Elapsed < 10; Elapsed++) {
    Value = MmioRead32 (OpBase + CT_EHCI_USBMODE);
    MmioWrite32 (
      OpBase + CT_EHCI_USBMODE,
      (Value & ~(UINT32)CT_USBMODE_CM_MASK) | CT_USBMODE_CM_HOST
      );
    MicroSecondDelay (1000);
    if ((MmioRead32 (OpBase + CT_EHCI_USBMODE) & CT_USBMODE_CM_MASK) == CT_USBMODE_CM_HOST) {
      break;
    }

    // Shifted steppings put the op block at BAR+0x40 (USBMODE at 0xA8);
    // BAR+0x1A8 read back undecoded (0) on this board, so try 0xA8
    Value = MmioRead32 (BarBase + 0xA8);
    MmioWrite32 (
      BarBase + 0xA8,
      (Value & ~(UINT32)CT_USBMODE_CM_MASK) | CT_USBMODE_CM_HOST
      );
    MicroSecondDelay (1000);
    if ((MmioRead32 (BarBase + 0xA8) & CT_USBMODE_CM_MASK) == CT_USBMODE_CM_HOST) {
      break;
    }
    MicroSecondDelay (1000);
    if ((MmioRead32 (OpBase + CT_EHCI_USBMODE) & CT_USBMODE_CM_MASK) == CT_USBMODE_CM_HOST) {
      break;
    }
  }

  Value = MmioRead32 (OpBase + CT_EHCI_USBMODE);
  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: after %u USBMODE attempt(s) USBMODE 0x%08x (CM=%u) BAR+0x1a8 0x%08x USBSTS 0x%08x\n",
    Elapsed + 1,
    Value,
    Value & CT_USBMODE_CM_MASK,
    MmioRead32 (BarBase + 0x1A8),
    MmioRead32 (OpBase + CT_EHCI_USBSTS)
    ));

  if ((Value & CT_USBMODE_CM_MASK) != CT_USBMODE_CM_HOST) {
    UINT32  Attempt;

    // Some steppings need an 8-bit access or a write while RST is asserted
    MmioWrite8 (OpBase + CT_EHCI_USBMODE, CT_USBMODE_CM_HOST);
    MicroSecondDelay (1000);

    MmioOr32 (OpBase + CT_EHCI_USBCMD, CT_USBCMD_RESET);
    for (Attempt = 0; Attempt < 100; Attempt++) {
      MmioWrite32 (OpBase + CT_EHCI_USBMODE, CT_USBMODE_CM_HOST);
      MmioWrite8 (OpBase + CT_EHCI_USBMODE, CT_USBMODE_CM_HOST);
      if ((MmioRead32 (OpBase + CT_EHCI_USBCMD) & CT_USBCMD_RESET) == 0) {
        break;
      }

      MicroSecondDelay (100);
    }

    MmioWrite32 (OpBase + CT_EHCI_USBMODE, CT_USBMODE_CM_HOST);
    MicroSecondDelay (1000);

    if ((MmioRead32 (OpBase + CT_EHCI_USBMODE) & CT_USBMODE_CM_MASK) == CT_USBMODE_CM_HOST) {
      DEBUG ((DEBUG_ERROR, "OtgHostDxe: USBMODE latched host mode during reset\n"));
      return EFI_SUCCESS;
    }

    // Some steppings are strapped to host with CM reading 0; EhciDxe only
    // needs the cap/op registers. Continue rather than abort
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: USBMODE did not latch host mode - continuing anyway\n"));
    return EFI_SUCCESS;
  }

  return EFI_SUCCESS;
}

// Port/OTG status register logging (relative to OpBase)
#define CT_EHCI_PORTSC        0x44
#define CT_EHCI_OTGSC         0xC4  // Chipidea LPM map: OP_OTGSC (BAR+0xF4)
#define CT_EHCI_ULPIVP        0x30  // ULPI viewport (Chipidea)

#define CT_PORTSC_CCS         BIT0  // current connect status
#define CT_PORTSC_CSC         BIT1  // connect status change (RWC)
#define CT_PORTSC_PE          BIT2
#define CT_PORTSC_PP          BIT12

#define CT_ULPIVP_RUN         BIT30
#define CT_ULPIVP_RW          BIT29  // 0 = read, 1 = write

STATIC UINTN      mOtgOpBase   = 0;
STATIC EFI_EVENT  mOtgPollEvent = NULL;
STATIC EFI_HANDLE mOtgNdHandle = NULL;

/**
  Read one ULPI register via the viewport; 0xFFFF on timeout means no ULPI
  clock (repeater/CS-RST problem, not the core)
**/
STATIC
UINT32
CtUlpiRead (
  IN UINTN  OpBase,
  IN UINT8  Addr
  )
{
  UINT32  Value;
  UINT32  Elapsed;

  // Read data comes back in bits 15:8 (ULPIDATRD); and the viewport
  // only completes while the PHY clocks. Clear PHCD, then wake with ULPIWU
  MmioAnd32 (OpBase + CT_EHCI_PORTSC, ~(UINT32)(BIT23 | CT_PORTSC_CSC));
  MmioWrite32 (OpBase + CT_EHCI_ULPIVP, BIT31);
  for (Elapsed = 0; Elapsed < 100000; Elapsed += 100) {
    if ((MmioRead32 (OpBase + CT_EHCI_ULPIVP) & BIT31) == 0) {
      break;
    }

    MicroSecondDelay (100);
  }

  MmioWrite32 (OpBase + CT_EHCI_ULPIVP, CT_ULPIVP_RUN | ((UINT32)Addr << 16));
  for (Elapsed = 0; Elapsed < 100000; Elapsed += 100) {
    Value = MmioRead32 (OpBase + CT_EHCI_ULPIVP);
    if ((Value & CT_ULPIVP_RUN) == 0) {
      return (Value >> 8) & 0xFF;
    }

    MicroSecondDelay (100);
  }

  return 0xFFFF;
}

/**
  Periodic diagnostics poll.
  Stays read-only so EhciDxe and UsbBusDxe still see the PORTSC change bits
**/
STATIC
VOID
EFIAPI
CtOtgPollStatus (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT32  Portsc;;

  if (mOtgOpBase == 0) {
    return;
  }

  // Chipidea TX underrun handling
  {
    STATIC BOOLEAN  mHseFixed = FALSE;
    UINT32          Sts;

    Sts = MmioRead32 (mOtgOpBase + CT_EHCI_USBSTS);
    if (!mHseFixed && ((Sts & BIT4) != 0)) {
      mHseFixed = TRUE;

      MmioWrite32 (mOtgOpBase + CT_EHCI_USBSTS, BIT4);   // write-1-to-clear HSE
      // Restore what EhciDxe's HCRESET wipes, per downstream kernel ehci_reset()
      MmioWrite32 (mOtgOpBase + CT_EHCI_USBMODE, 0x23);  // USBMODE_EX: HC | VBPS
      MmioWrite32 (mOtgOpBase + 0x24, 0x00080000);       // TXFILLTUNING = TXFIFO_DEFAULT (8 << 16)
      MmioWrite32 (mOtgOpBase + 0x20, 0x00001010);       // BURSTSIZE
      MmioAnd32 (mOtgOpBase + 0x84, ~(UINT32)(BIT0 | BIT22)); // HOSTPC1: clear auto-LPM + PHCD
      MmioOr32 (mOtgOpBase + CT_EHCI_USBCMD, CT_USBCMD_RUN);

      DEBUG ((
        DEBUG_ERROR,
        "OtgHostDxe: HSE fix (USBSTS was 0x%08x): TXFILL 0x%08x BURST 0x%08x USBCMD 0x%08x USBSTS 0x%08x PERIODIC 0x%08x ASYNC 0x%08x\n",
        Sts,
        MmioRead32 (mOtgOpBase + 0x24),
        MmioRead32 (mOtgOpBase + 0x20),
        MmioRead32 (mOtgOpBase + CT_EHCI_USBCMD),
        MmioRead32 (mOtgOpBase + CT_EHCI_USBSTS),
        MmioRead32 (mOtgOpBase + 0x14),
        MmioRead32 (mOtgOpBase + 0x18)
        ));
    }
  }

  Portsc = MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC);

  // Log the whole HC run state on change
  {
    STATIC UINT32  mLastHcState = 0xFFFFFFFF;
    UINT32         Usbcmd;
    UINT32         Usbsts;
    UINT32         Usbmode;
    UINT32         State;

    Usbcmd  = MmioRead32 (mOtgOpBase + CT_EHCI_USBCMD);
    Usbsts  = MmioRead32 (mOtgOpBase + CT_EHCI_USBSTS);
    Usbmode = MmioRead32 (mOtgOpBase + CT_EHCI_USBMODE);
    State   = (Usbcmd & 0x3) | ((Usbsts & BIT12) >> 10) | ((Usbmode & 0x3) << 4);

    // Keep printing every 5s
    if (State != mLastHcState) {
      mLastHcState = State;

      DEBUG ((
        DEBUG_ERROR,
        "OtgHostDxe: HC state USBCMD 0x%08x USBSTS 0x%08x USBMODE 0x%08x FRINDEX 0x%08x PERIODIC 0x%08x ASYNC 0x%08x CONFIGFLAG 0x%08x\n",
        Usbcmd,
        Usbsts,
        Usbmode,
        MmioRead32 (mOtgOpBase + 0x0C),
        MmioRead32 (mOtgOpBase + 0x14),
        MmioRead32 (mOtgOpBase + 0x18),
        MmioRead32 (mOtgOpBase + 0x40)
        ));
    }
  }

  // EhciDxe's HCRESET at BDS clears USBMODE.CM and the ULPI A-device settings
  // Re-assert when CM != 3
  {
    STATIC CONST UINT32  ReInit[][2] = {
      { 0x06, 0x20 },   // Function Control clear: leave PHY reset
      { 0x05, 0x40 },   // Function Control set:   SuspendM = 1
      { 0x0B, 0x66 }    // OTG Control set: DrvVbusExternal|DrvVbus|DmPd|DpPd
    };
    UINT32  Mode;
    UINT32  Idx;
    UINT32  Elapsed;

    Mode = MmioRead32 (mOtgOpBase + CT_EHCI_USBMODE);
    if ((Mode & CT_USBMODE_CM_MASK) != CT_USBMODE_CM_HOST) {
      STATIC BOOLEAN  mHostModeRestored = FALSE;

      if (mHostModeRestored) {
        return;
      }

      mHostModeRestored = TRUE;
      MmioWrite32 (
        mOtgOpBase + CT_EHCI_USBMODE,
        (Mode & ~(UINT32)CT_USBMODE_CM_MASK) | CT_USBMODE_CM_HOST
        );
      MmioWrite8 (mOtgOpBase + CT_EHCI_USBMODE, CT_USBMODE_CM_HOST);

      for (Idx = 0; Idx < ARRAY_SIZE (ReInit); Idx++) {
        MmioAnd32 (mOtgOpBase + CT_EHCI_PORTSC, ~(UINT32)(BIT23 | CT_PORTSC_CSC));
        MmioWrite32 (
          mOtgOpBase + CT_EHCI_ULPIVP,
          CT_ULPIVP_RUN | CT_ULPIVP_RW | (ReInit[Idx][0] << 16) | (ReInit[Idx][1] & 0xFF)
          );
        for (Elapsed = 0; Elapsed < 100000; Elapsed += 100) {
          if ((MmioRead32 (mOtgOpBase + CT_EHCI_ULPIVP) & CT_ULPIVP_RUN) == 0) {
            break;
          }

          MicroSecondDelay (100);
        }
      }

      Portsc = MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC);
      MmioWrite32 (
        mOtgOpBase + CT_EHCI_PORTSC,
        (Portsc & ~(UINT32)CT_PORTSC_CSC) | CT_PORTSC_PP
        );

      DEBUG ((
        DEBUG_ERROR,
        "OtgHostDxe: host mode lost (USBMODE 0x%08x) - re-asserted: USBMODE 0x%08x PORTSC 0x%08x OTGSC 0x%08x\n",
        Mode,
        MmioRead32 (mOtgOpBase + CT_EHCI_USBMODE),
        MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC),
        MmioRead32 (mOtgOpBase + CT_EHCI_OTGSC)
        ));

      Portsc = MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC);
    }
  }
}

STATIC EFI_EVENT  mOtgExitBootServicesEvent = NULL;

/**
  ExitBootServices: stop firmware polling and keep the pads Windows needs.
  Windows' usbehci.sys only programs UFOR/UFOT/UFOS - no driver for the
  TUSB1211 CS/RST or CHG_OTG VBUS lines - so re-assert them and clear HOSTPC1's
  PHY low-power/auto-suspend bits for the hand-off.
**/
STATIC
VOID
EFIAPI
CtOtgExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT32  HostPc;

  if (mOtgPollEvent != NULL) {
    gBS->SetTimer (mOtgPollEvent, TimerCancel, 0);
  }

  CtOtgEnablePhyRepeater ();

  // (HOSTPC1) Clear ASUS (BIT0 auto low-power) and PHCD (BIT22) for the hand-off.
  HostPc = MmioRead32 (mOtgOpBase + 0x84);
  MmioWrite32 (mOtgOpBase + 0x84, HostPc & ~(BIT0 | BIT22));
}

EFI_STATUS
EFIAPI
OtgHostDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;
  UINTN       Base;
  UINTN       Size;
  UINT8       CapLength;
  UINT32      Portsc;

  Base = (UINTN)FixedPcdGet32 (PcdOtgHostBase);
  Size = (UINTN)FixedPcdGet32 (PcdOtgHostSize);

  DEBUG ((DEBUG_ERROR, "OtgHostDxe: ENTRY reached, OTG0 at 0x%08x + 0x%x\n", Base, Size));

  Status = CtOtgEnterHostMode (Base);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "OtgHostDxe: host-mode bring-up FAILED - %r (ID=0x%08x CAPLEN=0x%02x)\n",
      Status,
      MmioRead32 (Base + CT_CI_ID),
      MmioRead8 (Base + CT_CI_CAP_OFFSET + CT_EHCI_CAPLENGTH)
      ));

    return Status;
  }

  // Remember OpBase for the status poller
  CapLength   = MmioRead8 (Base + CT_CI_CAP_OFFSET + CT_EHCI_CAPLENGTH);
  mOtgOpBase  = Base + CT_CI_CAP_OFFSET + CapLength;

  // Retry up to ~500 ms and latch the first valid repeater identity
  {
    STATIC UINT32  mUlpiVendor  = 0xFFFFFFFF;
    STATIC UINT32  mUlpiProduct = 0xFFFFFFFF;
    UINT32         Attempt;
    UINT32         VidLo;
    UINT32         VidHi;
    UINT32         PidLo;
    UINT32         PidHi;

    for (Attempt = 0; (Attempt < 50) && (mUlpiVendor == 0xFFFFFFFF); Attempt++) {
      VidLo = CtUlpiRead (mOtgOpBase, 0x00);
      VidHi = CtUlpiRead (mOtgOpBase, 0x01);
      PidLo = CtUlpiRead (mOtgOpBase, 0x02);
      PidHi = CtUlpiRead (mOtgOpBase, 0x03);

      if ((VidLo <= 0xFF) && (VidHi <= 0xFF) &&
          (PidLo <= 0xFF) && (PidHi <= 0xFF) &&
          !((VidLo == 0xFF) && (VidHi == 0xFF)) &&
          !((VidLo == 0x00) && (VidHi == 0x00))) {
        mUlpiVendor  = (VidHi << 8) | VidLo;
        mUlpiProduct = (PidHi << 8) | PidLo;
        break;
      }

      MicroSecondDelay (10000);
    }

    if (mUlpiVendor != 0xFFFFFFFF) {
      DEBUG ((
        DEBUG_ERROR,
        "OtgHostDxe: ULPI vendor 0x%04x product 0x%04x LATCHED after %u attempt(s) (TUSB121x = 0x0451/0x150x)\n",
        mUlpiVendor,
        mUlpiProduct,
        Attempt + 1
        ));
    } else {
      DEBUG ((
        DEBUG_ERROR,
        "OtgHostDxe: ULPI viewport never answered in %u attempts - no ULPI clock from the repeater\n",
        Attempt
        ));
    }
  }

  {
    STATIC CONST UINT32  UlpiInit[][2] = {
      { 0x06, 0x20 },   // Function Control clear: leave PHY reset
      { 0x05, 0x40 },   // Function Control set:   SuspendM = 1
      { 0x0B, 0x66 },   // OTG Control set: DrvVbusExternal|DrvVbus|DmPd|DpPd
    };
    UINT32  Idx;
    UINT32  Elapsed;

    for (Idx = 0; Idx < ARRAY_SIZE (UlpiInit); Idx++) {
      MmioAnd32 (mOtgOpBase + CT_EHCI_PORTSC, ~(UINT32)(BIT23 | CT_PORTSC_CSC));
      MmioWrite32 (
        mOtgOpBase + CT_EHCI_ULPIVP,
        CT_ULPIVP_RUN | CT_ULPIVP_RW | (UlpiInit[Idx][0] << 16) | (UlpiInit[Idx][1] & 0xFF)
        );
      for (Elapsed = 0; Elapsed < 100000; Elapsed += 100) {
        if ((MmioRead32 (mOtgOpBase + CT_EHCI_ULPIVP) & CT_ULPIVP_RUN) == 0) {
          break;
        }

        MicroSecondDelay (100);
      }

      DEBUG ((
        DEBUG_ERROR,
        "OtgHostDxe: ULPI write 0x%02x = 0x%02x (%a)\n",
        UlpiInit[Idx][0],
        UlpiInit[Idx][1],
        ((MmioRead32 (mOtgOpBase + CT_EHCI_ULPIVP) & CT_ULPIVP_RUN) == 0) ? "done" : "TIMEOUT"
        ));
    }

    MicroSecondDelay (100000);

    DEBUG ((
      DEBUG_ERROR,
      "OtgHostDxe: ULPI FuncCtrl 0x%02x IfcCtrl 0x%02x OtgCtrl 0x%02x IntStatus 0x%02x Debug/LineState 0x%02x\n",
      CtUlpiRead (mOtgOpBase, 0x04),
      CtUlpiRead (mOtgOpBase, 0x07),
      CtUlpiRead (mOtgOpBase, 0x0A),
      CtUlpiRead (mOtgOpBase, 0x13),
      CtUlpiRead (mOtgOpBase, 0x15)
      ));

  }

  // Force port power-on
  Portsc = MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC);
  MmioWrite32 (mOtgOpBase + CT_EHCI_PORTSC, (Portsc & ~(UINT32)CT_PORTSC_CSC) | CT_PORTSC_PP);
  MicroSecondDelay (100000);

  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: initial PORTSC 0x%08x OTGSC 0x%08x\n",
    MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC),
    MmioRead32 (mOtgOpBase + CT_EHCI_OTGSC)
    ));

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  CtOtgPollStatus,
                  NULL,
                  &mOtgPollEvent
                  );
  if (!EFI_ERROR (Status)) {
    gBS->SetTimer (mOtgPollEvent, TimerPeriodic, 2500000);
  } else {
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: status poll timer failed - %r\n", Status));
  }

  // Re-assert the pads and clear PHY low-power bits at ExitBootServices;
  // cancel the poller first so no firmware MMIO happens once the OS owns the block.
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  CtOtgExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mOtgExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: ExitBootServices notify failed - %r\n", Status));
  }

  // Hand EhciDxe a window starting at the EHCI capability registers
  Handle = NULL;
  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeEhci,
             // DMA: NonCoherent (Cloverview OTG master is not cache-coherent with the CPU
             NonDiscoverableDeviceDmaTypeCoherent,
             NULL,
             &Handle,
             1,
             Base + CT_CI_CAP_OFFSET,
             Size - CT_CI_CAP_OFFSET
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: registration failed - %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_ERROR, "OtgHostDxe: EHCI host registered at 0x%08x, handle %p\n", Base + CT_CI_CAP_OFFSET, Handle));
  mOtgNdHandle = Handle;

  Status = EFI_SUCCESS;
  DEBUG ((DEBUG_ERROR, "OtgHostDxe: registration done, leaving connect to BDS ConnectAll\n"));
  MicroSecondDelay (500000);
  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: after connect USBCMD 0x%08x USBSTS 0x%08x USBMODE 0x%08x FRINDEX 0x%08x PERIODIC 0x%08x ASYNC 0x%08x CONFIGFLAG 0x%08x PORTSC 0x%08x\n",
    MmioRead32 (mOtgOpBase + CT_EHCI_USBCMD),
    MmioRead32 (mOtgOpBase + CT_EHCI_USBSTS),
    MmioRead32 (mOtgOpBase + CT_EHCI_USBMODE),
    MmioRead32 (mOtgOpBase + 0x0C),
    MmioRead32 (mOtgOpBase + 0x14),
    MmioRead32 (mOtgOpBase + 0x18),
    MmioRead32 (mOtgOpBase + 0x40),
    MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC)
    ));

  return EFI_SUCCESS;
}
