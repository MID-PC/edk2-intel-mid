/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/AcpiTable.h>

#define CT_SIG_FACS  EFI_ACPI_6_5_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE
#define CT_SIG_FADT  EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE
#define CT_SIG_MADT  EFI_ACPI_6_5_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE
#define CT_SIG_DSDT  EFI_ACPI_6_5_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE
#define CT_SIG_DBG2  SIGNATURE_32 ('D', 'B', 'G', '2')
#define CT_SIG_CSRT  SIGNATURE_32 ('C', 'S', 'R', 'T')
#define CT_SIG_SSDT  SIGNATURE_32 ('S', 'S', 'D', 'T')

/**
  Install order validated against Windows; FACS and DSDT must precede FADT.
**/
STATIC CONST UINT32  mCtInstallOrder[] = {
  CT_SIG_FACS,
  CT_SIG_DSDT,
  CT_SIG_FADT,
  CT_SIG_MADT,
  CT_SIG_CSRT,
  CT_SIG_DBG2,
  CT_SIG_SSDT
};

/**
  Find a RAW section with a matching 4CC signature in one storage file and
  install it, freeing the FFS copy. Size comes from Header->Length because
  InstallAcpiTable requires Length to equal the passed size.
**/
STATIC
EFI_STATUS
CtInstallTableFromStorage (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable,
  IN CONST EFI_GUID           *StorageGuid,
  IN UINT32                   Signature
  )
{
  EFI_STATUS                   Status;
  UINTN                        Instance;
  UINTN                        SectionSize;
  VOID                         *Section;
  EFI_ACPI_DESCRIPTION_HEADER  *Header;
  UINTN                        TableKey;

  Instance = 0;
  while (TRUE) {
    Status = GetSectionFromAnyFv (
               StorageGuid,
               EFI_SECTION_RAW,
               Instance,
               &Section,
               &SectionSize
               );
    if (Status == EFI_NOT_FOUND) {
      return EFI_NOT_FOUND;
    }

    if (EFI_ERROR (Status) || (Section == NULL)) {
      DEBUG ((DEBUG_ERROR, "AcpiPlatform: ACPI section %u unreadable: %r\n", Instance, Status));
      return EFI_PROTOCOL_ERROR;
    }

    if (SectionSize >= sizeof (EFI_ACPI_DESCRIPTION_HEADER)) {
      Header = (EFI_ACPI_DESCRIPTION_HEADER *)Section;
      if (Header->Signature == Signature) {
        TableKey = 0;
        Status   = AcpiTable->InstallAcpiTable (
                                AcpiTable,
                                Section,
                                Header->Length,
                                &TableKey
                                );
        FreePool (Section);
        if (EFI_ERROR (Status)) {
          return Status;
        }

        DEBUG ((
          DEBUG_INFO,
          "AcpiPlatform: installed %c%c%c%c (%u bytes): %r\n",
          Signature & 0xFF,
          (Signature >> 8) & 0xFF,
          (Signature >> 16) & 0xFF,
          (Signature >> 24) & 0xFF,
          (UINT32)Header->Length,
          Status
          ));
        return EFI_SUCCESS;
      }
    }

    FreePool (Section);
    Instance++;
  }
}

/**
  Publish the platform ACPI table set: each signature is looked up in the
  device DSDT storage, then the SoC fixed-table storage; missing tables are
  skipped and never abort the set.
**/
EFI_STATUS
EFIAPI
AcpiPlatformEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS              Status;
  EFI_ACPI_TABLE_PROTOCOL *AcpiTable;
  UINTN                   Index;
  UINT32                  Signature;
  BOOLEAN                 InstalledAny;

  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&AcpiTable
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatform: no ACPI table protocol: %r\n", Status));
    return Status;
  }

  // FACS and DSDT MUST install before FADT
  InstalledAny = FALSE;
  for (Index = 0; Index < ARRAY_SIZE (mCtInstallOrder); Index++) {
    Signature = mCtInstallOrder[Index];

    Status = CtInstallTableFromStorage (AcpiTable, &gEfiAcpiTableStorageGuid, Signature);
    if (Status == EFI_NOT_FOUND) {
      Status = CtInstallTableFromStorage (AcpiTable, &gCloverviewAcpiTableStorageGuid, Signature);
    }

    if (Status == EFI_NOT_FOUND) {
      DEBUG ((
        DEBUG_INFO,
        "AcpiPlatform: table %c%c%c%c not published by this device; skipping\n",
        Signature & 0xFF,
        (Signature >> 8) & 0xFF,
        (Signature >> 16) & 0xFF,
        (Signature >> 24) & 0xFF
        ));
      continue;
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "AcpiPlatform: failed to install %c%c%c%c: %r\n",
        Signature & 0xFF,
        (Signature >> 8) & 0xFF,
        (Signature >> 16) & 0xFF,
        (Signature >> 24) & 0xFF,
        Status));
      return Status;
    }

    InstalledAny = TRUE;
  }

  if (!InstalledAny) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatform: no ACPI table found in any storage file\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_INFO, "AcpiPlatform: platform ACPI table set installed\n"));
  return EFI_SUCCESS;
}
