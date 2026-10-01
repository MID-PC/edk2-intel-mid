/** @file
  Moorefield (Silvermont / Z3580) platform ACPI table set.

  Builds FACS, FADT and MADT in memory and installs them together with the
  board DSDT, producing a hardware-reduced ACPI namespace that is sufficient
  for Windows to enumerate the four Silvermont cores and the MMIO devices the
  board describes in ASL. Windows starts the application processors itself
  with INIT/SIPI from the MADT; no firmware MP wake-up is involved.

  FACS and DSDT are installed before FADT; AcpiTableDxe patches the FADT
  FIRMWARE_CTRL / X_FIRMWARE_CTRL and DSDT / X_DSDT pointers every time the
  table set is published, so they are left zero here.

  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <IndustryStandard/MemoryMappedConfigurationSpaceAccessTable.h>
#include <Protocol/AcpiTable.h>

///
/// FADT.PM1xxxBlock, PM2Block, PMTimerBlock, GPE0Block and GPE1Block are all
/// zero: the part has no ACPI fixed hardware and the FADT advertises
/// HARDWARE_REDUCED_ACPI, so the OS ignores them entirely.
///
/// WBINVD is set, not cleared: the bit reads "the WBINVD instruction works
/// properly", so the OS may use it to flush and invalidate the boot-loader
/// hand-off caches at ExitBootServices. Every other bit here is from the
/// valid hardware-reduced mask in DynamicTablesPkg's AcpiFadtLib.
///
#define MF_FADT_FLAGS  (EFI_ACPI_6_5_WBINVD | EFI_ACPI_6_5_SLP_BUTTON | \
                        EFI_ACPI_6_5_HW_REDUCED_ACPI | \
                        EFI_ACPI_6_5_LOW_POWER_S0_IDLE_CAPABLE)

///
/// The GFX path is a fixed-mode linear framebuffer handed over through the
/// Graphics Output Protocol, so there is no legacy VGA framebuffer to keep the
/// OS away from. The SoC has no CMOS RTC, so say so rather than let the OS
/// probe for one that cannot be there.
///
#define MF_FADT_BOOT_ARCH_FLAGS  (EFI_ACPI_6_5_VGA_NOT_PRESENT | \
                                  EFI_ACPI_6_5_CMOS_RTC_NOT_PRESENT)

///
/// Z3580 is a tablet-class part: no S3/S4 and no legacy cooling or docking
/// model for the OS to honour.
///
#define MF_PREFERRED_PM_PROFILE  EFI_ACPI_6_5_PM_PROFILE_TABLET

///
/// I/O APIC identifier. Moorefield exposes a single I/O APIC; there is no
/// second GSI range to offset into.
///
#define MF_IO_APIC_ID  0x00

#define MF_OEM_ID  { 'I', 'N', 'T', 'E', 'L', ' ' }
#define MF_OEM_TABLE_ID  SIGNATURE_32 ('M', 'O', 'O', 'R')

///
/// Z3580 has four cores in two modules. The SFI CPUS table on the device
/// (ZX551ML/tables/CPUS) lists their APIC IDs as 0, 2, 4 and 6 - the odd IDs
/// are the absent SMT siblings - so the IDs are not consecutive. The
/// ACPI processor UID is the logical index and must agree with \_SB.CPUn._UID
/// in Dsdt.asl.
///
#define MF_CPU_COUNT  4

/**
  MADT with all four cores online. This mirrors the ACPI 6.5 APIC table
  layout directly - common header, LocalApicAddress, Flags, then the APIC
  structures packed back to back - so the whole table is a single object.
**/
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER                   Header;
  UINT32                                        LocalApicAddress;
  UINT32                                        Flags;
  EFI_ACPI_6_5_PROCESSOR_LOCAL_APIC_STRUCTURE   LocalApic[MF_CPU_COUNT];
  EFI_ACPI_6_5_IO_APIC_STRUCTURE                IoApic;
} MOOREFIELD_MADT;

/**
  ACPI 2.0+ "HPET" IA-PC High Precision Event Timer Table.

  MdePkg provides the signature but no structure for this table, so it is
  declared here. The OS needs this table in addition to the PNP0103 device in
  the DSDT: the device tells it the block is there, this table is what it
  actually programs. Without it ACPI.sys has no way to find the counter and
  falls back to the APIC timer.

  It is required because the FADT is hardware-reduced (no PM timer block) and
  the SoC has no CMOS RTC, so the HPET is the only ACPI-visible time source.
**/
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER                 Header;
  UINT32                                      SequenceNumber;
  UINT16                                      MinimumTick;
  EFI_ACPI_6_5_GENERIC_ADDRESS_STRUCTURE      BaseAddress;
  UINT8                                       Flags;
  UINT8                                       Reserved;
} MOOREFIELD_HPET;

