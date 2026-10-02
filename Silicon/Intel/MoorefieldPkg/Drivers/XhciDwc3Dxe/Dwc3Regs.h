/** @file
  Register and PMIC definitions for the Synopsys DWC3 OTG core on Moorefield.

  All offsets are relative to the DWC3 aperture base (PcdDwc3Base, 0xF9100000),
  exactly as the ZX551ML kernel spells them, so this file can be diffed against
  drivers/usb/dwc3/{core.h,otg.h} and include/linux/usb/dwc3-intel-mid.h in
  ZX551ML/kernel_MM/kernel line by line. The xHCI capability/operational
  registers occupy the bottom of the same aperture and are owned by
  MdeModulePkg's XhciDxe.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef DWC3_REGS_H_
#define DWC3_REGS_H_

//
// DWC3 global registers (core.h / otg.h)
//
#define DWC3_GCTL           0xC110
#define DWC3_GUCTL          0xC12C
#define DWC3_GSNPSID        0xC120
#define DWC3_GUSB2PHYCFG0   0xC200
#define DWC3_GUSB3PIPECTL0  0xC2C0

//
// DWC3 OTG block (otg.h)
//
#define DWC3_OCFG           0xCC00
#define DWC3_OCTL           0xCC04
#define DWC3_OEVT           0xCC08
#define DWC3_OEVTEN         0xCC0C
#define DWC3_ADPCFG         0xCC20
#define DWC3_ADPCTL         0xCC24
#define DWC3_ADPEVT         0xCC28
#define DWC3_ADPEVTEN       0xCC2C

//
// GSNPSID: upper half identifies the core ("U3"), lower half is the revision.
//
#define DWC3_GSNPSID_MASK   0xFFFF0000
#define DWC3_GSNPSID_ID     0x55330000

//
// GCTL
//
#define DWC3_GCTL_GBLHIBERNATIONEN  BIT1
#define DWC3_GCTL_CORESOFTRESET     BIT11
#define DWC3_GCTL_PRTCAPDIR(N)      ((UINT32)(N) << 12)
#define DWC3_GCTL_PRTCAPDIR_MASK    DWC3_GCTL_PRTCAPDIR (3)
#define DWC3_GCTL_PRTCAP_HOST       1
#define DWC3_GCTL_PRTCAP_OTG        3
#define DWC3_GCTL_PWRDNSCALE(N)     ((UINT32)(N) << 19)

//
// The suspend clock on Merrifield/Moorefield is 19.2 MHz, so
// PwrDnScale = 19200 / 16 = 1200 (0x4B0). dwc_set_ssphy_p3_clockrate() in
// dwc3-host-intel.c adds margin for suspend-clock jitter and programs 0x4E2.
//
#define DWC3_GCTL_PWRDNSCALE_HOST   0x4E2

//
// GUCTL. Bit 15 (CMDEVADDR) must be clear or the core misses transfer-complete
// events after hibernation exit (dwc_silicon_wa()).
//
#define DWC3_GUCTL_CMDEVADDR        BIT15

//
// GUSB2PHYCFG0
//
#define DWC3_GUSB2PHYCFG_ULPI_SELECT   BIT4    // 1 = external ULPI PHY
#define DWC3_GUSB2PHYCFG_SUS_PHY       BIT6
#define DWC3_GUSB2PHYCFG_ULPI_AUTORSM  BIT15
#define DWC3_GUSB2PHYCFG_PHYSOFTRST    BIT31

//
// GUSB3PIPECTL0
//
#define DWC3_GUSB3PIPECTL_SUS_EN       BIT17
#define DWC3_GUSB3PIPECTL_PHYSOFTRST   BIT31

//
// SoC registers outside the aperture, from dwc3-intel-mid.h
//
#define SCCB_USB_CFG                   0xFF03A018
#define SCCB_USB_CFG_SELECT_ULPI       BIT14

//
// APBFC EXI-OTG3 misc register. Bit 3 disables the OTG3-EXI interface, which
// is a silicon workaround for failing transfers on EP#8 (dwc_silicon_wa()).
//
#define APBFC_EXIOTG3_MISC0_REG        0xF90FF85C
#define APBFC_EXIOTG3_MISC0_DIS_EXI    BIT3

//
// Shady Cove PMIC registers, reached over SCU IPC.
//
#define PMIC_USBPHYCTRL                0x30    // dwc3-intel-mid.h
#define PMIC_USBPHYCTRL_USBPHYRSTB     BIT0    // 1 = USBRST# deasserted
#define PMIC_CHGRCTRL1                 0x4C    // pmic_ccsm.h
#define PMIC_CHGRCTRL1_OTGMODE         BIT6
#define PMIC_SCHGRIRQ1                 0x4F
#define PMIC_SCHGRIRQ1_SVBUSDET        BIT0
#define PMIC_VLDOCNT                   0xAF
#define PMIC_VLDOCNT_VUSBPHYEN         BIT2

//
// PMIC GPIO 6 control, which is wired to the SMB1357 charger's OTG (5V boost
// enable) pin on ZX551ML. SMB1357_OTG_PMIC_PIN in smb1357_charger.c: setting
// BIT5|BIT4|BIT0 drives the pin high, clearing them drives it low.
//
#define PMIC_GPIO6_CTLO                0x84
#define PMIC_GPIO6_CTLO_OTG_EN         (BIT5 | BIT4 | BIT0)

//
// The PMIC is reached over SCU IPC-1, which ScuIpcLib owns. The addresses
// below are PMIC register addresses, not IPC register offsets.
//

#endif // DWC3_REGS_H_
