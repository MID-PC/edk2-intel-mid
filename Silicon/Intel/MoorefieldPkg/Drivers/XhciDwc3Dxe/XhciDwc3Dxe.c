/** @file
  Moorefield DWC3 OTG core: host-mode bring-up, VBUS, and xHCI registration.

  The kernel picks host vs peripheral from an extcon state machine firmware has no
  use for, so this assumes an OTG cable and runs the ZX551ML sequence unchanged.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MtrrLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/PcdLib.h>
#include <Library/ScuIpcLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Usb2HostController.h>

#include "Dwc3Regs.h"

#define USB_PHY_DEBOUNCE_US       10000U     // VUSBPHY settle, control_usb_phy_power()
#define USB_IDLE_SETTLE_US        100000U    // dwc3_intel_b_idle()
#define USB_VBUS_POLL_STEP_US     10000U
#define USB_VBUS_POLL_LIMIT       50U        // 500 ms for VBUS to be reported valid
#define DWC3_PHY_RESET_HOLD_US    100000U    // dwc_core_reset()
#define DWC3_CORE_RESET_SETTLE_US 20000U
#define DWC3_HOST_MODE_SETTLE_US  20000U

/**
  Gate or ungate the USB2 PHY (enable_usb_phy() / control_usb_phy_power()).

  The UTMI PHY runs off the PMIC VUSBPHY rail, and while it is down a peripheral
  biasing DP/DM can damage the SoC, so USBRST# brackets it. ULPI just holds reset.

  @param[in] Ulpi  TRUE if the external ULPI PHY is fitted.
  @param[in] On    TRUE to power up, FALSE to power down.
**/
STATIC
VOID
Usb2PhyPower (
  IN BOOLEAN  Ulpi,
  IN BOOLEAN  On
  )
{
  EFI_STATUS  Status;
  UINT8       Rstb;

  Rstb   = On ? PMIC_USBPHYCTRL_USBPHYRSTB : 0;
  Status = EFI_SUCCESS;

  if (Ulpi) {
    Status = ScuIpcPmicUpdate (PMIC_USBPHYCTRL, Rstb, PMIC_USBPHYCTRL_USBPHYRSTB);
  } else if (On) {
    Status = ScuIpcPmicUpdate (PMIC_VLDOCNT, PMIC_VLDOCNT_VUSBPHYEN, PMIC_VLDOCNT_VUSBPHYEN);
    MicroSecondDelay (USB_PHY_DEBOUNCE_US);
    if (!EFI_ERROR (Status)) {
      Status = ScuIpcPmicUpdate (PMIC_USBPHYCTRL, Rstb, PMIC_USBPHYCTRL_USBPHYRSTB);
    }
  } else {
    Status = ScuIpcPmicUpdate (PMIC_USBPHYCTRL, 0, PMIC_USBPHYCTRL_USBPHYRSTB);
    if (!EFI_ERROR (Status)) {
      Status = ScuIpcPmicUpdate (PMIC_VLDOCNT, 0, PMIC_VLDOCNT_VUSBPHYEN);
    }
  }

  DEBUG ((
    EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_ERROR,
    "XhciDwc3Dxe: %a USB2 PHY %a: %r\n",
    Ulpi ? "ULPI" : "UTMI",
    On ? "on" : "off",
    Status
    ));
}