/**
  ACPI "MCFG" PCI Express memory-mapped configuration space table with one
  allocation structure. MCFG is what lets Linux use the ECAM window that the
  PCI0 root bridge in Dsdt.asl reserves through its PDRC device.
**/
#pragma pack(1)
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER                                                    Header;
  UINT64                                                                         Reserved;
  EFI_ACPI_MEMORY_MAPPED_ENHANCED_CONFIGURATION_SPACE_BASE_ADDRESS_ALLOCATION_STRUCTURE  Segment;
} MOOREFIELD_MCFG;
#pragma pack()

#define MF_HPET_REVISION  0x01

///
/// The HPET is the OS's only clock here, so declare it as such. Bit 0 of Flags
/// is the only defined bit: clear means the block is in system memory, which
/// is how the PNP0103 _CRS in Dsdt.asl describes 0xFED00000.
///
#define MF_HPET_FLAGS  0x00

STATIC CONST EFI_ACPI_6_5_FIRMWARE_ACPI_CONTROL_STRUCTURE  mFacs = {
  .Signature = EFI_ACPI_6_5_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE,
  .Length    = sizeof (EFI_ACPI_6_5_FIRMWARE_ACPI_CONTROL_STRUCTURE),
  .Version   = EFI_ACPI_6_5_FIRMWARE_ACPI_CONTROL_STRUCTURE_VERSION
};

//
// Every PM1xxx/PM2/PMTimer/GPE block stays zero: see MF_FADT_FLAGS.
//
STATIC CONST EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE  mFadt = {
  .Header = {
    .Signature       = EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
    .Length          = sizeof (EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE),
    .Revision        = EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE_REVISION,
    .OemId           = MF_OEM_ID,
    .OemTableId      = MF_OEM_TABLE_ID,
    .OemRevision     = 0x00000001,
    .CreatorId       = SIGNATURE_32 ('I', 'N', 'T', 'L'),
    .CreatorRevision = 0x000C0000
  },
  .PreferredPmProfile = MF_PREFERRED_PM_PROFILE,
  .IaPcBootArch       = MF_FADT_BOOT_ARCH_FLAGS,
  .Flags             = MF_FADT_FLAGS,
  .MinorVersion      = 0x05  // ACPI 6.5
  //
  // FirmwareCtrl, Dsdt and every register block are left zero. AcpiTableDxe
  // fills in FirmwareCtrl/XFirmwareCtrl and Dsdt/XDsdt from the FACS and DSDT
  // it installs, and the OS ignores the register blocks in hardware-reduced
  // mode.
  //
};

#define MF_LOCAL_APIC(Uid, Id)  {                                          \
      .Type             = EFI_ACPI_6_5_PROCESSOR_LOCAL_APIC,               \
      .Length           = sizeof (EFI_ACPI_6_5_PROCESSOR_LOCAL_APIC_STRUCTURE), \
      .AcpiProcessorUid = (Uid),  /* Must match \_SB.CPUn._UID */          \
      .ApicId           = (Id),                                            \
      .Flags            = EFI_ACPI_6_5_LOCAL_APIC_ENABLED                  \
    }

STATIC CONST MOOREFIELD_MADT  mMadt = {
  .Header = {
    .Signature       = EFI_ACPI_6_5_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
    .Length          = sizeof (MOOREFIELD_MADT),
    .Revision        = EFI_ACPI_6_5_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION,
    .OemId           = MF_OEM_ID,
    .OemTableId      = MF_OEM_TABLE_ID,
    .OemRevision     = 0x00000001,
    .CreatorId       = SIGNATURE_32 ('I', 'N', 'T', 'L'),
    .CreatorRevision = 0x000C0000
  },
  .LocalApicAddress = FixedPcdGet32 (PcdLocalApicBase),
  .LocalApic        = {
    MF_LOCAL_APIC (0, 0),
    MF_LOCAL_APIC (1, 2),
    MF_LOCAL_APIC (2, 4),
    MF_LOCAL_APIC (3, 6)
  },
  .IoApic = {
    .Type                      = EFI_ACPI_6_5_IO_APIC,
    .Length                    = sizeof (EFI_ACPI_6_5_IO_APIC_STRUCTURE),
    .IoApicId                  = MF_IO_APIC_ID,
    .IoApicAddress             = FixedPcdGet32 (PcdIoApicBase),
    .GlobalSystemInterruptBase = 0
  }
};

