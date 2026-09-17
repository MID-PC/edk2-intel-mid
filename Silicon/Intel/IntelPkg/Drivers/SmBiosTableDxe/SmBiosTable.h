/** @file
  SMBIOS table templates for the Intel MID SMBIOS platform driver.

  Structure of the SMBIOS records mirrors the Mu-Silicium platform
  (Silicon/Silicium/SiliciumPkg/Drivers/SmBiosTableDxe): static templates with
  a parallel CHAR8* string pack, filled from FixedAtBuild PCDs at dispatch time.

  Device identity strings come from the Intel MID package PCDs
  (PcdSmbiosSystem*); the device DSC overrides them. CPU/SoC identity comes
  from the generation package (PcdSmbiosProcessor*).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SMBIOS_TABLE_H_
#define SMBIOS_TABLE_H_

#include <IndustryStandard/SmBios.h>
#include <Library/PcdLib.h>

typedef struct {
  SMBIOS_TABLE_TYPE0    *Table;
  CHAR8                 **Strings;
} SMBIOS_TABLE_ENTRY;

extern SMBIOS_TABLE_TYPE0  mSmbiosType0;
extern CHAR8               *mSmbiosType0Strings[];

extern SMBIOS_TABLE_TYPE1  mSmbiosType1;
extern CHAR8               *mSmbiosType1Strings[];

extern SMBIOS_TABLE_TYPE2  mSmbiosType2;
extern CHAR8               *mSmbiosType2Strings[];

extern SMBIOS_TABLE_TYPE3  mSmbiosType3;
extern CHAR8               *mSmbiosType3Strings[];

extern SMBIOS_TABLE_TYPE4  mSmbiosType4;
extern CHAR8               *mSmbiosType4Strings[];

extern SMBIOS_TABLE_TYPE7  mSmbiosType7L1I;
extern CHAR8               *mSmbiosType7L1IStrings[];

extern SMBIOS_TABLE_TYPE7  mSmbiosType7L1D;
extern CHAR8               *mSmbiosType7L1DStrings[];

extern SMBIOS_TABLE_TYPE7  mSmbiosType7L2;
extern CHAR8               *mSmbiosType7L2Strings[];

extern SMBIOS_TABLE_TYPE16 mSmbiosType16;
extern CHAR8               *mSmbiosType16Strings[];

extern SMBIOS_TABLE_TYPE17 mSmbiosType17;
extern CHAR8               *mSmbiosType17Strings[];

extern SMBIOS_TABLE_TYPE19 mSmbiosType19;
extern CHAR8               *mSmbiosType19Strings[];
#endif