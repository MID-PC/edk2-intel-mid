/** @file
  Moorefield ACPI table installer.

  Publishes the tables packaged in gMoorefieldAcpiTableStorageGuid:
  FACS, DSDT, FADT, MADT, HPET and MCFG. The fixed tables are built from
  the .aslc sources in MoorefieldPkg/Acpi/AcpiTables and packed into the
  platform ACPI storage FFS together with the board DSDT.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/AcpiTable.h>

/**
  Install order: FACS and DSDT must precede the FADT that points at them.
**/
STATIC CONST UINT32  mMfInstallOrder[] = {
  EFI_ACPI_6_5_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE,
  EFI_ACPI_6_5_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
  EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
  EFI_ACPI_6_5_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
  EFI_ACPI_6_5_HIGH_PRECISION_EVENT_TIMER_TABLE_SIGNATURE,
  EFI_ACPI_6_5_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE
};

/**
  Find the first RAW section with the requested 4CC in the Moorefield
  storage file and install it. Size comes from Header->Length because
  InstallAcpiTable requires Length to equal the passed size.

  @param[in]  AcpiTable  ACPI table protocol instance.
  @param[in]  Signature  Table signature to look for.

  @retval EFI_SUCCESS    The table was installed.
  @retval EFI_NOT_FOUND  No section with that signature exists.
  @return                Errors from the FFS reader or the protocol.
**/
STATIC
EFI_STATUS
MfInstallTableFromStorage (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable,
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
               &gMoorefieldAcpiTableStorageGuid,
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
        Status   = AcpiTable->InstallAcpiTable (AcpiTable, Section, Header->Length, &TableKey);
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
  Publish the Moorefield ACPI table set.

  @param[in]  ImageHandle  Image handle of this driver.
  @param[in]  SystemTable  UEFI system table.

  @retval EFI_SUCCESS  The table set was installed.
  @return              Errors from the ACPI table protocol.
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

  InstalledAny = FALSE;
  for (Index = 0; Index < ARRAY_SIZE (mMfInstallOrder); Index++) {
    Signature = mMfInstallOrder[Index];

    Status = MfInstallTableFromStorage (AcpiTable, Signature);
    if (Status == EFI_NOT_FOUND) {
      DEBUG ((
        DEBUG_INFO,
        "AcpiPlatform: table %c%c%c%c not present in storage; skipping\n",
        Signature & 0xFF,
        (Signature >> 8) & 0xFF,
        (Signature >> 16) & 0xFF,
        (Signature >> 24) & 0xFF
        ));
      continue;
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "AcpiPlatform: failed to install %c%c%c%c: %r\n",
        Signature & 0xFF,
        (Signature >> 8) & 0xFF,
        (Signature >> 16) & 0xFF,
        (Signature >> 24) & 0xFF,
        Status
        ));
      return Status;
    }

    InstalledAny = TRUE;
  }

  if (!InstalledAny) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatform: no ACPI table found in the Moorefield storage file\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_INFO, "AcpiPlatform: Moorefield ACPI table set installed\n"));
  return EFI_SUCCESS;
}
