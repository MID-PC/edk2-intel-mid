/** @file
  One candidate address shared by PEI reservation, ACPI and USB boot printer.
  SPDX-License-Identifier: BSD-2-Clause-Patent
  0x3EEFE000 is the old 1 GB board's trace address. It is also inside the
  supplied T00G System RAM map, but T00G reset retention is not yet proven.
**/
#ifndef CT_USB_TRACE_H_
#define CT_USB_TRACE_H_
#define CT_USB_TRACE_BASE       0x7FAFE000
#define CT_USB_TRACE_PAGE_SIZE  0x1000
#define CT_USB_TRACE_SIZE       0x40
#define CT_USB_TRACE_SIGNATURE  0x43415443 /* 'CTAC' */
#endif