/**
  Drive VBUS as an OTG A-device (setSMB1357Charger(ENABLE_5V) + otg(1)).
  Shady Cove is told it is the OTG source, then PMIC GPIO 6 raises the charger's OTG
  pin to put 5V on VBUS. A missing VBUS-valid bit is reported but not fatal.

  @retval EFI_SUCCESS  VBUS was enabled and reported valid.
  @retval Other        A PMIC access failed, or VBUS never reported valid.
**/
STATIC
EFI_STATUS
Usb2EnableVbus (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       Sts;
  UINT32      Retry;

  Sts    = 0;
  Status = ScuIpcPmicUpdate (PMIC_CHGRCTRL1, PMIC_CHGRCTRL1_OTGMODE, PMIC_CHGRCTRL1_OTGMODE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: PMIC OTG mode: %r\n", Status));
    return Status;
  }

  Status = ScuIpcPmicUpdate (PMIC_GPIO6_CTLO, PMIC_GPIO6_CTLO_OTG_EN, PMIC_GPIO6_CTLO_OTG_EN);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: SMB1357 OTG pin: %r\n", Status));
    return Status;
  }

  {
    UINT8  Chg;
    UINT8  Gpio;

    Chg  = 0;
    Gpio = 0;
    ScuIpcPmicRead (PMIC_CHGRCTRL1, &Chg);
    ScuIpcPmicRead (PMIC_GPIO6_CTLO, &Gpio);
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: readback CHGRCTRL1 0x%02x (want BIT6), GPIO6CTLO 0x%02x (want 0x31 bits)\n", Chg, Gpio));
  }

  for (Retry = 0; Retry < USB_VBUS_POLL_LIMIT; Retry++) {
    Status = ScuIpcPmicRead (PMIC_SCHGRIRQ1, &Sts);
    if (!EFI_ERROR (Status) && ((Sts & PMIC_SCHGRIRQ1_SVBUSDET) != 0)) {
      DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: VBUS on (SCHGRIRQ1 0x%02x, %u ms)\n", Sts, Retry * (USB_VBUS_POLL_STEP_US / 1000)));
      return EFI_SUCCESS;
    }

    MicroSecondDelay (USB_VBUS_POLL_STEP_US);
  }

  DEBUG ((DEBUG_WARN, "XhciDwc3Dxe: VBUS enabled but not reported valid (SCHGRIRQ1 0x%02x)\n", Sts));
  return EFI_TIMEOUT;
}

/**
  Set the core's port capability direction, leaving the other GCTL bits alone
  (dwc3_switch_mode()).
**/
STATIC
VOID
Dwc3SwitchMode (
  IN UINTN   Base,
  IN UINT32  Mode
  )
{
  UINT32  Gctl;

  Gctl  = MmioRead32 (Base + DWC3_GCTL);
  Gctl &= ~DWC3_GCTL_PRTCAPDIR_MASK;
  Gctl |= DWC3_GCTL_PRTCAPDIR (Mode);
  MmioWrite32 (Base + DWC3_GCTL, Gctl);
}

/**
  Bring the OTG block to its idle state with the USB2 PHY off
  (dwc3_intel_platform_init() + dwc3_intel_b_idle()): hibernation off, ADP and OTG
  cleared, core forced to OTG so nothing drives D+ before host mode starts.
**/
STATIC
VOID
Dwc3OtgIdle (
  IN UINTN    Base,
  IN BOOLEAN  Ulpi
  )
{
  UINT32  Tmp;

  MmioAnd32 (Base + DWC3_GCTL, ~(UINT32)DWC3_GCTL_GBLHIBERNATIONEN);

  MmioWrite32 (Base + DWC3_ADPCFG, 0);
  MmioWrite32 (Base + DWC3_ADPCTL, 0);
  MmioWrite32 (Base + DWC3_ADPEVTEN, 0);
  Tmp = MmioRead32 (Base + DWC3_ADPEVT);
  MmioWrite32 (Base + DWC3_ADPEVT, Tmp);

  MmioWrite32 (Base + DWC3_OCFG, 0);
  MmioWrite32 (Base + DWC3_OCTL, 0);
  MmioWrite32 (Base + DWC3_OEVTEN, 0);
  Tmp = MmioRead32 (Base + DWC3_OEVT);
  MmioWrite32 (Base + DWC3_OEVT, Tmp);

  Dwc3SwitchMode (Base, DWC3_GCTL_PRTCAP_OTG);

  Usb2PhyPower (Ulpi, FALSE);
  MicroSecondDelay (USB_IDLE_SETTLE_US);
}

