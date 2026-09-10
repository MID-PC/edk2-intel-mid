/** @file
  Old CloverTrailPkg USB trace print-before-clear mechanism.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include "../Include/UsbTrace.h"

#ifdef T00K_USB_TRACE
VOID
CtUsbTracePrintAndClear (
  VOID
  )
{
  UINTN Index;
  UINT32 Sig;
  Sig = MmioRead32 (CT_USB_TRACE_BASE);
  (VOID)Sig;
  DEBUG ((DEBUG_ERROR, "OtgHostDxe: ACPI trace @%08x sig=%08x STA=%u PS0=%u\n",
    CT_USB_TRACE_BASE, Sig, MmioRead32 (CT_USB_TRACE_BASE + 0x04),
    MmioRead32 (CT_USB_TRACE_BASE + 0x08)));
  if ((Sig == CT_USB_TRACE_SIGNATURE) && (MmioRead32 (CT_USB_TRACE_BASE + 8) != 0)) {
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: WinPS0 MODE=%08x HOSTPC1=%08x\n",
      MmioRead32 (CT_USB_TRACE_BASE + 0x18), MmioRead32 (CT_USB_TRACE_BASE + 0x1C)));
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: WinPS0 USBCMD=%08x USBSTS=%08x\n",
      MmioRead32 (CT_USB_TRACE_BASE + 0x20), MmioRead32 (CT_USB_TRACE_BASE + 0x24)));
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: WinPS0 PORTSC=%08x ULPIVP=%08x\n",
      MmioRead32 (CT_USB_TRACE_BASE + 0x28), MmioRead32 (CT_USB_TRACE_BASE + 0x2C)));
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: WinPS0 OTGSC=%08x TXFILL=%08x\n",
      MmioRead32 (CT_USB_TRACE_BASE + 0x30), MmioRead32 (CT_USB_TRACE_BASE + 0x34)));
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: WinPS0 USBINTR=%08x CONFIGFLAG=%08x\n",
      MmioRead32 (CT_USB_TRACE_BASE + 0x38), MmioRead32 (CT_USB_TRACE_BASE + 0x3C)));
  } else {
    DEBUG ((DEBUG_ERROR, "OtgHostDxe: no completed retained PS0 snapshot (check signature and STA count)\n"));
  }
  DEBUG_CODE_BEGIN ();
  MicroSecondDelay (5000000);
  DEBUG_CODE_END ();
  // Clear all 16 DWORDs, exactly like the old package. Only AML _STA/_PS0 writes
  // CTAC for the next attempt; a no-Windows cycle must not create a trace.
  for (Index = 0; Index < CT_USB_TRACE_SIZE; Index += 4) {
    MmioWrite32 (CT_USB_TRACE_BASE + Index, 0);
  }
}
#endif
