/** @file
  Generic ACPI table installer for the Intel Atom Z25xx (Clover Trail+) SoC.

  Thin publisher: every table is a raw blob in the firmware volumes. This
  driver locates them, identifies them by their 4CC signature, and installs
  them through EFI_ACPI_TABLE_PROTOCOL in the exact order the platform
  validated against Windows:

    FACS -> DSDT -> FADT -> MADT -> CSRT -> DBG2 -> SSDT (PPM)

  The driver is SoC-generic: it installs whatever tables a device publishes
  and silently skips any that a device chose to exclude. Which tables exist is
  decided entirely by what the device packs into the two storage files:

    - SoC fixed tables (FACS/FADT/MADT/DBG2/CSRT) and the per-SKU \_PR PPM SSDT
      (SsdPpm<SKU>.aml) come from the .aml blobs in
      Silicon/Intel/CloverviewPkg/Acpi/AcpiTables/, packed by the device FDF
      into a FREEFORM file keyed on gCloverviewAcpiTableStorageGuid. A device
      that does not need a table (e.g. t00g omits CSRT, non-KDNET builds omit
      DBG2) simply does not list it, so the driver skips it.
    - The device DSDT is compiled from the platform's Dsdt.asl
      (Platforms/<dev>Pkg/AcpiTables/AcpiTables.inf, keyed on
      gEfiAcpiTableStorageGuid) and defines no \_PR: the processor PPM/PEP
      surface is the SSDT above, loaded add-only into the namespace.

  Windows' bootia32.efi/winload fails with 0xc0000225 ("the firmware (BIOS) is
  not ACPI compatible") when no ACPI tables are exposed at all. This driver
  installs the smallest set that satisfies the Windows ACPI loader: a
  hardware-reduced FADT (rev 5, 0xF4 bytes), its FACS, a four-thread MADT and
  the DSDT. The XSDT/RSDP are produced by MdeModulePkg AcpiTableDxe when the
  tables are installed through EFI_ACPI_TABLE_PROTOCOL.

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
  Table install order validated against Windows.

  FACS and DSDT must come before FADT (see AcpiPlatformEntryPoint): the
  remaining fixed tables are in stable, order-independent positions. The PPM
  SSDT is order-independent too and is installed last.
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
  Search one storage file for a RAW section whose 4CC header signature matches
  and install it through EFI_ACPI_TABLE_PROTOCOL.

  The RAW ACPI sections of the storage FREEFORM file are read until a blob
  whose 4CC header signature matches is found; it is then installed and the
  FFS copy freed. InstallAcpiTable requires Header.Length to equal the passed
  size (AcpiTableProtocol.c), so the size always comes from Header->Length.

  @param  AcpiTable    ACPI table protocol handle.
  @param  StorageGuid  GUID of the FREEFORM file to scan.
  @param  Signature    Header signature of the table to find.

  @retval EFI_SUCCESS          Table found and installed.
  @retval EFI_NOT_FOUND        No RAW section with that signature in this file.
  @retval other                The table could not be located or installed.
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
  Publish the platform ACPI table set.

  Every signature on the validated install list is looked up first in the
  device DSDT storage (gEfiAcpiTableStorageGuid) and then in the SoC fixed
  table storage (gCloverviewAcpiTableStorageGuid), and installed if present.
  Tables a device chose to exclude (not packed in FV) are skipped: the driver
  keeps going and never aborts the set because one entry is missing.
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

  //
  // IMPORTANT: FACS and DSDT are installed on the list above before FADT.
  // MdeModulePkg/Universal/Acpi/AcpiTableDxe special-cases the FADT: whenever
  // a FADT is added (or a FACS/DSDT is added later), it rewrites
  // Fadt->FirmwareCtrl/XFirmwareCtrl and Fadt->Dsdt/XDsdt from its own internal
  // Facs/Dsdt pointers. Installing FACS and DSDT first makes AcpiTableDxe fill
  // in the real addresses (it also relocates them into ACPI NVS / reclaim
  // memory itself). Windows then boots cleanly instead of bugchecking with
  // ACPI_BIOS_ERROR (0xA5) on a FADT whose DSDT/XFirmwareCtrl are zero.
  //
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