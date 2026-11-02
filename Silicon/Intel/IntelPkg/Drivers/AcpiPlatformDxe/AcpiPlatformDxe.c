#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/AcpiTable.h>

#define ACPI_SIG_FACS  EFI_ACPI_5_0_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE
#define ACPI_SIG_FADT  EFI_ACPI_5_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE
#define ACPI_SIG_MADT  EFI_ACPI_5_0_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE
#define ACPI_SIG_HPET  EFI_ACPI_5_0_HIGH_PRECISION_EVENT_TIMER_TABLE_SIGNATURE
#define ACPI_SIG_DSDT  EFI_ACPI_5_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE
#define ACPI_SIG_DBG2  EFI_ACPI_5_0_DEBUG_PORT_2_TABLE_SIGNATURE
#define ACPI_SIG_CSRT  EFI_ACPI_5_0_CORE_SYSTEM_RESOURCE_TABLE_SIGNATURE
#define ACPI_SIG_MCFG  EFI_ACPI_5_0_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE
#define ACPI_SIG_SSDT  EFI_ACPI_5_0_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE

//
// Install order validated against Windows; FACS and DSDT must precede FADT.
// Tables missing from a platform are skipped with a warning.
//
STATIC CONST UINT32  mAcpiInstallOrder[] = {
  ACPI_SIG_FACS,
  ACPI_SIG_DSDT,
  ACPI_SIG_FADT,
  ACPI_SIG_MADT,
  ACPI_SIG_HPET,
  ACPI_SIG_MCFG,
  ACPI_SIG_CSRT,
  ACPI_SIG_DBG2,
  ACPI_SIG_SSDT
};

//
// Install the first RAW section with a matching signature from StorageGuid.
// Size comes from Header->Length because InstallAcpiTable requires it.
//
STATIC
EFI_STATUS
AcpiInstallTableFromStorage (
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

  // Skip disabled storage
  if ((StorageGuid == NULL) || IsZeroGuid (StorageGuid)) {
    return EFI_NOT_FOUND;
  }

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
      DEBUG ((EFI_D_ERROR, "AcpiPlatform: ACPI section %u unreadable: %r\n", Instance, Status));
      return EFI_PROTOCOL_ERROR;
    }

    if (SectionSize >= sizeof (EFI_ACPI_DESCRIPTION_HEADER)) {
      Header = (EFI_ACPI_DESCRIPTION_HEADER *)Section;
      if (Header->Signature == Signature) {
        // Install table
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
          EFI_D_INFO,
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

//
// Publish the platform ACPI table set.
// Missing or failed tables are warnings; the rest are still installed.
//
EFI_STATUS
EFIAPI
AcpiPlatformEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS               Status;
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  CONST EFI_GUID           *TableStorageGuid;
  CONST EFI_GUID           *Dbg2StorageGuid;
  UINTN                    Index;
  UINT32                   Signature;
  BOOLEAN                  InstalledAny;

  // Locate the ACPI table protocol
  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&AcpiTable
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "AcpiPlatform: no ACPI table protocol: %r\n", Status));
    return Status;
  }

  // Get configured storage files
  TableStorageGuid = PcdGetPtr (PcdAcpiTableStorageFile);
  Dbg2StorageGuid  = PcdGetPtr (PcdAcpiDbg2TableStorageGuid);

  // FACS and DSDT MUST install before FADT
  InstalledAny = FALSE;
  for (Index = 0; Index < ARRAY_SIZE (mAcpiInstallOrder); Index++) {
    Signature = mAcpiInstallOrder[Index];

    Status = AcpiInstallTableFromStorage (AcpiTable, TableStorageGuid, Signature);
    if ((Status == EFI_NOT_FOUND) && (Signature == ACPI_SIG_DBG2)) {
      Status = AcpiInstallTableFromStorage (AcpiTable, Dbg2StorageGuid, Signature);
    }

    if (Status == EFI_NOT_FOUND) {
      DEBUG ((
        EFI_D_WARN,
        "AcpiPlatform: table %c%c%c%c not found; skipping\n",
        Signature & 0xFF,
        (Signature >> 8) & 0xFF,
        (Signature >> 16) & 0xFF,
        (Signature >> 24) & 0xFF
        ));
      continue;
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((
        EFI_D_WARN,
        "AcpiPlatform: failed to install %c%c%c%c: %r; skipping\n",
        Signature & 0xFF,
        (Signature >> 8) & 0xFF,
        (Signature >> 16) & 0xFF,
        (Signature >> 24) & 0xFF,
        Status
        ));
      continue;
    }

    InstalledAny = TRUE;
  }

  // Fail if nothing was installed
  if (!InstalledAny) {
    DEBUG ((EFI_D_ERROR, "AcpiPlatform: no ACPI table found in any storage file\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((EFI_D_INFO, "AcpiPlatform: platform ACPI table set installed\n"));
  return EFI_SUCCESS;
}
