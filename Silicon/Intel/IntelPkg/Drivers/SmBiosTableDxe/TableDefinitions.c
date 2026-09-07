/** @file
  SMBIOS record templates and string packs for the Intel MID platform.

  The device identity strings are replaced from the Intel MID package PCDs
  (PcdSmbiosSystem*) and the CPU identity from the generation package
  (PcdSmbiosProcessor*) at driver dispatch time (see SmBiosTable.c). Every
  string slot is pre-filled with "Not Specified" so the structure string
  references stay valid regardless of which PCDs differ from their defaults.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/SmBios.h>
#include <Library/PcdLib.h>

#include "SmBiosTable.h"

SMBIOS_TABLE_TYPE0  mSmbiosType0 = {
  {
    SMBIOS_TYPE_BIOS_INFORMATION,
    sizeof (SMBIOS_TABLE_TYPE0),
    0
  },
  1,    // Vendor
  2,    // BiosVersion
  0,    // BiosSegment
  3,    // BiosReleaseDate
  0,    // BiosSize (set at runtime)
  { 0 },                            // BiosCharacteristics (bits set at runtime)
  { 0, 0 },                         // BIOSCharacteristicsExtensionBytes
  0,    // SystemBiosMajorRelease
  0,    // SystemBiosMinorRelease
  0,    // EmbeddedControllerFirmwareMajorRelease
  0,    // EmbeddedControllerFirmwareMinorRelease
  { 0 },                            // ExtendedBiosSize
};

CHAR8  *mSmbiosType0Strings[] = {
  "Not Specified", // Vendor
  "Not Specified", // BiosVersion
  "Not Specified", // BiosReleaseDate
  NULL
};

SMBIOS_TABLE_TYPE1  mSmbiosType1 = {
  {
    SMBIOS_TYPE_SYSTEM_INFORMATION,
    sizeof (SMBIOS_TABLE_TYPE1),
    0
  },
  1, // Manufacturer
  2, // ProductName
  3, // Version
  4, // SerialNumber
  { 0 }, // Uuid (set at runtime)
  SystemWakeupTypePowerSwitch, // WakeUpType
  5, // SKUNumber
  6, // Family
};

CHAR8  *mSmbiosType1Strings[] = {
  "Not Specified", // Manufacturer
  "Not Specified", // ProductName
  "Not Specified", // Version
  "Not Specified", // SerialNumber
  "Not Specified", // SKUNumber
  "Not Specified", // Family
  NULL
};

SMBIOS_TABLE_TYPE2  mSmbiosType2 = {
  {
    SMBIOS_TYPE_BASEBOARD_INFORMATION,
    15,               // fixed size: no contained object handles
    0
  },
  1,    // Manufacturer
  2,    // ProductName
  3,    // Version
  4,    // SerialNumber
  5,    // AssetTag
  { 0 },                // FeatureFlag
  6,    // LocationInChassis
  0,    // ChassisHandle (set at runtime)
  BaseBoardTypeMotherBoard,
  0,    // NumberOfContainedObjectHandles
  { 0 }, // ContainedObjectHandles (unused)
};

CHAR8  *mSmbiosType2Strings[] = {
  "Not Specified", // Manufacturer
  "Not Specified", // ProductName
  "Not Specified", // Version
  "Not Specified", // SerialNumber
  "Not Specified", // AssetTag
  "Not Specified", // LocationInChassis
  NULL
};

SMBIOS_TABLE_TYPE3  mSmbiosType3 = {
  {
    SMBIOS_TYPE_SYSTEM_ENCLOSURE,
    18,               // no ContainedElements
    0
  },
  1,                      // Manufacturer
  MiscChassisTablet,  // Type
  2,    // Version
  3,    // SerialNumber
  4,    // AssetTag
  ChassisStateSafe,       // BootupState
  ChassisStateSafe,       // PowerSupplyState
  ChassisStateSafe,       // ThermalState
  ChassisSecurityStatusNone,
  { 0, 0, 0, 0 },         // OemDefined
  0,    // Height
  0,    // NumberofPowerCords
  0,    // ContainedElementCount
  0,    // ContainedElementRecordLength
  { { 0, 0, 0 } },
};

CHAR8  *mSmbiosType3Strings[] = {
  "Not Specified", // Manufacturer
  "Not Specified", // Version
  "Not Specified", // SerialNumber
  "Not Specified", // AssetTag
  NULL
};

SMBIOS_TABLE_TYPE4  mSmbiosType4 = {
  {
    SMBIOS_TYPE_PROCESSOR_INFORMATION,
    sizeof (SMBIOS_TABLE_TYPE4),
    0
  },
  1,                  // Socket
  0x03,               // ProcessorType: Central Processor
  ProcessorFamilyUnknown, // ProcessorFamily
  2,                  // ProcessorManufacturer
  { { 0 }, { 0 } },   // ProcessorId (set at runtime)
  3,                  // ProcessorVersion
  { 0 },              // Voltage (unknown)
  100,                // ExternalClock (set at runtime from PcdFSBClock)
  0,                  // MaxSpeed (set at runtime)
  0,                  // CurrentSpeed (set at runtime)
  0,                  // Status (set at runtime)
  ProcessorUpgradeOther, // ProcessorUpgrade
  SMBIOS_HANDLE_PI_RESERVED, // L1CacheHandle
  SMBIOS_HANDLE_PI_RESERVED, // L2CacheHandle
  SMBIOS_HANDLE_PI_RESERVED, // L3CacheHandle
  4,                  // SerialNumber
  5,                  // AssetTag
  6,                  // PartNumber
  2,                  // CoreCount
  2,                  // EnabledCoreCount
  2,                  // ThreadCount
  BIT3 | BIT2,        // ProcessorCharacteristics: Multicore, 64-bit capable
  ProcessorFamilyUnknown, // ProcessorFamily2
  2,                  // CoreCount2
  2,                  // EnabledCoreCount2
  2,                  // ThreadCount2
  0,                  // ThreadEnabled
  7,                  // SocketType
};

CHAR8  *mSmbiosType4Strings[] = {
  "On Board",      // Socket
  "Intel",         // ProcessorManufacturer
  "Not Specified", // ProcessorVersion
  "Not Specified", // SerialNumber
  "Not Specified", // AssetTag
  "Not Specified", // PartNumber
  NULL
};

SMBIOS_TABLE_TYPE16 mSmbiosType16 = {
  {
    SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY,
    sizeof (SMBIOS_TABLE_TYPE16),
    0
  },
  MemoryArrayLocationSystemBoard, // Location
  MemoryArrayUseSystemMemory,     // Use
  MemoryErrorCorrectionNone,      // MemoryErrorCorrection
  0,                              // MaximumCapacity (set at runtime)
  0xFFFF,                         // MemoryErrorInformationHandle
  1,                              // NumberOfMemoryDevices
  0,                              // ExtendedMaximumCapacity
};

CHAR8  *mSmbiosType16Strings[] = {
  NULL
};

SMBIOS_TABLE_TYPE17 mSmbiosType17 = {
  {
    SMBIOS_TYPE_MEMORY_DEVICE,
    sizeof (SMBIOS_TABLE_TYPE17),
    0
  },
  0,         // MemoryArrayHandle (set at runtime)
  0xFFFF,    // MemoryErrorInformationHandle
  0,         // TotalWidth
  0,         // DataWidth
  0,         // Size (set at runtime)
  MemoryFormFactorRowOfChips,
  0,         // DeviceSet
  1,         // DeviceLocator
  2,         // BankLocator
  MemoryTypeLpddr2, // MemoryType
  { 0 },     // TypeDetail
  800,       // Speed
  3,         // Manufacturer
  4,         // SerialNumber
  5,         // AssetTag
  6,         // PartNumber
  0,         // Attributes
  0,         // ExtendedSize
  800,       // ConfiguredMemoryClockSpeed
  0,         // MinimumVoltage
  0,         // MaximumVoltage
  0,         // ConfiguredVoltage
  0,         // MemoryTechnology
  { { 0 } },  // MemoryOperatingModeCapability
  0,         // FirmwareVersion
  0,         // ModuleManufacturerID
  0,         // ModuleProductID
  0,         // MemorySubsystemControllerManufacturerID
  0,         // MemorySubsystemControllerProductID
  0,         // NonVolatileSize
  0,         // VolatileSize
  0,         // CacheSize
  0,         // LogicalSize
  0,         // ExtendedSpeed
  0,         // ExtendedConfiguredMemorySpeed
  0,         // Pmic0ManufacturerID
  0,         // Pmic0RevisionNumber
  0,         // RcdManufacturerID
  0,         // RcdRevisionNumber
};

CHAR8  *mSmbiosType17Strings[] = {
  "Not Specified", // DeviceLocator
  "Not Specified", // BankLocator
  "Not Specified", // Manufacturer
  "Not Specified", // SerialNumber
  "Not Specified", // AssetTag
  "Not Specified", // PartNumber
  NULL
};

SMBIOS_TABLE_TYPE19 mSmbiosType19 = {
  {
    SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS,
    sizeof (SMBIOS_TABLE_TYPE19),
    0
  },
  0,       // StartingAddress (set at runtime)
  0,       // EndingAddress (set at runtime)
  0,       // MemoryArrayHandle (set at runtime)
  0,       // PartitionWidth
  0,       // ExtendedStartingAddress
  0,       // ExtendedEndingAddress
};

CHAR8  *mSmbiosType19Strings[] = {
  NULL
};