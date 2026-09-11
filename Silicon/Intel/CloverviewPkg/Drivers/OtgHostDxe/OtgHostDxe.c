/** @file
  Brings up the Clover Trail+ (Atom Z25xx) USB OTG controller (OTG0,
  0000:00:02.3, "penwell_otg" @ 0xFFA60000) in plain EHCI host mode and
  registers it as a non-discoverable EHCI device so MdeModulePkg's generic
  EhciDxe/UsbBusDxe stack can drive it.

  Hardware notes
  --------------
  * The block is a Chipidea/ARC "HDRC" dual-role core, which is EHCI-compliant
    in host mode but places the EHCI capability registers at BAR offset 0x100
    instead of 0x000 (offsets 0x000..0x0FF hold the ID/HWPARAMS block). The
    generic EhciDxe reads CAPLENGTH at offset 0 of the MMIO window it is given,
    so the device is registered at PcdOtgHostBase + CT_CI_CAP_OFFSET rather
    than at the raw BAR base.
  * The core comes out of reset in device (peripheral) mode. USBMODE.CM must be
    programmed to 3 (host) *while the controller is halted*, and on this core
    USBMODE is only writable once after each controller reset - hence the
    RESET -> wait -> USBMODE sequence below. USBMODE lives in the operational
    register block at OpBase + 0x68 (OpBase = CapBase + CAPLENGTH).
  * The 2760 DSDT OTG0._DSM (UUID ce2ee385-...) does exactly two things that
    matter to a host-only bring-up: it writes 0x23 to the byte at BAR + 0xF8
    (PHY/vendor control: enable the UTMI clock and take the PHY out of low
    power) and clears bit 0 of the DWORD at BAR + 0xB4 (UFOT - leave software
    controlled PHY suspend). Both are replicated here; the stepping dependent
    UFOR (BAR + 0x54) eye-diagram tweak is deliberately not applied because it
    is calibration only and the stock bootloader already programmed the PHY.
  * VBUS: the single-port host block on these phones feeds the modem and the
    OTG port's VBUS is switched by the PMIC. The port is left with whatever
    VBUS state the bootloader established (as on the Z2760, which enumerated
    without any OTG/SCU driver); no SCU IPC traffic is issued from here so the
    watchdog keeper in PlatformRuntimeDxe is not perturbed.

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



//
// Chipidea vendor block (offsets from the raw BAR base).
//
#define CT_CI_ID              0x000
//
// Measured on the A502CG: the DWORD at BAR + 0x000 reads 0x01100030, i.e.
// CAPLENGTH = 0x30 (byte 0) and HCIVERSION = 0x0110 (bytes 2:3). So this core
// exposes the EHCI capability registers at BAR offset 0x000 (not 0x100, which
// held 0x01 - a byte of the vendor block) and there is no Chipidea ID register
// at offset 0. OpBase is therefore BAR + 0x30.
//
#define CT_CI_CAP_OFFSET      0x000  // EHCI CAPLENGTH/HCIVERSION live here.
#define CT_CI_UFOR            0x054  // PHY eye-diagram calibration (unused).
#define CT_CI_UFOT            0x0B4  // PHY/power control (DSDT: clear bit 0).
#define CT_CI_UFOS            0x0F8  // PHY control byte (DSDT: write 0x23).

#define CT_UFOS_INIT          0x23
#define CT_UFOT_PHY_SUSPEND   BIT0

//
// EHCI capability/operational registers, relative to CapBase.
//
#define CT_EHCI_CAPLENGTH     0x00
#define CT_EHCI_HCIVERSION    0x02
#define CT_EHCI_HCSPARAMS     0x04

//
// Operational registers, relative to OpBase.
//
#define CT_EHCI_USBCMD        0x00
#define CT_EHCI_USBSTS        0x04
//
// USBMODE is at OpBase + 0x68 on every Chipidea/ARC core (the downstream
// penwell_otg header agrees: CI_USBCMD 0x140, CI_USBMODE 0x1A8, delta 0x68).
// The previous 0xC8 was wrong and actively harmful: with OpBase = BAR + 0x30 it
// aliased BAR + 0xF8, i.e. the UFOS PHY control byte, so every "USBMODE" write
// clobbered the PHY setup instead of switching the core to host mode.
//
// The BAR+0x20/0x24 dump (0x00000001 / 0x000001A8) is DCIVERSION = 1 and
// DCCPARAMS = 0x1A8 (DEN = 8 endpoints, DC = 1, HC = 1), which confirms this is
// a genuine dual-role Chipidea core whose register file starts at BAR + 0x000,
// so USBMODE is at BAR + 0x30 + 0x68 = BAR + 0x98.
//
//
// CORRECTION (from the CM-SCAN result plus the observed register values):
// this core uses the Chipidea *LPM* register map, not the non-LPM one. In the
// LPM layout (Linux drivers/usb/chipidea ci_regs_lpm[]) the operational
// offsets are OP_PORTSC 0x44, OP_DEVLC 0x84, OP_OTGSC 0xC4, OP_USBMODE 0xC8 -
// i.e. shifted by 0x60 from the non-LPM 0x64/0x68 pair this driver was using.
// Everything observed on the device agrees:
//   * op+0xC4 (BAR+0xF4) was the ONLY register in the scanned window that
//     accepted a write, and it read 0x003F0E20 - a plausible OTGSC (ID/session
//     status bits), not a reserved dword.
//   * op+0xC8 is BAR+0xF8, the byte the 2760 OTG0 _DSM writes 0x23 to and which
//     reads back 0x23. 0x23 = CM(1:0) = 3 (host) | SDIS(bit5) - i.e. "UFOS" in
//     the ASL is really USBMODE, and host mode does latch there.
//   * op+0x68/0x64 (BAR+0x98/0x94) always read 0 and silently drop writes,
//     because in the LPM map those offsets are reserved.
//   * op+0x84 (BAR+0xB4, the ASL's "UFOT", 0x46000202) is DEVLC, whose PTS/PFSC
//     fields explain the PHY-ish semantics attributed to it.
// So point USBMODE at op+0xC8 (the CM-SCAN loop stopped at 0xC8 and never
// tested it).
//
#define CT_EHCI_USBMODE       0xC8  // Chipidea LPM map: CM field in bits 1:0.

#define CT_USBCMD_RUN         BIT0
#define CT_USBCMD_RESET       BIT1
#define CT_USBSTS_HALTED      BIT12

#define CT_USBMODE_CM_MASK    (BIT0 | BIT1)
#define CT_USBMODE_CM_IDLE    0
#define CT_USBMODE_CM_DEVICE  2
#define CT_USBMODE_CM_HOST    3

#define CT_RESET_TIMEOUT_US   500000

/**
  Take the core out of device mode and leave it halted in host mode, which is
  the state EhciDxe expects to find the controller in.

  @param  BarBase  Raw MMIO base of the OTG block (PcdOtgHostBase).

  @retval EFI_SUCCESS   USBMODE reads back as host mode.
  @retval EFI_TIMEOUT   The controller did not complete its reset.
  @retval EFI_DEVICE_ERROR  USBMODE refused to latch host mode.
**/
//
// TUSB1211/TUSB1210 ULPI repeater ("PHY") control lines. The downstream
// kernel's platform_usb_otg.c gets these for PCI_DEVICE_ID_INTEL_CLV_OTG from
// the SFI GPIO table by name; sfi-tables/GPIO on the A502CG gives:
//   clv_gpio_0 pin  78 (0x4E) usb_otg_phy_rst -> bank 0 @ 0xFF119000, local 78
//   clv_gpio_1 pin 171 (0xAB) usb_otg_phy_cs  -> bank 1 @ 0xFF13F000, local 75
// (clv_gpio_1 has global base 96, which is the same bank the SD mux uses.)
//
// The repeater must be selected (CS asserted high = ULPI mode, not carkit /
// low-power bypass) and released from reset (RST high) before the Chipidea
// core can talk ULPI to it; otherwise the core sees no ULPI clock, CAPLENGTH
// reads back plausibly but no port ever reports a connect. Both lines are
// plain GPIO outputs (alternate function 0).
//
#define CT_GPIO_GPLR_OFFSET   0x00  // level (read)
#define CT_GPIO_GPDR_OFFSET   0x0C  // direction (1 = output)
#define CT_GPIO_GPSR_OFFSET   0x18  // set-output
#define CT_GPIO_GPCR_OFFSET   0x24  // clear-output
#define CT_GPIO_GAFR_OFFSET   0x54  // alternate function, 2 bits per pin

