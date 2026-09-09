/** @file
  ACPI table installer for Intel Atom Z25xx (Clover Trail+, Asus ZenFone 6 T00G).

  Thin publisher: every table is a raw blob in the firmware volumes. This
  driver locates them, identifies them by their 4CC signature, and installs
  them through EFI_ACPI_TABLE_PROTOCOL in the exact order the platform
  validated against Windows:

    FACS -> DSDT -> FADT -> MADT -> (DBG2, KDNET-USB build only)

  Table ownership (Silicium model):
    - SoC fixed tables (FADT/FACS/MADT/DBG2) are .aslc sources in
      Silicon/Intel/CloverviewPkg/Acpi/AcpiTables/, packed into one FREEFORM
      FFS file keyed on gCloverviewAcpiTableStorageGuid.
    - The device DSDT is compiled from the platform's Dsdt.asl
      (Platforms/t00gPkg/AcpiPlatformDxe/AcpiTables.inf, keyed on
      gEfiAcpiTableStorageGuid); its \_PR block #includes the per-SKU PrZ*.asl
      fragments from the SoC package.

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
#define CT_SIG_DBG2  SIGNATURE_32 ('D', 'B', 'G', '2')

/**
  Install a fixed table through EFI_ACPI_TABLE_PROTOCOL.

  The RAW ACPI sections of the Cloverview FREEFORM file (gCloverviewAcpiTable
  StorageGuid) are read until a blob whose 4CC header signature matches is
  found; it is then installed and the FFS copy freed. InstallAcpiTable requires
  the Header.Length to equal the passed size (AcpiTableProtocol.c), so the
  size always comes from Header->Length.

  @param  AcpiTable    ACPI table protocol handle.
  @param  Signature    Header signature of the table to find (FACS/FACP/APIC/DBG2).

  @retval EFI_SUCCESS          Table found and installed.
  @retval EFI_NOT_FOUND        No RAW section with that signature was found.
  @retval other                The table could not be located or installed.
**/
STATIC
EFI_STATUS
CtInstallCloverviewTable (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable,
  IN UINT32                   Signature
  )
{
  EFI_STATUS    Status;
  UINTN         Instance;
  UINTN         SectionSize;
  VOID          *Section;
  EFI_ACPI_DESCRIPTION_HEADER  *Header;
  UINTN         TableKey;

  Instance = 0;
  while (TRUE) {
    Status = GetSectionFromAnyFv (
               &gCloverviewAcpiTableStorageGuid,
               EFI_SECTION_RAW,
               Instance,
               &Section,
               &SectionSize
               );
    if (Status == EFI_NOT_FOUND) {
      return EFI_NOT_FOUND;
    }

    if (EFI_ERROR (Status) || (Section == NULL)) {
      DEBUG ((DEBUG_ERROR, "AcpiPlatform: SoC ACPI section %u unreadable: %r\n", Instance, Status));
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
  Install the device DSDT located in the gEfiAcpiTableStorageGuid file.
**/
STATIC
EFI_STATUS
CtInstallDsdt (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable
  )
{
  EFI_STATUS    Status;
  UINTN         SectionSize;
  VOID          *Section;
  EFI_ACPI_DESCRIPTION_HEADER  *Dsdt;
  UINTN         TableKey;

  Section     = NULL;
  SectionSize = 0;
  Status      = GetSectionFromAnyFv (
                  &gEfiAcpiTableStorageGuid,
                  EFI_SECTION_RAW,
                  0,
                  &Section,
                  &SectionSize
                  );
  if (EFI_ERROR (Status) || (Section == NULL)) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatform: DSDT not found in FV: %r\n", Status));
    return EFI_NOT_FOUND;
  }

  Dsdt = (EFI_ACPI_DESCRIPTION_HEADER *)Section;
  if (Dsdt->Signature != EFI_ACPI_6_5_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatform: bad DSDT signature 0x%08x\n", Dsdt->Signature));
    FreePool (Section);
    return EFI_NOT_FOUND;
  }

  TableKey = 0;
  Status   = AcpiTable->InstallAcpiTable (AcpiTable, Section, Dsdt->Length, &TableKey);
  FreePool (Section);

  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "AcpiPlatform: installed DSDT (%u bytes): %r\n", (UINT32)Dsdt->Length, Status));
  return EFI_SUCCESS;
}

/**
  Publish the platform ACPI table set.
**/
EFI_STATUS
EFIAPI
AcpiPlatformEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;

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
  // IMPORTANT: FACS and DSDT must be installed through EFI_ACPI_TABLE_PROTOCOL,
  // not merely copied into ACPI NVS and pointed at from our FADT image.
  // MdeModulePkg/Universal/Acpi/AcpiTableDxe special-cases the FADT: whenever a
  // FADT is added (or a FACS/DSDT is added later), it rewrites
  // Fadt->FirmwareCtrl/XFirmwareCtrl and Fadt->Dsdt/XDsdt from its own internal
  // Facs/Dsdt pointers. Because we never registered a FACS or a DSDT with the
  // protocol, those internal pointers were NULL and AcpiTableDxe overwrote our
  // addresses with zero. Windows then found a FADT with DSDT == 0, failed ACPI
  // namespace initialization and bugchecked with ACPI_BIOS_ERROR (0xA5).
  // Installing FACS and DSDT first makes AcpiTableDxe fill in the real
  // addresses (it also relocates them into ACPI NVS / reclaim memory itself).
  //
  Status = CtInstallCloverviewTable (AcpiTable, CT_SIG_FACS);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = CtInstallDsdt (AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = CtInstallCloverviewTable (AcpiTable, CT_SIG_FADT);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = CtInstallCloverviewTable (AcpiTable, CT_SIG_MADT);
  if (EFI_ERROR (Status)) {
    return Status;
  }

#ifdef KDNET_USB
  //
  // DBG2 (Microsoft Debug Port 2), single-entry USB OTG debug port, based on
  // the Z2760 DBG2_.aml / "INTLDBGO" table (114 bytes). This is what lets
  // Windows' kernel debugger transport (kdnet.sys / usb debug port) bind the
  // Chipidea/ARC OTG core as a USB device controller instead of a serial port.
  // Only installed in the KDNET-USB build, where usb-host.asl is swapped for
  // usb-debug.asl so nothing forces the core into host mode.
  //
  Status = CtInstallCloverviewTable (AcpiTable, CT_SIG_DBG2);
  if (EFI_ERROR (Status)) {
    return Status;
  }
#endif

  DEBUG ((DEBUG_INFO, "AcpiPlatform: FACS/DSDT/FADT/MADT installed\n"));
  return EFI_SUCCESS;
}