/**
  Soft-reset the core with both PHYs held in reset (dwc_core_reset()).
**/
STATIC
VOID
Dwc3CoreReset (
  IN UINTN  Base
  )
{
  MmioOr32 (Base + DWC3_GCTL, DWC3_GCTL_CORESOFTRESET);
  MmioOr32 (Base + DWC3_GUSB3PIPECTL0, DWC3_GUSB3PIPECTL_PHYSOFTRST);
  MmioOr32 (Base + DWC3_GUSB2PHYCFG0, DWC3_GUSB2PHYCFG_PHYSOFTRST);

  MicroSecondDelay (DWC3_PHY_RESET_HOLD_US);

  MmioAnd32 (Base + DWC3_GUSB3PIPECTL0, ~(UINT32)DWC3_GUSB3PIPECTL_PHYSOFTRST);
  MmioAnd32 (Base + DWC3_GUSB2PHYCFG0, ~(UINT32)DWC3_GUSB2PHYCFG_PHYSOFTRST);

  MicroSecondDelay (DWC3_CORE_RESET_SETTLE_US);

  MmioAnd32 (Base + DWC3_GCTL, ~(UINT32)DWC3_GCTL_CORESOFTRESET);
}

/**
  Switch the core to host mode with the USB2 PHY running
  (dwc3_intel_prepare_start_host(), the SCU workaround from dwc3_intel_resume(),
  and the core setup of __dwc3_start_host()).

  @param[in] Base   DWC3 aperture base.
  @param[in] Ulpi   TRUE if the external ULPI PHY is fitted.
**/
STATIC
VOID
Dwc3StartHost (
  IN UINTN    Base,
  IN BOOLEAN  Ulpi
  )
{
  Dwc3SwitchMode (Base, DWC3_GCTL_PRTCAP_HOST);

  Usb2PhyPower (Ulpi, TRUE);

  // Do not let either PHY suspend: a suspended PHY makes FS/LS devices fail to
  // enumerate in host mode (set_sus_phy(otg, 0)). ULPI auto-resume is a silicon
  // erratum and stays off (disable_phy_auto_resume()).
  MmioAnd32 (Base + DWC3_GUSB2PHYCFG0, ~(UINT32)(DWC3_GUSB2PHYCFG_SUS_PHY | DWC3_GUSB2PHYCFG_ULPI_AUTORSM));
  MmioAnd32 (Base + DWC3_GUSB3PIPECTL0, ~(UINT32)DWC3_GUSB3PIPECTL_SUS_EN);

  //
  // SCU is supposed to set GUSB2PHYCFG0.bit4 for a ULPI PHY and does not, so
  // set it (or clear it for UTMI) here (dwc3_intel_resume()).
  //
  if (Ulpi) {
    MmioOr32 (Base + DWC3_GUSB2PHYCFG0, DWC3_GUSB2PHYCFG_ULPI_SELECT);
  } else {
    MmioAnd32 (Base + DWC3_GUSB2PHYCFG0, ~(UINT32)DWC3_GUSB2PHYCFG_ULPI_SELECT);
  }

  MicroSecondDelay (1000);

  Dwc3CoreReset (Base);

  // dwc_silicon_wa()
  MmioAnd32 (Base + DWC3_GUCTL, ~(UINT32)DWC3_GUCTL_CMDEVADDR);
  MmioOr32 ((UINTN)APBFC_EXIOTG3_MISC0_REG, APBFC_EXIOTG3_MISC0_DIS_EXI);

  //
  // dwc_set_host_mode() + dwc_set_ssphy_p3_clockrate(): the kernel writes the
  // whole of GCTL, dropping every other option, then patches PWRDNSCALE.
  //
  MmioWrite32 (
    Base + DWC3_GCTL,
    DWC3_GCTL_PRTCAPDIR (DWC3_GCTL_PRTCAP_HOST) | DWC3_GCTL_PWRDNSCALE (DWC3_GCTL_PWRDNSCALE_HOST)
    );
  MicroSecondDelay (DWC3_HOST_MODE_SETTLE_US);

  DEBUG ((
    DEBUG_ERROR,
    "XhciDwc3Dxe: host mode: GCTL 0x%08x GUSB2PHYCFG0 0x%08x\n",
    MmioRead32 (Base + DWC3_GCTL),
    MmioRead32 (Base + DWC3_GUSB2PHYCFG0)
    ));
}