#define CT_OTG_PHY_RST_LOCAL  78    // clv_gpio_0 @ PcdGpioAuxBase
#define CT_OTG_PHY_CS_LOCAL   75    // clv_gpio_1 @ PcdGpioCoreBase (171 - 96)
#define CT_OTG_VBUS_LOCAL     92    // clv_gpio_0 'CHG_OTG' @ PcdGpioAuxBase

/**
  Drive a Langwell GPIO as a plain (alternate function 0) output.

  @param  GpioBase  Base of the GPIO bank.
  @param  LocalPin  Pin number within the bank.
  @param  High      TRUE to drive high, FALSE to drive low.
**/
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

  //
  // Alternate function 0 = GPIO.
  //
  Register = GpioBase + CT_GPIO_GAFR_OFFSET + ((LocalPin / 16) * sizeof (UINT32));
  Shift    = (UINT32)((LocalPin % 16) * 2);
  Value    = MmioRead32 (Register) & ~(3U << Shift);
  MmioWrite32 (Register, Value);

  //
  // Set the level first, then switch the pad to an output, so the repeater
  // never sees a short glitch of the opposite polarity.
  //
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
  Select the TUSB121x ULPI repeater and take it out of reset.

  Mirrors what penwell_otg does with pdata->gpio_cs / pdata->gpio_reset on
  Clover Trail+: assert CS (ULPI mode), then pulse RST low and release it,
  leaving time for the repeater's 60 MHz ULPI clock to come up before the
  Chipidea core is reset.
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

  //
  // CS high: put the repeater in ULPI mode rather than the default bypass.
  //
  CtGpioSetOutput (CsBase, CT_OTG_PHY_CS_LOCAL, TRUE);
  MicroSecondDelay (1000);

  //
  // Reset pulse (active low), then release and wait for the ULPI clock.
  //
  CtGpioSetOutput (RstBase, CT_OTG_PHY_RST_LOCAL, FALSE);
  MicroSecondDelay (2000);
  CtGpioSetOutput (RstBase, CT_OTG_PHY_RST_LOCAL, TRUE);
  MicroSecondDelay (20000);

  //
  // VBUS: on this board the OTG port's 5 V boost is switched by the PMIC
  // through the SoC line named CHG_OTG in sfi-tables/GPIO (clv_gpio_0 pin 92,
  // i.e. local pin 92 of the bank at PcdGpioAuxBase). Without it asserted the
  // A-device never powers the bus, so no peripheral can ever pull D+ and
  // PORTSC.CCS stays 0 forever - which is exactly the "nothing is ever logged"
  // symptom. Drive it high and give the boost converter time to come up.
  //
  CtGpioSetOutput (RstBase, CT_OTG_VBUS_LOCAL, TRUE);
  MicroSecondDelay (100000);

  //
  // Read the pad levels back (GPLR) so a stuck/ignored write is visible in the
  // log rather than silently assumed to have worked.
  //
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
// South-complex PMU ("intel_pmu_driver", iomem ff11d000-ff11dfff). The
// downstream kernel drives every south-complex block's clock/power state
// through this block: struct mrst_pmu_reg gives PM_STS 0x00, PM_CMD 0x04,
// PM_ICS 0x08, PM_SSC[4] 0x20, PM_SSS[4] 0x30, and each LSS occupies two bits
// (BITS_PER_LSS = 2) with 0 = D0i0 (fully on) and 3 = D0i3 (clock/power
// gated). USB OTG is PMU_USB_OTG_LSS_06, i.e. bits 13:12 of PM_SSC[0]/PM_SSS[0],
// and clv_pmu_choose_state() parks it in D0i1 by default - so the bootloader
// very likely left the OTG island gated. A gated island explains the exact
// symptom set observed: reads of the operational block return reset values,
// USBINTR (a plain RW register) takes writes, but USBMODE.CM never latches and
// the ULPI viewport never completes, because the core's ULPI/UTMI clock is off.
//
// An "interactive" PM command is issued the same way the kernel does it
// (pmu_issue_interactive_command): write the desired sub-system states into
// PM_SSC[0..3], then write INTERACTIVE_VALUE 0x00002201 to PM_CMD and wait for
// PM_STS busy to clear.
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
  Force the USB OTG south-complex island (LSS 6) into D0i0 so the core gets its
  clocks. Purely diagnostic if it is already in D0i0.
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

  //
  // Ask for the current state of everything except LSS 6, which goes to D0i0.
  //
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

  //
  // Clocks first: nothing else can work while the island is gated.
  //
  CtOtgPmuUngate ();

  //
  // The external ULPI repeater has to be alive before the core is touched.
  //
  CtOtgEnablePhyRepeater ();

  //
  // PHY bring-up, now mirroring the stock OTG0 ASL *exactly*. From the 2760
  // DSDT (Device (OTG0), OperationRegion KEYS @ 0xFFA60000):
  //   _PS0:      UFOT &= 0xFFBFFFFE  -> clear bit 22 (PHY/core power down)
  //                                     and bit 0 (software PHY suspend)
  //   _DSM fn 1: UFOS  = 0x23        -> PHY control byte
  //              UFOT &= 0xFFFFFFFE
  //              if STEP 1/2: UFOR &= 0xFFC0FFFF; UFOR |= 0x000B0000
  //                                  -> ULPI clock/eye config = 0x0B
  // The previous code only wrote UFOS and cleared UFOT bit 0, leaving bit 22
  // (power down) set and never programming UFOR bits 21:16. With the PHY
  // powered down there is no ULPI clock, which is exactly why USBMODE.CM never
  // latched and the ULPI viewport never answered even though plain operational
  // registers (USBINTR) accepted writes.
  //
  //
  // NEW: clear UFOT bit 23 (0x00800000) as well.
  //
  // The stock OTG0 ASL shows what bit 23 is for:
  //   Method (_STA) { If ((USBS == Zero)) { If ((UOFD == One)) { UFOT |= 0x00800000 } ... } }
  // i.e. the platform sets UFOT bit 23 when the "USB OTG Force Device" setup
  // knob is enabled - it straps the dual-role core to device/peripheral mode.
  // _PS0 only clears bits 22 (power down) and 0 (PHY suspend); it never clears
  // bit 23, because on a Windows tablet nothing had set it.
  //
  // On this phone the primary bootloader implements fastboot over this very
  // port, i.e. it ran the core as a USB *device*, so it very plausibly left
  // bit 23 set. That single bit explains the whole remaining symptom set:
  // USBMODE.CM refuses to latch 3 (host) no matter how it is written, PORTSC
  // tracks line state (0x1004 <-> 0x1804) but CCS never asserts, and OTGSC
  // reads 0 - all of which is exactly a core forced into device mode while the
  // PHY (TUSB1211 0x0451/0x1508, IntStatus 0x06 = VbusValid|SessValid,
  // LineState 0x01 = device attached) is perfectly healthy.
  //
  // So clear 23, 22 and 0 together: mask 0xFF3FFFFE.
  //
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

  //
  // ULPI-side host preparation, done HERE - before the controller reset and the
  // USBMODE.CM write - because the Chipidea core only latches CM while the PHY
  // is actually clocking it. Until now this sequence ran later, in the entry
  // point, i.e. long after CM had already been attempted with the PHY in
  // suspend (PORTSC.PHCD set), which is exactly why USBMODE stayed 0 while the
  // repeater itself was alive (TUSB1211 0x0451/0x1508 latched, OtgCtrl read
  // back 0x67, IntStatus 0x06, LineState 0x01).
  //
  // Sequence: clear PORTSC.PHCD, wake the viewport (ULPIWU), then
  //   0x06 (FuncCtrl clear) = 0x20 -> leave PHY reset
  //   0x05 (FuncCtrl set)   = 0x40 -> SuspendM = 1, PHY clocks the bus
  //   0x0B (OtgCtrl set)    = 0x66 -> DrvVbusExternal|DrvVbus|DmPulldown|DpPulldown
  //
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
      //
      // Clear PHCD (PORTSC bit 23) without touching the CSC write-1-to-clear bit.
      //
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

  //
  // Raw 32-bit dump of BAR+0x000..0x1FF, repeated forever (debug). The stock
  // OTG0 DSDT accesses this window with DWordAcc only (UFOR 0x54, UFOT 0xB4)
  // and iomem shows a 128 KiB BAR, so the assumption that BAR+0x000 is the
  // EHCI CAPBASE (0x01100030) may be wrong - 0x000..0x0FF is the Chipidea
  // ID/vendor block. Observe the real layout: look for a dword whose low byte
  // is a plausible CAPLENGTH (0x20/0x30/0x40) with 0x0100 in bits 31:16
  // (HCIVERSION), and for USBMODE (CM bits) 0x68 past the resulting OpBase.
  //

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

  //
  // Stop the controller, then reset it. USBMODE is write-once after reset on
  // this core, so it must be programmed immediately afterwards.
  //
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

  //
  // Dump the operational register window around the expected USBMODE so the
  // real layout is visible instead of assumed. DEBUG_ERROR so it is never
  // filtered out.
  //
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

  //
  // Program USBMODE.CM = host. Try the Chipidea operational offset first and,
  // if it refuses to latch, the absolute BAR + 0x1A8 alias that some Penwell/
  // Cloverview steppings expose. Retry a few times: the write is ignored while
  // the core is still settling after the reset.
  //
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

    //
    // Fallback alias: on steppings whose register file is shifted by 0x100 the
    // operational block starts at BAR + 0x40, putting USBMODE at BAR + 0xA8
    // (penwell_otg's CI_USBMODE 0x1A8 with its cap block at 0x100). BAR + 0x1A8
    // read back as 0x00000000 on this board, i.e. undecoded, so try 0xA8.
    //
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

    //
    // Retry as an 8-bit access, then repeat while USBCMD.RST is asserted;
    // some Cloverview steppings need one or the other.
    //
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

    //
    // EhciDxe only needs the capability/operational registers, and on some
    // Cloverview steppings the core is strapped to host mode with CM reading
    // back 0. Continue rather than abort.
    //
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: USBMODE did not latch host mode - continuing anyway\n"));
    return EFI_SUCCESS;
  }

  return EFI_SUCCESS;
}

