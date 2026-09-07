/** @file
  Publishes SMBIOS records for the Intel MID platform (Type 0/1/2/3/4/16/17/19).

  Device identity is read from PcdSmbiosSystem* (set by the device DSC), the
  processor identity from the generation package PCDs, and the memory topology
  is derived from the EFI memory map.

  The driver is dispatched by a depex on the SMBIOS protocol, which is produced
  by MdeModulePkg/Universal/SmbiosDxe (the SMBIOS 3.0 entry point is chosen by
  PcdSmbiosVersion/PcdSmbiosEntryPointProvideMethod).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/Smbios.h>

#include "SmBiosTable.h"

STATIC CHAR8  mBiosVendor[64];
STATIC CHAR8  mBiosVersion[64];
STATIC CHAR8  mBuildDate[11];

STATIC CONST CHAR8  mAscIIMonth[12][4] = {
  "Jan",
  "Feb",
  "Mar",
  "Apr",
  "May",
  "Jun",
  "Jul",
  "Aug",
  "Sep",
  "Oct",
  "Nov",
  "Dec"
};

STATIC
VOID
ConvertFirmwareString (
  IN  CHAR16  *String,
  OUT CHAR8   *Buffer,
  IN  UINTN   BufferSize
  )
{
  AsciiSPrintUnicodeFormat (Buffer, BufferSize, L"%s", String);
}

STATIC
VOID
SetBuildDate (
  VOID
  )
{
  UINTN  Month;
  UINTN  Day;
  UINTN  i;

  Month = 1;
  for (i = 0; i < 12; i++) {
    if (AsciiStrnCmp (__DATE__, mAscIIMonth[i], 3) == 0) {
      Month = i + 1;
      break;
    }
  }

  if (__DATE__[4] == ' ') {
    Day = (UINTN)(__DATE__[5] - '0');
  } else {
    Day = (UINTN)((__DATE__[4] - '0') * 10 + (__DATE__[5] - '0'));
  }

  AsciiSPrint (
    mBuildDate,
    sizeof (mBuildDate),
    "%02d/%02d/%c%c%c%c",
    Month,
    Day,
    __DATE__[7],
    __DATE__[8],
    __DATE__[9],
    __DATE__[10]
    );
}

STATIC
VOID
UpdateSmBiosType0 (
  VOID
  )
{
  UINT32  FdSize;

  ConvertFirmwareString ((CHAR16 *)FixedPcdGetPtr (PcdFirmwareVendor), mBiosVendor, sizeof (mBiosVendor));
  ConvertFirmwareString ((CHAR16 *)FixedPcdGetPtr (PcdFirmwareVersionString), mBiosVersion, sizeof (mBiosVersion));
  SetBuildDate ();

  mSmbiosType0Strings[0] = mBiosVendor;
  mSmbiosType0Strings[1] = mBiosVersion;
  mSmbiosType0Strings[2] = mBuildDate;

  FdSize = FixedPcdGet32 (PcdFdSize);
  if (FdSize == 0) {
    FdSize = SIZE_2MB;
  }
  mSmbiosType0.BiosSize = (UINT8)((FdSize / SIZE_64KB) - 1);

  mSmbiosType0.BiosCharacteristics.PciIsSupported = 1;
  mSmbiosType0.BiosCharacteristics.BiosIsUpgradable = 1;
  mSmbiosType0.BiosCharacteristics.BiosShadowingAllowed = 1;
  mSmbiosType0.BiosCharacteristics.BootFromCdIsSupported = 1;
  mSmbiosType0.BiosCharacteristics.SelectableBootIsSupported = 1;
  mSmbiosType0.BIOSCharacteristicsExtensionBytes[0] = 0x01; // ACPI is supported
  mSmbiosType0.BIOSCharacteristicsExtensionBytes[1] = 0x08; // UEFI is supported
}

STATIC
VOID
UpdateSmBiosType1 (
  VOID
  )
{
  mSmbiosType1.Uuid = gIntelMidTokenSpaceGuid;
  mSmbiosType1Strings[0] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemManufacturer);
  mSmbiosType1Strings[1] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemModel);
  mSmbiosType1Strings[2] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemRetailModel);
  mSmbiosType1Strings[4] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemRetailSku);
  mSmbiosType1Strings[5] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemModel);
}

STATIC
VOID
UpdateSmBiosType2 (
  VOID
  )
{
  mSmbiosType2Strings[0] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemManufacturer);
  mSmbiosType2Strings[1] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemBoardModel);
}

STATIC
VOID
UpdateSmBiosType3 (
  VOID
  )
{
  mSmbiosType3Strings[0] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemManufacturer);
}

STATIC
VOID
UpdateSmBiosType4 (
  VOID
  )
{
  UINT32  CpuSignature;
  UINT32  CpuFeatureFlags;

  AsmCpuid (0x01, &CpuSignature, NULL, NULL, &CpuFeatureFlags);
  CopyMem (&mSmbiosType4.ProcessorId.Signature, &CpuSignature, sizeof (UINT32));
  CopyMem (&mSmbiosType4.ProcessorId.FeatureFlags, &CpuFeatureFlags, sizeof (UINT32));

  mSmbiosType4Strings[2] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosProcessorModel);
  mSmbiosType4Strings[5] = (CHAR8 *)FixedPcdGetPtr (PcdSmbiosProcessorPartNumber);

  mSmbiosType4.MaxSpeed     = FixedPcdGet16 (PcdSmbiosProcessorMaxSpeedMHz);
  mSmbiosType4.CurrentSpeed = FixedPcdGet16 (PcdSmbiosProcessorMaxSpeedMHz);
  mSmbiosType4.ExternalClock = (UINT16)(FixedPcdGet32 (PcdFSBClock) / 1000000);
  mSmbiosType4.Status        = 0x41; // Socket populated, CPU enabled
}

STATIC
UINT64
GetMemorySize (
  VOID
  )
{
  EFI_STATUS            Status;
  UINTN                 MapSize;
  UINTN                 MapKey;
  UINTN                 DescriptorSize;
  UINT32                DescriptorVersion;
  EFI_MEMORY_DESCRIPTOR *MemoryMap;
  EFI_MEMORY_DESCRIPTOR *MapWalker;
  UINT64                MemorySize;
  UINTN                 NumEntries;
  UINTN                 Index;

  MapSize       = 0;
  MemoryMap     = NULL;
  MemorySize    = 0;

  Status = gBS->GetMemoryMap (
                  &MapSize,
                  MemoryMap,
                  &MapKey,
                  &DescriptorSize,
                  &DescriptorVersion
                  );
  if (Status != EFI_BUFFER_TOO_SMALL) {
    DEBUG ((DEBUG_ERROR, "SmBiosTableDxe: GetMemoryMap #1 failed, Status = %r\n", Status));
    return 0;
  }

  MemoryMap = AllocatePool (MapSize);
  if (MemoryMap == NULL) {
    DEBUG ((DEBUG_ERROR, "SmBiosTableDxe: failed to allocate %d bytes for memory map\n", MapSize));
    return 0;
  }

  Status = gBS->GetMemoryMap (
                  &MapSize,
                  MemoryMap,
                  &MapKey,
                  &DescriptorSize,
                  &DescriptorVersion
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SmBiosTableDxe: GetMemoryMap #2 failed, Status = %r\n", Status));
    FreePool (MemoryMap);
    return 0;
  }

  NumEntries = MapSize / DescriptorSize;
  MapWalker  = MemoryMap;
  for (Index = 0; Index < NumEntries; Index++) {
    if (MapWalker->Type == EfiConventionalMemory) {
      MemorySize += LShiftU64 (MapWalker->NumberOfPages, EFI_PAGE_SHIFT);
    }
    MapWalker = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)MapWalker + DescriptorSize);
  }

  FreePool (MemoryMap);

  return MemorySize;
}

STATIC
VOID
UpdateSmBiosMemory (
  UINT64  MemorySize
  )
{
  UINT64  MemorySizeInMb;
  UINT64  MemorySizeInKb;

  if (MemorySize == 0) {
    return;
  }

  MemorySizeInMb = RShiftU64 (MemorySize, 20);
  MemorySizeInKb = RShiftU64 (MemorySize, 10);

  if (MemorySize >= SIZE_2TB) {
    mSmbiosType16.MaximumCapacity        = 0x80000000;
    mSmbiosType16.ExtendedMaximumCapacity = MemorySizeInKb;
  } else {
    mSmbiosType16.MaximumCapacity = (UINT32)MemorySizeInKb;
  }

  if (MemorySizeInMb >= 0x7FFF) {
    mSmbiosType17.Size        = 0x7FFF;
    mSmbiosType17.ExtendedSize = (UINT32)MemorySizeInMb;
  } else {
    mSmbiosType17.Size = (UINT16)MemorySizeInMb;
  }

  mSmbiosType19.StartingAddress = 0;
  mSmbiosType19.EndingAddress   = (UINT32)(MemorySizeInKb - 1);
}

STATIC EFI_SMBIOS_PROTOCOL  *mSmBiosProtocol = NULL;

STATIC
EFI_STATUS
RegisterTable (
  IN  EFI_SMBIOS_TABLE_HEADER  *TableHeader,
  IN  CHAR8                   **StringPack,
  OUT EFI_SMBIOS_HANDLE        *DataSmbiosHandle OPTIONAL
  )
{
  EFI_STATUS               Status;
  EFI_SMBIOS_HANDLE        SmBiosHandle;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  CHAR8                    *Strings;
  UINTN                    RecordSize;
  UINTN                    Index;

  //
  // The SMBIOS protocol expects the string pack as a contiguous byte area
  // directly after the formatted section, so assemble the record into a
  // fresh buffer of the right size instead of passing the template struct.
  //
  RecordSize = TableHeader->Length;
  if (StringPack == NULL || StringPack[0] == NULL) {
    // Two terminator bytes for a record without strings.
    RecordSize += 2;
  } else {
    for (Index = 0; StringPack[Index] != NULL; Index++) {
      RecordSize += AsciiStrSize (StringPack[Index]);
    }
    // One extra terminator byte between the strings and the final double-null.
    RecordSize += 1;
  }

  Record = AllocateZeroPool (RecordSize);
  if (Record == NULL) {
    DEBUG ((DEBUG_ERROR, "SmBiosTableDxe: failed to allocate %d bytes for SMBIOS record\n", RecordSize));
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (Record, TableHeader, TableHeader->Length);
  Strings = (CHAR8 *)Record + Record->Length;

  if (StringPack != NULL && StringPack[0] != NULL) {
    for (Index = 0; StringPack[Index] != NULL; Index++) {
      UINTN  StringSize = AsciiStrSize (StringPack[Index]);

      CopyMem (Strings, StringPack[Index], StringSize);
      Strings += StringSize;
    }
    *Strings = 0;
  } else {
    *Strings       = 0;
    *(Strings + 1) = 0;
  }

  SmBiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  Status = mSmBiosProtocol->Add (mSmBiosProtocol, gImageHandle, &SmBiosHandle, Record);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SmBiosTableDxe: Add type %d record failed, Status = %r\n", Record->Type, Status));
  } else if (DataSmbiosHandle != NULL) {
    *DataSmbiosHandle = SmBiosHandle;
  }

  FreePool (Record);

  return Status;
}

STATIC
EFI_STATUS
RegisterSmBiosTables (
  VOID
  )
{
  EFI_STATUS         Status;
  EFI_SMBIOS_HANDLE   MemoryArrayHandle;
  EFI_SMBIOS_HANDLE   ChassisHandle;

  //
  // Register the system/BIOS records first so the Type 2 chassis handle can
  // be linked. The Type 3 handle and the Type 16 memory array handle are only
  // known after the referenced record has been registered, so the memory
  // records are registered last.
  //
  Status = RegisterTable ((EFI_SMBIOS_TABLE_HEADER *)&mSmbiosType0, mSmbiosType0Strings, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = RegisterTable ((EFI_SMBIOS_TABLE_HEADER *)&mSmbiosType1, mSmbiosType1Strings, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = RegisterTable ((EFI_SMBIOS_TABLE_HEADER *)&mSmbiosType3, mSmbiosType3Strings, &ChassisHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mSmbiosType2.ChassisHandle = ChassisHandle;

  Status = RegisterTable ((EFI_SMBIOS_TABLE_HEADER *)&mSmbiosType2, mSmbiosType2Strings, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = RegisterTable ((EFI_SMBIOS_TABLE_HEADER *)&mSmbiosType4, mSmbiosType4Strings, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = RegisterTable ((EFI_SMBIOS_TABLE_HEADER *)&mSmbiosType16, mSmbiosType16Strings, &MemoryArrayHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mSmbiosType17.MemoryArrayHandle = MemoryArrayHandle;
  mSmbiosType19.MemoryArrayHandle = MemoryArrayHandle;

  Status = RegisterTable ((EFI_SMBIOS_TABLE_HEADER *)&mSmbiosType17, mSmbiosType17Strings, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = RegisterTable ((EFI_SMBIOS_TABLE_HEADER *)&mSmbiosType19, mSmbiosType19Strings, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SmBiosTableDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS           Status;
  UINT64               MemorySize;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&mSmBiosProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SmBiosTableDxe: LocateProtocol (gEfiSmbiosProtocolGuid) failed, Status = %r\n", Status));
    return Status;
  }

  MemorySize = GetMemorySize ();
  DEBUG ((DEBUG_INFO, "SmBiosTableDxe: memory size = %d MB\n", (UINTN)RShiftU64 (MemorySize, 20)));

  UpdateSmBiosType0 ();
  UpdateSmBiosType1 ();
  UpdateSmBiosType2 ();
  UpdateSmBiosType3 ();
  UpdateSmBiosType4 ();
  UpdateSmBiosMemory (MemorySize);

  return RegisterSmBiosTables ();
}