//
// Read-only monitor. XhciDxe is silent unless it fails, so this reports on change
// whether the host started (USBCMD.R/S) and whether ports are powered (PORTSC.PP)
// with anything seen on them (PORTSC.CCS), until DWC3_MON_TICKS or ExitBootServices.
//
#define DWC3_MON_PERIOD_100NS  5000000ULL   // 500 ms
#define DWC3_MON_TICKS         1200U    // 10 minutes
#define DWC3_MON_PMIC_EVERY    20U      // ticks, 10 s
#define DWC3_MON_MAX_PORTS     8U

STATIC EFI_HANDLE  mDeviceHandle;
STATIC EFI_EVENT   mMonTimer;
STATIC EFI_EVENT  mMonExitEvent;
STATIC UINT32     mMonTicks;
STATIC UINT32     mMonLast[2 + DWC3_MON_MAX_PORTS];
STATIC UINT32     mMonPmicLast = MAX_UINT32;


/**
  Connect drivers to the registered device and report whether an xHCI host
  controller protocol has appeared. If nothing connected the handle, because BDS
  connected everything before it existed, the controller never starts.
**/
STATIC
VOID
Dwc3ConnectAndReport (
  IN CONST CHAR8  *When
  )
{
  EFI_STATUS  Status;
  UINTN       Count;
  EFI_HANDLE  *Handles;

  Status = gBS->ConnectController (mDeviceHandle, NULL, NULL, TRUE);
  Count   = 0;
  Handles = NULL;
  gBS->LocateHandleBuffer (ByProtocol, &gEfiUsb2HcProtocolGuid, NULL, &Count, &Handles);
  if (Handles != NULL) {
    gBS->FreePool (Handles);
  }

  DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: %a: ConnectController %r, USB2 HC handles: %u\n", When, Status, (UINT32)Count));
}

STATIC
VOID
EFIAPI
Dwc3MonStop (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mMonTimer != NULL) {
    gBS->SetTimer (mMonTimer, TimerCancel, 0);
    gBS->CloseEvent (mMonTimer);
    mMonTimer = NULL;
  }
}