//
// PcdHpetSize records the real size of the register block for other
// firmware modules; the HPET table itself only carries the address. Keep the
// two in step by having the build check the address, and note the size is the
// PNP0103 _CRS in Dsdt.asl.
//
STATIC CONST MOOREFIELD_HPET  mHpet = {
  .Header = {
    .Signature       = EFI_ACPI_6_5_HIGH_PRECISION_EVENT_TIMER_TABLE_SIGNATURE,
    .Length          = sizeof (MOOREFIELD_HPET),
    .Revision        = MF_HPET_REVISION,
    .OemId           = MF_OEM_ID,
    .OemTableId      = MF_OEM_TABLE_ID,
    .OemRevision     = 0x00000001,
    .CreatorId       = SIGNATURE_32 ('I', 'N', 'T', 'L'),
    .CreatorRevision = 0x000C0000
  },
  .SequenceNumber = 0x00000000,
  //
  // Main counter tick in units of 1e-7 seconds. 1 means a 10 MHz (100 ns)
  // counter. The field is advisory: the OS reads the true period from the
  // counter's capability register and clamps the programmed period to the low
  // 32 bits, which are valid on every HPET. Confirm against the real Moorefield
  // HPET frequency if a timebase-sensitive consumer ever shows up.
  //
  .MinimumTick = 0x0001,
  .BaseAddress = {
    .AddressSpaceId     = EFI_ACPI_6_5_SYSTEM_MEMORY,
    .RegisterBitWidth   = 32,
    .RegisterBitOffset  = 0,
    .AccessSize         = 4,
    .Address            = FixedPcdGet64 (PcdHpetBase)
  },
  .Flags    = MF_HPET_FLAGS,
  .Reserved = 0
};

///
/// One segment, bus 0 only, as in the SFI MCFG table on the device
/// (ZX551ML/tables/MCFG): base 0x7F600000, segment 0, buses 00-00. Bus 0 is
/// 1 MB of ECAM space, which is what PDRC in Dsdt.asl reserves.
///
STATIC CONST MOOREFIELD_MCFG  mMcfg = {
  .Header = {
    .Signature       = EFI_ACPI_6_5_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE,
    .Length          = sizeof (MOOREFIELD_MCFG),
    .Revision        = EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_SPACE_ACCESS_TABLE_REVISION,
    .OemId           = MF_OEM_ID,
    .OemTableId      = MF_OEM_TABLE_ID,
    .OemRevision     = 0x00000001,
    .CreatorId       = SIGNATURE_32 ('I', 'N', 'T', 'L'),
    .CreatorRevision = 0x000C0000
  },
  .Segment = {
    .BaseAddress                = FixedPcdGet32 (PcdPciMmcfgBase),
    .PciSegmentGroupNumber      = 0,
    .StartBusNumber             = 0,
    .EndBusNumber               = 0
  }
};

/**
  Fill in the 8-bit checksum of a writable ACPI table.

  AcpiTableDxe recomputes the checksum on install anyway, so this is not
  load-bearing. It is done here so that each table is already well formed
  the moment it leaves this module, which keeps the tables debuggable on
  their own and makes an accidental length change show up as a bad checksum
  rather than going unnoticed.

  @param[in, out]  Table  Table to checksum, in writable memory.
**/
STATIC
VOID
MfChecksumTable (
  IN OUT EFI_ACPI_DESCRIPTION_HEADER  *Table
  )
{
  UINT8  *Bytes;
  UINT8   Sum;
  UINTN   Index;

  Table->Checksum = 0;

  Sum   = 0;
  Bytes = (UINT8 *)Table;
  for (Index = 0; Index < Table->Length; Index++) {
    Sum = (UINT8)(Sum + Bytes[Index]);
  }

  Table->Checksum = (UINT8)(0x100U - Sum);
}