//
// Port/OTG status registers (relative to OpBase) used only for logging.
//
#define CT_EHCI_PORTSC        0x44
#define CT_EHCI_OTGSC         0xC4  // Chipidea LPM map: OP_OTGSC (BAR+0xF4; the only register the CM-SCAN could write, read 0x003F0E20).
#define CT_EHCI_ULPIVP        0x30  // ULPI viewport (Chipidea).

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
  Read one ULPI register on the external repeater through the Chipidea ULPI
  viewport. Returns 0xFFFF on timeout, which means the repeater is not clocking
  (i.e. CS/RST handling or the repeater itself is the problem, not the core).
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

  //
  // Two bugs were hiding the repeater here.
  //
  // 1. Read data is returned in ULPIDATRD (bits 15:8), not in ULPIDATWR
  //    (bits 7:0). The old code returned bits 7:0, i.e. the write-data field,
  //    which always reads back 0 - so every register read looked like 0x00 and
  //    the vendor/product validity check rejected it, printing "viewport never
  //    answered" even when the transaction had completed successfully.
  //
  // 2. The viewport only completes while the PHY clock is running. PORTSC.PHCD
  //    (bit 23) gates it and the viewport wants an explicit wakeup (ULPIWU,
  //    bit 31) first; without that RUN can stay set forever. The BAR dump
  //    showed ULPIVP = 0x08000000, i.e. ULPISS set, so the ULPI link itself is
  //    in sync and clocking - only the access sequence was wrong.
  //
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
  Periodic poll that logs USB connect/disconnect transitions seen by the OTG
  port, plus the OTG session/ID bits. Purely diagnostic: PORTSC change bits are
  left alone so EhciDxe/UsbBusDxe still observe them.
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

  //
  // Host system error (USBSTS bit 4) recovery.
  //
  // The SCHED-PROBE result (op+0x10 and everything up to op+0x24 "dropped")
  // is not evidence of wrong schedule-base offsets after all: the probe only
  // runs once USBCMD.ASE|PSE are already set, and PERIODICLISTBASE /
  // ASYNCLISTADDR are architecturally ignored while the schedules are enabled,
  // while op+0x10 (CTRLDSSEGMENT) does not exist at all on this core
  // (HCCPARAMS reports 64-bit = 0). So the offsets are standard EHCI and the
  // real fault is the HOST SYSTEM ERROR bit itself.
  //
  // On ARC/Chipidea EHCI cores that error is the classic TX underrun caused by
  // TXFILLTUNING (op+0x24) and the AHB BURSTSIZE register coming out of reset
  // with unusable values - Linux's ehci-fsl / ci_hdrc host glue programs
  // TXFILLTUNING = 0x00000008 (TXFIFOTHRES) and a sane burst size for exactly
  // this reason. Generic EhciDxe never touches either register, and its HCRESET
  // wipes anything programmed before it binds, so fix it here - once, when the
  // error is actually observed - then clear the status bit and set RUN again.
  //
  {
    STATIC BOOLEAN  mHseFixed = FALSE;
    UINT32          Sts;

    Sts = MmioRead32 (mOtgOpBase + CT_EHCI_USBSTS);
    if (!mHseFixed && ((Sts & BIT4) != 0)) {
      mHseFixed = TRUE;

      MmioWrite32 (mOtgOpBase + CT_EHCI_USBSTS, BIT4);   // write-1-to-clear HSE
      //
      // op+0x24 == BAR+0x54 is TXFILLTUNING, i.e. exactly the register the
      // stock OTG0 _DSM writes as "UFOR" with bits 21:16 = 0x0B
      // (TXFIFOTHRES = 11). The previous value of 8 written here put 8 into
      // TXSCHOH (bits 7:0) and zeroed TXFIFOTHRES, which is precisely the TX
      // underrun -> HOST SYSTEM ERROR condition. Restore the stock threshold
      // (EhciDxe's HCRESET clears it) and keep BURSTSIZE at op+0x20, the only
      // one of the two candidate offsets that latched (0x1010).
      //
      //
      // Downstream ehci_reset() (drivers/usb/host/ehci-hcd.c:264-277) does
      // exactly three things for has_hostpc cores like this one, and EhciDxe's
      // HCRESET wipes all of them:
      //   1. usbmode_ex  = USBMODE_EX_HC | USBMODE_EX_VBPS = 0x23
      //      (include/linux/usb/ehci_def.h: USBMODE_EX is at operational
      //      offset 0xC8 - which is what CT_EHCI_USBMODE already points at,
      //      i.e. the register the stock OTG0 _DSM writes 0x23 to. The VBPS
      //      bit (VBus Power Select) must be set together with CM=3, and we
      //      were only ever writing CM=3.)
      //   2. txfill_tuning = TXFIFO_DEFAULT = (8 << 16) = 0x00080000.
      //      The previous 0x000B0000 came from the PHY calibration register
      //      (UFOR/BAR+0x54) and put TXFIFOTHRES = 0x0B here, which is what
      //      kept producing the TX underrun / USBSTS.HSE and the halt at the
      //      first DMA transfer (SET_ADDRESS).
      //   3. clear HOSTPC_ASUS (BIT0, auto PHY low power) in HOSTPC1, which
      //      lives at BAR+0xB4 = operational offset 0x84; PHCD (BIT22) stays
      //      clear as before.
      //
      MmioWrite32 (mOtgOpBase + CT_EHCI_USBMODE, 0x23);  // USBMODE_EX: HC | VBPS
      MmioWrite32 (mOtgOpBase + 0x24, 0x00080000);       // TXFILLTUNING = TXFIFO_DEFAULT (8 << 16)
      MmioWrite32 (mOtgOpBase + 0x20, 0x00001010);       // BURSTSIZE
      MmioAnd32 (mOtgOpBase + 0x84, ~(UINT32)(BIT0 | BIT22)); // HOSTPC1: clear ASUS auto-LPM + PHCD
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

  //
  // Diagnostic: is EhciDxe actually driving the controller? "CONNECT" with no
  // enumeration and a mouse that powers down means either (a) the host
  // controller was never started (USBCMD.RUN stays 0 / USBSTS.HCHalted stays
  // 1), or (b) it runs but the port never leaves device-mode semantics (PE=1
  // with CCS=0 is not a valid EHCI host port state, which is what has been
  // observed). Log the whole run state whenever it changes - RUN/RESET bits,
  // HCHalted, USBMODE.CM, FRINDEX (must advance once the schedule runs),
  // ASYNCLISTADDR/PERIODICLISTBASE (set by EhciDxe) and CONFIGFLAG.
  //
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

    //
    // The state-change gate hid the decisive information: if EhciDxe never
    // starts the controller, USBCMD/USBSTS/USBMODE never change after the very
    // first tick, so exactly one line was printed during DXE (long before BDS)
    // and it scrolled away - which is why only the CONNECT line is ever seen.
    // Print unconditionally every 20th tick (~5 s) as well, so the run state is
    // observable while a device is plugged in at the Shell.
    //
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

  //
  // NOTE: the post-HCRESET restore below also reprograms TXFILLTUNING/BURSTSIZE.
  // EhciDxe issues a full HCRESET when it binds the controller at BDS
  // (EfiBootManagerConnectAll), and on this Chipidea core HCRESET clears
  // USBMODE.CM back to idle/device and drops the ULPI OTG Control settings
  // (DpPulldown/DmPulldown/DrvVbus) plus PORTSC.PP. That is exactly the
  // observed "works until BDS, then permanently disconnected" behaviour: while
  // host mode is live OTGSC cycles 0x3f0f20/0x3f2f20 and CCS/CSC assert on
  // insert; afterwards it only cycles 0x3f0e20/0x3f2e20 and CCS never asserts
  // again. Re-assert host mode and the A-device ULPI settings whenever CM is
  // no longer 3.
  //
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
    //
    // The periodic host-mode "recovery" is disabled: it is what prevents any
    // device from ever enumerating. CT_EHCI_USBMODE (op+0xC8) is BAR+0xF8, the
    // same vendor byte the DSDT sequence writes 0x23 to, so CM often reads back
    // as "not host" even while the core is running as a host. Every 250 ms tick
    // then cleared PORTSC.PHCD, drove three ULPI viewport writes and did a
    // read-modify-write of PORTSC with PP set - straight through whatever port
    // reset / SET_ADDRESS sequence EhciDxe and UsbBusDxe were in the middle of.
    // That aborts every enumeration (keyboard, mass storage) and kills an
    // already-running mouse, which is exactly the observed behaviour. Keep this
    // poller strictly read-only; if host mode really is lost after EhciDxe's
    // HCRESET, it must be restored from an EHCI binding hook, once, not from a
    // timer that races the host driver.
    //
    //
    // ONE-SHOT host-mode restore. Host mode is confirmed working before BDS
    // (CCS/CSC assert on insert, OTGSC cycles 0x3f0f20/0x3f2f20); it is lost
    // exactly when EhciDxe binds the controller and issues HCRESET, which on
    // this Chipidea core clears USBMODE.CM and the ULPI A-device settings.
    // A continuously repeating recovery races EhciDxe/UsbBusDxe port resets and
    // aborts every enumeration, so restore host mode at most ONCE - the first
    // tick on which CM is seen to have left host mode (i.e. right after
    // EhciDxe's HCRESET) - and never touch the port again afterwards.
    //
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
  ExitBootServices handler: stop all firmware-side polling of the OTG block and
  leave the platform pads in the state Windows needs.

  Windows drives OTG0 through its own usbehci.sys bound to the ACPI device, and
  its _DSM only programs UFOR/UFOT/UFOS. It has no driver for the TUSB1211
  chip-select / reset lines (clv_gpio_1 pin 171, clv_gpio_0 pin 78) or for the
  CHG_OTG VBUS boost line (clv_gpio_0 pin 92), so those must stay asserted
  across the hand-off. Also clear HOSTPC1's PHY low-power/auto-suspend bits so
  the repeater keeps clocking ULPI while Windows re-initialises the core.
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

  //
  // No firmware MMIO on this block once the OS owns it.
  //
  if (mOtgPollEvent != NULL) {
    gBS->SetTimer (mOtgPollEvent, TimerCancel, 0);
  }

  //
  // Re-assert CS / release RST / keep the VBUS boost on.
  //
  CtOtgEnablePhyRepeater ();

  //
  // HOSTPC1 (BAR+0xB4 = op+0x84): clear ASUS (BIT0, automatic PHY low power)
  // and PHCD (BIT22, PHY clock disable) so the PHY is not parked at hand-off.
  //
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

  //
  // Debug: prove the driver is dispatched at all. DEBUG_ERROR so it cannot be
  // filtered by PcdDebugPrintErrorLevel, printed before any MMIO access.
  //
  DEBUG ((DEBUG_ERROR, "OtgHostDxe: ENTRY reached, OTG0 at 0x%08x + 0x%x\n", Base, Size));

  Status = CtOtgEnterHostMode (Base);
  if (EFI_ERROR (Status)) {
    //
    // Debug: the failure path returned quietly, so no ULPI line ever appeared.
    // Loop on the failure line too, with the raw ID/CAPLENGTH readings, so the
    // reason is impossible to miss.
    //
    //
    // Debug: this used to be "if (TRUE)", i.e. a single print followed by a
    // silent return, so no repeating failure line was ever seen. Loop for real.
    //
    DEBUG ((
      DEBUG_ERROR,
      "OtgHostDxe: host-mode bring-up FAILED - %r (ID=0x%08x CAPLEN=0x%02x)\n",
      Status,
      MmioRead32 (Base + CT_CI_ID),
      MmioRead8 (Base + CT_CI_CAP_OFFSET + CT_EHCI_CAPLENGTH)
      ));

    return Status;
  }

  //
  // Remember OpBase for the status poller and report the repeater identity and
  // the initial port state.
  //
  CapLength   = MmioRead8 (Base + CT_CI_CAP_OFFSET + CT_EHCI_CAPLENGTH);
  mOtgOpBase  = Base + CT_CI_CAP_OFFSET + CapLength;

  //
  // ULPI repeater identity. The viewport can take a while to start answering
  // after the repeater's 60 MHz clock comes up, so retry for up to ~500 ms and
  // latch the first valid answer. Once latched the values are frozen and the
  // line is printed exactly once, so it stays readable on the framebuffer
  // console instead of being re-read/overwritten later.
  //
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
      //
      // Debug request: repeat the latched identity line forever (0.5 s apart)
      // so it is impossible to miss on the framebuffer console. Boot does not
      // continue past this point while this loop is in place.
      //
      //
      // Debug: the previous "if (TRUE)" printed the line exactly once, which is
      // why no repeating output was ever observed. Loop for real.
      //
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

  //
  // CCS never asserts even with external bus power, yet the ULPI link is alive
  // (TUSB1211 0x0451/0x1508 latched on the first attempt). Everything that is
  // still missing lives inside the repeater rather than in the Chipidea core:
  // as an A-device the PHY needs DpPulldown/DmPulldown enabled (otherwise the
  // bus is never terminated as a host and a peripheral pulling D+ is invisible),
  // VBUS driven via DrvVbus + DrvVbusExternal (the 5 V boost is external and
  // only *requested* by CHG_OTG), and SuspendM released so the PHY actually
  // clocks the bus. The Chipidea core programs none of that, so it is done here
  // through the ULPI viewport (write: RUN|RW, addr in 23:16, data in 7:0) and
  // then read back together with the Interrupt Status and Debug (LineState)
  // registers, which say directly whether the PHY sees a device.
  //
  {
    STATIC CONST UINT32  UlpiInit[][2] = {
      { 0x06, 0x20 },   // Function Control clear: leave PHY reset
      { 0x05, 0x40 },   // Function Control set:   SuspendM = 1 (not suspended)
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

  //
  // Force port power on unconditionally (harmless if PPC is 0) so a device can
  // be detected at all; EhciDxe will take over the port afterwards.
  //
  Portsc = MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC);
  MmioWrite32 (mOtgOpBase + CT_EHCI_PORTSC, (Portsc & ~(UINT32)CT_PORTSC_CSC) | CT_PORTSC_PP);
  MicroSecondDelay (100000);

  DEBUG ((
    DEBUG_ERROR,
    "OtgHostDxe: initial PORTSC 0x%08x OTGSC 0x%08x\n",
    MmioRead32 (mOtgOpBase + CT_EHCI_PORTSC),
    MmioRead32 (mOtgOpBase + CT_EHCI_OTGSC)
    ));

  //
  // 250 ms periodic connect/disconnect logger.
  //
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

  //
  // Hand-off to the OS (Windows).
  //
  // Pads verified against the downstream kernel this round:
  //   intel_soc_dump.c: GPIO0 = 0xff119000, GPIO1 = 0xff13f000
  //   sfi-tables/GPIO:  clv_gpio_0 pin 78 usb_otg_phy_rst, pin 92 CHG_OTG,
  //                     clv_gpio_1 pin 171 usb_otg_phy_cs (local 171-96 = 75)
  //   penwell_otg.c:    phy_power() = gpio_cs level, phy_reset() = gpio_reset
  //                     pulse; there is no pad-mux step, GAFR must be 0 (GPIO),
  //                     which lnw_gpio_request() also enforces.
  // So PcdGpioAuxBase/PcdGpioCoreBase and the three local pin numbers used by
  // CtOtgEnablePhyRepeater() are correct.
  //
  // Windows, however, has no driver for these three pads: its usbehci.sys only
  // gets the ACPI OTG0 device (_CRS + _DSM), which touches UFOR/UFOT/UFOS but
  // never the TUSB1211 CS/RST lines or the CHG_OTG VBUS boost. Anything that
  // parks the PHY or drops VBUS before/at ExitBootServices therefore leaves
  // Windows with a dead port. Re-assert all three pads (and clear PHY
  // low-power/force-device in HOSTPC1/UFOT) at ExitBootServices, and cancel the
  // status poller first so no firmware MMIO happens once the OS owns the block.
  //
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

  //
  // Hand EhciDxe a window that starts at the EHCI capability registers.
  //
  Handle = NULL;
  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeEhci,
             //
             // DMA: NonCoherent, not Coherent.
             //
             // EhciDxe now binds and starts the core, but the reported run state
             // is USBCMD 0x00080B30 (RUN=0, ASE|PSE requested), USBSTS 0x0000D094
             // and every transfer fails with "EhcControlTransfer: HC halted at
             // entrance" / Device Error. USBSTS bit4 (0x10) is HOST SYSTEM ERROR:
             // the controller took an error on the memory bus while fetching its
             // periodic/async schedule, which makes it halt itself immediately
             // after RUN is set (hence HCHalted=1 with the schedule enable bits
             // still asserted, FRINDEX frozen, and the later reset once more DMA
             // traffic is attempted).
             //
             // Cause: the device was registered as DmaTypeCoherent, so
             // NonDiscoverablePciDeviceDxe hands EhciDxe plain cached normal
             // memory for the QH/qTD lists and data buffers. This SoC's OTG
             // master is not cache-coherent with the CPU (the Linux platform data
             // marks these south-complex USB/SD masters non-coherent), so the
             // controller reads stale/garbage descriptors and raises a host
             // system error. NonCoherent makes the shim allocate uncached
             // (EFI_MEMORY_UC) DMA buffers and use Map/Unmap semantics.
             //
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

  //
  // Decisive diagnostic for the "CONNECT but nothing enumerates" symptom.
  // The reported HC state was USBCMD 0x00080B00 (RUN=0), USBSTS 0x00001484
  // (HCHalted=1), USBMODE 0x00005003 (CM=3, host mode IS latched), FRINDEX 0,
  // PERIODICLISTBASE 0, ASYNCLISTADDR 0, CONFIGFLAG 1. Host mode is fine; the
  // controller was simply never started - EhciDxe either never bound to this
  // handle or bailed out of EhcInitHC. Connect the handle explicitly here and
  // log the outcome; EhciDxe's own failure paths ("failed to init host
  // controller", "EhcInitHC: failed to enable period/async schedule") print at
  // DEBUG_ERROR immediately after this line, and the register dump below shows
  // whether the schedule bases and RUN bit ever get programmed.
  //
  //
  // Do NOT connect the handle here. OtgHostDxe is a DXE_DRIVER with Depex TRUE,
  // so it runs in the first dispatcher pass, before NonDiscoverablePciDeviceDxe
  // and EhciDxe have installed their Driver Binding protocols - the connect can
  // only fail (EFI_NOT_FOUND), and holding the ND protocol BY_DRIVER from an
  // early failed pass only gets in the way. BDS connects it later:
  // PlatformBootManagerBeforeConsole signals EndOfDxe and calls
  // EfiBootManagerConnectAll at TPL_APPLICATION, which walks the whole handle
  // database (repeatedly, so PciIo installed on this same handle is seen on a
  // later pass) and lets EhciDxe bind and run EhcInitHC undisturbed.
  //
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