STATIC
VOID
EFIAPI
Dwc3MonTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN   Base;
  UINTN   Op;
  UINT32  Ports;
  UINT32  I;
  UINT32  V;

  Base  = (UINTN)FixedPcdGet32 (PcdDwc3Base);
  Op    = Base + MmioRead8 (Base);
  Ports = (MmioRead32 (Base + 0x04) >> 24) & 0xFF;
  if (Ports > DWC3_MON_MAX_PORTS) {
    Ports = DWC3_MON_MAX_PORTS;
  }

  V = MmioRead32 (Op + 0x00);
  if (V != mMonLast[0]) {
    mMonLast[0] = V;
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: USBCMD 0x%08x (%a)\n", V, (V & BIT0) ? "running" : "stopped"));
  }

  V = MmioRead32 (Op + 0x04);
  if (V != mMonLast[1]) {
    mMonLast[1] = V;
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: USBSTS 0x%08x\n", V));
  }

  for (I = 0; I < Ports; I++) {
    V = MmioRead32 (Op + 0x400 + I * 0x10);
    if (V != mMonLast[2 + I]) {
      mMonLast[2 + I] = V;
      DEBUG ((
        DEBUG_ERROR,
        "XhciDwc3Dxe: PORTSC%u 0x%08x (%a%a, PLS %u, speed %u)\n",
        I + 1, V,
        (V & BIT9) ? "PP " : "no-PP ",
        (V & BIT0) ? "CONNECTED" : "empty",
        (V >> 5) & 0xF,
        (V >> 10) & 0xF
        ));
    }
  }


  //
  // VBUS/OTG state from the PMIC, sampled every 10 s and logged on change. If
  // the charger watchdog eventually drops VBUS, the timestamp shows when.
  //
  if ((mMonTicks % DWC3_MON_PMIC_EVERY) == 0) {
    UINT8   Irq;
    UINT8   Chg;
    UINT8   Gpio;
    UINT32  Pack;

    Irq  = 0;
    Chg  = 0;
    Gpio = 0;
    ScuIpcPmicRead (PMIC_SCHGRIRQ1, &Irq);
    ScuIpcPmicRead (PMIC_CHGRCTRL1, &Chg);
    ScuIpcPmicRead (PMIC_GPIO6_CTLO, &Gpio);
    Pack = Irq | ((UINT32)Chg << 8) | ((UINT32)Gpio << 16);
    if (Pack != mMonPmicLast) {
      mMonPmicLast = Pack;
      DEBUG ((
        DEBUG_ERROR,
        "XhciDwc3Dxe: t=%us PMIC SCHGRIRQ1 0x%02x CHGRCTRL1 0x%02x GPIO6CTLO 0x%02x\n",
        mMonTicks / 2, Irq, Chg, Gpio
        ));
    }
  }

  if ((mMonTicks == 1) || (mMonTicks == 10)) {
    Dwc3ConnectAndReport ("tick");
  }

  if (++mMonTicks >= DWC3_MON_TICKS) {
    Dwc3MonStop (NULL, NULL);
  }
}

/**
  Log the xHCI capability registers and the extended capability list once, so
  the port layout (which ports are USB2 and which USB3), the debug capability
  and legacy-support handoff state are on record.
**/
STATIC
VOID
Dwc3DumpCaps (
  VOID
  )
{
  UINTN   Base;
  UINT32  Hcc;
  UINT32  Off;
  UINT32  Cap;
  UINT32  Guard;

  Base = (UINTN)FixedPcdGet32 (PcdDwc3Base);
  Hcc  = MmioRead32 (Base + 0x10);
  DEBUG ((
    DEBUG_ERROR,
    "XhciDwc3Dxe: CAPLENGTH 0x%02x HCIVERSION 0x%04x HCSPARAMS1 0x%08x HCSPARAMS2 0x%08x HCCPARAMS1 0x%08x (AC64 %u)\n",
    MmioRead8 (Base), MmioRead16 (Base + 2), MmioRead32 (Base + 4), MmioRead32 (Base + 8), Hcc, Hcc & 1
    ));

  Off = ((Hcc >> 16) & 0xFFFF) << 2;
  for (Guard = 0; (Off != 0) && (Guard < 16); Guard++) {
    Cap = MmioRead32 (Base + Off);
    DEBUG ((
      DEBUG_ERROR,
      "XhciDwc3Dxe: xECP @0x%04x id %u: %08x %08x %08x %08x\n",
      Off, Cap & 0xFF, Cap, MmioRead32 (Base + Off + 4), MmioRead32 (Base + Off + 8), MmioRead32 (Base + Off + 12)
      ));
    if (((Cap >> 8) & 0xFF) == 0) {
      break;
    }

    Off += ((Cap >> 8) & 0xFF) << 2;
  }
}

STATIC
VOID
Dwc3MonStart (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       I;

  for (I = 0; I < ARRAY_SIZE (mMonLast); I++) {
    mMonLast[I] = MAX_UINT32;
  }

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Dwc3MonTick, NULL, &mMonTimer);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (mMonTimer, TimerPeriodic, DWC3_MON_PERIOD_100NS);
  }

  if (!EFI_ERROR (Status)) {
    gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_CALLBACK, Dwc3MonStop, NULL, &mMonExitEvent);
  }

  Dwc3DumpCaps ();
  DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: monitor: %r\n", Status));
}