/**
  Install one statically built table, filling in its checksum first.

  @param[in]  AcpiTable  ACPI table protocol instance.
  @param[in]  Template   Read-only template to copy and install.

  @retval EFI_SUCCESS  The table was installed.
  @return               Errors from the memory allocator or the protocol.
**/
STATIC
EFI_STATUS
MfInstallTemplate (
  IN EFI_ACPI_TABLE_PROTOCOL         *AcpiTable,
  IN CONST EFI_ACPI_DESCRIPTION_HEADER  *Template
  )
{
  EFI_STATUS                   Status;
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  UINTN                        TableKey;

  Table = AllocateCopyPool (Template->Length, Template);
  if (Table == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // The FACS has no checksum byte: it is a bare signature/length pair followed
  // by platform fields, not a common-header table. MfChecksumTable() would treat
  // it as an EFI_ACPI_DESCRIPTION_HEADER and write at offset 9, which is inside
  // HardwareSignature (offset 8), corrupting it. AcpiTableDxe does not compute a
  // checksum for the FACS either, so it has to be installed untouched.
  //
  if (Template->Signature != EFI_ACPI_6_5_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE) {
    MfChecksumTable (Table);
  }

  TableKey = 0;
  Status   = AcpiTable->InstallAcpiTable (AcpiTable, Table, Table->Length, &TableKey);
  FreePool (Table);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "AcpiPlatform: installed %c%c%c%c revision %u, %u bytes\n",
    Template->Signature & 0xFF,
    (Template->Signature >> 8) & 0xFF,
    (Template->Signature >> 16) & 0xFF,
    (Template->Signature >> 24) & 0xFF,
    Template->Revision,
    Template->Length
    ));

  return EFI_SUCCESS;
}

/**
  Install the board DSDT from the platform ACPI storage file.

  @param[in]  AcpiTable  ACPI table protocol instance.

  @retval EFI_SUCCESS           The DSDT was installed.
  @retval EFI_NOT_FOUND         No DSDT section in the storage file.
  @return                       Errors from the FFS reader or the protocol.
**/
STATIC
EFI_STATUS
MfInstallDsdt (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable
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
      DEBUG ((DEBUG_ERROR, "AcpiPlatform: no DSDT in the platform ACPI storage file\n"));
      return Status;
    }

    if (EFI_ERROR (Status) || (Section == NULL)) {
      DEBUG ((DEBUG_ERROR, "AcpiPlatform: ACPI section %u unreadable: %r\n", Instance, Status));
      return EFI_PROTOCOL_ERROR;
    }

    if (SectionSize >= sizeof (EFI_ACPI_DESCRIPTION_HEADER)) {
      Header = (EFI_ACPI_DESCRIPTION_HEADER *)Section;
      if (Header->Signature == EFI_ACPI_6_5_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) {
        TableKey = 0;
        Status   = AcpiTable->InstallAcpiTable (AcpiTable, Section, Header->Length, &TableKey);
        FreePool (Section);
        if (EFI_ERROR (Status)) {
          return Status;
        }

        DEBUG ((
          DEBUG_INFO,
          "AcpiPlatform: installed DSDT revision %u, %u bytes\n",
          Header->Revision,
          Header->Length
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

  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&AcpiTable
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AcpiPlatform: no ACPI table protocol: %r\n", Status));
    return Status;
  }

  // FACS and DSDT must be in place before the FADT that points at them.
  Status = MfInstallTemplate (AcpiTable, (CONST EFI_ACPI_DESCRIPTION_HEADER *)&mFacs);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = MfInstallDsdt (AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = MfInstallTemplate (AcpiTable, (CONST EFI_ACPI_DESCRIPTION_HEADER *)&mFadt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = MfInstallTemplate (AcpiTable, (CONST EFI_ACPI_DESCRIPTION_HEADER *)&mMadt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = MfInstallTemplate (AcpiTable, (CONST EFI_ACPI_DESCRIPTION_HEADER *)&mHpet);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = MfInstallTemplate (AcpiTable, (CONST EFI_ACPI_DESCRIPTION_HEADER *)&mMcfg);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "AcpiPlatform: Moorefield ACPI table set installed\n"));
  return EFI_SUCCESS;
}

// The HPET table is a fixed 0x38 bytes by definition, and its Generic Address
// Structure has to be 8-byte aligned inside it, which only holds because the
// fields in MOOREFIELD_HPET are in the order above. If either ever stops being
// true the table is silently malformed, so pin both down here.
STATIC_ASSERT (
  sizeof (MOOREFIELD_HPET) == 0x38,
  "HPET table must be 0x38 bytes"
  );
STATIC_ASSERT (
  OFFSET_OF (MOOREFIELD_HPET, BaseAddress) == 0x2A,
  "HPET Generic Address Structure must start at offset 0x2A"
  );