/**
  Make the DWC3 aperture uncacheable.
  DxeIpl maps everything write-back and nothing else sets an MTRR, so the aperture
  comes in cached: fine for a read-only ID register, fatal for polled bits.
  MtrrLib and one MTRR, BSP only, as this tree has no SetMemoryAttribute().

  @retval TRUE   The aperture is uncacheable.
  @retval FALSE  Attributes could not be programmed; reads may be wrong.
**/
BOOLEAN
Dwc3MakeUncacheable (
  IN UINTN  Base,
  IN UINTN  Size
  )
{
  RETURN_STATUS  Status;

  Status = MtrrSetMemoryAttribute (Base, Size, CacheUncacheable);

  DEBUG ((
    DEBUG_ERROR,
    "XhciDwc3Dxe: MtrrSetMemoryAttribute(0x%08x, 0x%08x, UC) -> 0x%016lx\n",
    (UINT32)Base,
    (UINT32)Size,
    (UINT64)Status
    ));

  return (BOOLEAN)(!RETURN_ERROR (Status));
}

/**
  Driver entry point.

  Any failure is logged and leaves the driver loaded but the controller
  unregistered, so a broken USB port never stops DXE dispatch.
**/
EFI_STATUS
EFIAPI
XhciDwc3DxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINTN       Base;
  UINTN       Size;
  UINT32      Id;
  BOOLEAN     Ulpi;

  Base = (UINTN)FixedPcdGet32 (PcdDwc3Base);
  Size = (UINTN)FixedPcdGet32 (PcdDwc3Size);

  DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: entry, aperture 0x%08x\n", (UINT32)Base));

  // Before the first register read, not after: every read from here assumes the
  // aperture is uncacheable.
  if (!Dwc3MakeUncacheable (Base, Size)) {
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: continuing with a cached aperture\n"));
  }

  Id = MmioRead32 (Base + DWC3_GSNPSID);
  if ((Id & DWC3_GSNPSID_MASK) != DWC3_GSNPSID_ID) {
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: GSNPSID 0x%08x at 0x%08x is not a DWC3 core\n", Id, (UINT32)Base));
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: DWC3 core 0x%08x\n", Id));

  //
  // SCCB_USB_CFG bit 14 straps the USB2 PHY: set = external ULPI (TUSB1211),
  // clear = integrated UTMI (get_usb2_phy_type()).
  //
  Ulpi = (BOOLEAN)((MmioRead32 (SCCB_USB_CFG) & SCCB_USB_CFG_SELECT_ULPI) != 0);
  DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: USB2 PHY is %a\n", Ulpi ? "ULPI" : "UTMI"));

  DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: OTG idle\n"));
  Dwc3OtgIdle (Base, Ulpi);

  //
  // VBUS first, then the host, as do_a_host() does. If the PMIC accesses fail
  // (for example SCU refuses them) host mode is still started so a self-powered
  // device on the port can enumerate.
  //
  DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: enabling VBUS\n"));
  Usb2EnableVbus ();
  DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: starting host\n"));
  Dwc3StartHost (Base, Ulpi);

  // The whole aperture goes to the non-discoverable bus: XhciDxe reads the
  // capability registers from offset 0 and never touches the DWC3 globals.
  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeXhci,
             NonDiscoverableDeviceDmaTypeCoherent,
             NULL,
             &mDeviceHandle,
             1,
             Base,
             Size
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: xHCI registration failed: %r\n", Status));
  } else {
    Dwc3ConnectAndReport ("entry");
    Dwc3MonStart ();
    DEBUG ((DEBUG_ERROR, "XhciDwc3Dxe: xHCI registered at 0x%08x + 0x%x\n", (UINT32)Base, (UINT32)Size));
  }

  return EFI_SUCCESS;
}
