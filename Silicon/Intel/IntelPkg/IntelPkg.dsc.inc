## @file
#  IntelPkg.dsc.inc - common Intel MID platform DSC fragment
#
#  Library stack and PCD policy shared by all Intel MID (Atom) platforms.
#  Included from the platform DSC; third-party devices override the
#  device-specific PCDs after the include.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[BuildOptions]
  GCC:DEBUG_*_*_CC_FLAGS         = -UMDEPKG_NDEBUG -Og
  GCC:RELEASE_*_*_CC_FLAGS       = -DMDEPKG_NDEBUG
  #
  # The image is entered directly at its first byte, so no ELF/PE loader is in
  # play for SEC: keep it position dependent and 32 bit.
  #
  GCC:*_*_IA32_CC_FLAGS          = -march=i686 -mtune=atom -fno-pic -fno-pie
  GCC:*_*_IA32_DLINK_FLAGS       = -no-pie -z common-page-size=0x1000

################################################################################
#
# Library classes
#
################################################################################
[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  # DXE-phase default: real PCD protocol client. BasePcdLibNull returns 0 for
  # every PcdGetXx(), which silently zeroes stack sizes, timeouts and the
  # framebuffer PCDs. SEC keeps the Null instance (no PPI available yet).
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf
  CpuLib|MdePkg/Library/BaseCpuLib/BaseCpuLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  PciLib|MdePkg/Library/BasePciLibCf8/BasePciLibCf8.inf
  PciCf8Lib|MdePkg/Library/BasePciCf8Lib/BasePciCf8Lib.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
  CacheMaintenanceLib|MdePkg/Library/BaseCacheMaintenanceLib/BaseCacheMaintenanceLib.inf
  UefiDecompressLib|MdePkg/Library/BaseUefiDecompressLib/BaseUefiDecompressLib.inf
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  TimerLib|UefiCpuPkg/Library/SecPeiDxeTimerLibUefiCpu/SecPeiDxeTimerLibUefiCpu.inf
  SafeIntLib|MdePkg/Library/BaseSafeIntLib/BaseSafeIntLib.inf
  RngLib|MdePkg/Library/BaseRngLibTimerLib/BaseRngLibTimerLib.inf
  OrderedCollectionLib|MdePkg/Library/BaseOrderedCollectionRedBlackTreeLib/BaseOrderedCollectionRedBlackTreeLib.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  StackCheckLib|MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  StackCheckFailureHookLib|MdePkg/Library/StackCheckFailureHookLibNull/StackCheckFailureHookLibNull.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  CcExitLib|UefiCpuPkg/Library/CcExitLibNull/CcExitLibNull.inf
  MicrocodeLib|UefiCpuPkg/Library/MicrocodeLib/MicrocodeLib.inf
  AmdSvsmLib|UefiCpuPkg/Library/AmdSvsmLibNull/AmdSvsmLibNull.inf
  LocalApicLib|UefiCpuPkg/Library/BaseXApicLib/BaseXApicLib.inf
  NestedInterruptTplLib|OvmfPkg/Library/NestedInterruptTplLib/NestedInterruptTplLib.inf
  MtrrLib|UefiCpuPkg/Library/MtrrLib/MtrrLib.inf
  CpuExceptionHandlerLib|UefiCpuPkg/Library/CpuExceptionHandlerLib/DxeCpuExceptionHandlerLib.inf
  SmmCpuRendezvousLib|UefiCpuPkg/Library/SmmCpuRendezvousLib/SmmCpuRendezvousLib.inf
  #
  # Full DxeMpInitLib: firmware brings the APs up via INIT-SIPI-SIPI so that
  # EFI_MP_SERVICES_PROTOCOL is genuinely multiprocessor. This is required for
  # SMP-aware EFI applications (e.g. CSMWrap reserves a core). The below-1MB
  # wakeup buffer is reserved (see DxeMpLib.c GetWakeupBuffer) so that the OS
  # does not reclaim it during its own AP bring-up.
  #
  MpInitLib|UefiCpuPkg/Library/MpInitLib/DxeMpInitLib.inf
  CpuPageTableLib|UefiCpuPkg/Library/CpuPageTableLib/CpuPageTableLib.inf
  SfiMemoryMapLib|Silicon/Intel/IntelPkg/Library/SfiMemoryMapLib/SfiMemoryMapLib.inf

  #
  # Debug Libraries
  #
!if $(TARGET) == DEBUG
  SerialPortLib|Silicon/Intel/IntelPkg/Library/FrameBufferSerialPortLib/FrameBufferSerialPortLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
!else
  SerialPortLib|MdePkg/Library/BaseSerialPortLibNull/BaseSerialPortLibNull.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!endif

  ReportStatusCodeLib|MdePkg/Library/BaseReportStatusCodeLibNull/BaseReportStatusCodeLibNull.inf
  PerformanceLib|MdePkg/Library/BasePerformanceLibNull/BasePerformanceLibNull.inf
  DebugAgentLib|MdeModulePkg/Library/DebugAgentLibNull/DebugAgentLibNull.inf
  PlatformHookLib|MdeModulePkg/Library/BasePlatformHookLibNull/BasePlatformHookLibNull.inf
  ResetSystemLib|MdeModulePkg/Library/BaseResetSystemLibNull/BaseResetSystemLibNull.inf
  CustomizedDisplayLib|MdeModulePkg/Library/CustomizedDisplayLib/CustomizedDisplayLib.inf
  FileExplorerLib|MdeModulePkg/Library/FileExplorerLib/FileExplorerLib.inf
  ImagePropertiesRecordLib|MdeModulePkg/Library/ImagePropertiesRecordLib/ImagePropertiesRecordLib.inf
  VariablePolicyHelperLib|MdeModulePkg/Library/VariablePolicyHelperLib/VariablePolicyHelperLib.inf
  VariablePolicyLib|MdeModulePkg/Library/VariablePolicyLib/VariablePolicyLib.inf
  VarCheckLib|MdeModulePkg/Library/VarCheckLib/VarCheckLib.inf
  AuthVariableLib|MdeModulePkg/Library/AuthVariableLibNull/AuthVariableLibNull.inf
  TpmMeasurementLib|MdeModulePkg/Library/TpmMeasurementLibNull/TpmMeasurementLibNull.inf
  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf
  VariableFlashInfoLib|MdeModulePkg/Library/BaseVariableFlashInfoLib/BaseVariableFlashInfoLib.inf
  GptLib|MdeModulePkg/Library/GptLib/GptLib.inf
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf
  UefiBootManagerLib|MdeModulePkg/Library/UefiBootManagerLib/UefiBootManagerLib.inf
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf
  #
  # Generic boot policy: connect everything (fixed-mode GOP becomes the console
  # via ConnectAll), Shell is the fallback boot option. SoC/device DSCs may
  # override this mapping with their own PlatformBootManagerLib.
  #
  PlatformBootManagerLib|Silicon/Intel/IntelPkg/Library/PlatformBootManagerLib/PlatformBootManagerLib.inf
  FrameBufferBltLib|MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.inf
  NonDiscoverableDeviceRegistrationLib|MdeModulePkg/Library/NonDiscoverableDeviceRegistrationLib/NonDiscoverableDeviceRegistrationLib.inf
  UefiUsbLib|MdePkg/Library/UefiUsbLib/UefiUsbLib.inf
  SecurityManagementLib|MdeModulePkg/Library/DxeSecurityManagementLib/DxeSecurityManagementLib.inf
  DxeServicesLib|MdePkg/Library/DxeServicesLib/DxeServicesLib.inf
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf

[LibraryClasses.common.SEC]
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  HobLib|MdePkg/Library/PeiHobLib/PeiHobLib.inf
  MemoryAllocationLib|MdePkg/Library/PeiMemoryAllocationLib/PeiMemoryAllocationLib.inf
  PeiServicesLib|MdePkg/Library/PeiServicesLib/PeiServicesLib.inf
  PeiServicesTablePointerLib|MdePkg/Library/PeiServicesTablePointerLibIdt/PeiServicesTablePointerLibIdt.inf

[LibraryClasses.common.PEI_CORE, LibraryClasses.common.PEIM]
  #
  # PEI Core and PEIMs must use the real PEI PCD library. With BasePcdLibNull
  # every PcdGetXx() returns 0, so PeiCore read PcdPeiCoreMaxPeiStackSize == 0
  # and DxeIpl computed a NULL new stack -> ASSERT in BaseLib SwitchStack().
  #
  PcdLib|MdePkg/Library/PeiPcdLib/PeiPcdLib.inf
  HobLib|MdePkg/Library/PeiHobLib/PeiHobLib.inf
  MemoryAllocationLib|MdePkg/Library/PeiMemoryAllocationLib/PeiMemoryAllocationLib.inf
  PeiServicesLib|MdePkg/Library/PeiServicesLib/PeiServicesLib.inf
  PeiServicesTablePointerLib|MdePkg/Library/PeiServicesTablePointerLibIdt/PeiServicesTablePointerLibIdt.inf
  PeimEntryPoint|MdePkg/Library/PeimEntryPoint/PeimEntryPoint.inf
  PeiCoreEntryPoint|MdePkg/Library/PeiCoreEntryPoint/PeiCoreEntryPoint.inf
  ExtractGuidedSectionLib|MdePkg/Library/PeiExtractGuidedSectionLib/PeiExtractGuidedSectionLib.inf

[LibraryClasses.common.DXE_CORE]
  HobLib|MdePkg/Library/DxeCoreHobLib/DxeCoreHobLib.inf
  MemoryAllocationLib|MdeModulePkg/Library/DxeCoreMemoryAllocationLib/DxeCoreMemoryAllocationLib.inf
  DxeCoreEntryPoint|MdePkg/Library/DxeCoreEntryPoint/DxeCoreEntryPoint.inf
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf
  CpuExceptionHandlerLib|UefiCpuPkg/Library/CpuExceptionHandlerLib/DxeCpuExceptionHandlerLib.inf

[LibraryClasses.common.DXE_DRIVER, LibraryClasses.common.UEFI_DRIVER, LibraryClasses.common.UEFI_APPLICATION]
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  #
  # Runtime drivers must NOT log through the framebuffer console. After the OS
  # calls SetVirtualAddressMap(), any DEBUG() from a runtime driver would touch
  # the physical framebuffer at 0x3F000000 and the shared console state at
  # 0x020F0000, neither of which is mapped in the OS virtual address space (and
  # 0x020F0000 is plain DRAM the OS may own). Windows Setup is the first thing
  # that calls runtime services (GetFirmwareEnvironmentVariable/GetTime), so the
  # board faulted and rebooted the moment setup.exe started, independent of
  # elapsed time. Null DebugLib for runtime drivers removes those accesses.
  #
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf
  UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
  VariablePolicyLib|MdeModulePkg/Library/VariablePolicyLib/VariablePolicyLibRuntimeDxe.inf

[LibraryClasses.common.UEFI_APPLICATION]
  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  ShellCommandLib|ShellPkg/Library/UefiShellCommandLib/UefiShellCommandLib.inf
  HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf
  BcfgCommandLib|ShellPkg/Library/UefiShellBcfgCommandLib/UefiShellBcfgCommandLib.inf

################################################################################
#
# PCDs
#
################################################################################
[PcdsFeatureFlag]
  # IA32-only platform: DxeIpl must NOT switch to long mode; DxeCore is IA32.
  gEfiMdeModulePkgTokenSpaceGuid.PcdDxeIplSwitchToLongMode|FALSE

[PcdsFixedAtBuild]
  #
  # Windows' bootia32.efi changes memory attributes through the CPU arch
  # protocol / DXE services. CpuDxe implements that by walking the page tables
  # and asserts in GetPageTableEntry() (CpuPageTable.c:336) whenever the
  # machine type is IA32 but PAE paging is not actually enabled. DxeIpl builds
  # the IA32 PAE tables (and enables paging + XD) when IsEnableNonExecNeeded()
  # returns TRUE, which it does as soon as any NX PCD is set AND the XD bit is
  # actually available. The Cloverview platform PEIM clears IA32_MISC_ENABLE[34]
  # (XD-disable) early, mirroring Linux' verify_cpu.S, so XD is real on
  # Cloverview parts and DxeIpl builds the PAE+NX tables correctly before the
  # hand-off.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetNxForStack|TRUE
  #
  # DXE memory protection: map non-Code memory types non-executable.
  # 0x7FD5 = all memory types except the Code types (Loader/BootServices/
  # Runtime-ServicesCode), the documented "all except Code" policy. This also
  # keeps IsEnableNonExecNeeded() -> ToBuildPageTable() TRUE so the PAE+NX
  # page tables are always built.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdDxeNxMemoryProtectionPolicy|0x0000000000007FD5
  gEfiMdeModulePkgTokenSpaceGuid.PcdImageProtectionPolicy|0x00000003

  gEfiMdePkgTokenSpaceGuid.PcdMaximumUnicodeStringLength|1000000
  gEfiMdePkgTokenSpaceGuid.PcdMaximumAsciiStringLength|1000000
  gEfiMdePkgTokenSpaceGuid.PcdMaximumLinkedListLength|1000000

  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxSizeNonPopulateCapsule|0x0
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x21, 0xaa, 0x2c, 0x46, 0x14, 0x76, 0x03, 0x45, 0x83, 0x6e, 0x8a, 0xb6, 0xf4, 0x66, 0x23, 0x31 }

  #
  # Firmware identity: reported as the BIOS version in SMBIOS Type0 and shown
  # on the UiApp front page (line 3). PcdFirmwareVendor's "EDK II" default is
  # kept.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"edk2-intel-mid v1.0"

  #
  # Debug
  #
  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|0x00
  gEfiMdePkgTokenSpaceGuid.PcdPerformanceLibraryPropertyMask|0
!if $(TARGET) == DEBUG
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F
  gEfiMdePkgTokenSpaceGuid.PcdDebugClearMemoryValue|0xAF
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8007EE0F
!else
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x00
  gEfiMdePkgTokenSpaceGuid.PcdDebugClearMemoryValue|0x00
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x00000000
!endif
  gEfiMdePkgTokenSpaceGuid.PcdPostCodePropertyMask|0
  gEfiMdePkgTokenSpaceGuid.PcdSpinLockTimeout|10000000

  gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE

[PcdsPatchableInModule]
  #
  # There is no SPI/NV flash region owned by this firmware: it is loaded from
  # the boot partition into DRAM by the primary bootloader, so
  # PcdFlashNvStorageVariableBase(64) is 0 and VariableRuntimeDxe ASSERTs on
  # NvStorageBase != 0 in InitNonVolatileVariableStore(). Run the variable
  # driver in emulated (volatile) NV mode instead; UiApp/BdsDxe only need
  # variables to survive within a single boot. This PCD is PatchableInModule
  # in MdeModulePkg.dec, so it must be set here rather than [PcdsFeatureFlag].
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE

[PcdsDynamicDefault]
  #
  # Common boot/console/variable policy shared by every Intel MID. A device
  # platform may override any of these in its own [PcdsDynamicDefault].
  #
  # Seconds until the boot manager (UiApp) falls through to the Shell.
  #
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|3
  gEfiMdeModulePkgTokenSpaceGuid.PcdConInConnectOnDemand|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvStoreReserved|0

################################################################################
#
# Components - the generic core and driver stack shared by every Intel MID
# platform. Placement/dispatch order lives in the platform FDF includes
# (DXE.inc / RAW.inc), not here.
#
################################################################################
[Components]
  #
  # SEC - entered directly at the very start of the firmware image (offset 0);
  # placed by [FV.FVSEC] in the platform FDF.
  #
  Silicon/Intel/IntelPkg/Sec/SecMain.inf

  #
  # PEI
  #
  MdeModulePkg/Core/Pei/PeiMain.inf
  MdeModulePkg/Universal/PCD/Pei/Pcd.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  }
  MdeModulePkg/Core/DxeIplPeim/DxeIpl.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/PeiPcdLib/PeiPcdLib.inf
      #
      # FVMAIN wraps FVDXE in an LZMA GUIDED section. Without these extraction
      # handlers registered, ExtractGuidedSectionDecode() fails, DxeIpl never
      # finds DxeCore and hands a NULL EntryPoint to SwitchStack().
      #
      NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
      NULL|MdeModulePkg/Library/PeiCrc32GuidedSectionExtractLib/PeiCrc32GuidedSectionExtractLib.inf
  }

  #
  # DXE core and architectural protocols
  #
  MdeModulePkg/Core/Dxe/DxeMain.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf
      NULL|MdeModulePkg/Library/DxeCrc32GuidedSectionExtractLib/DxeCrc32GuidedSectionExtractLib.inf
  }
  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  }

  UefiCpuPkg/CpuDxe/CpuDxe.inf
  MdeModulePkg/Universal/Metronome/Metronome.inf
  #
  # EFI_TIMER_ARCH_PROTOCOL. No 8254/PIT and no usable HPET on Intel MID
  # silicon, so the local APIC timer is the only tick source.
  #
  OvmfPkg/LocalApicTimerDxe/LocalApicTimerDxe.inf {
    <LibraryClasses>
      NestedInterruptTplLib|OvmfPkg/Library/NestedInterruptTplLib/NestedInterruptTplLib.inf
  }
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/VarCheckUefiLib/VarCheckUefiLib.inf
  }
  #
  # No FaultTolerantWriteDxe: it requires PcdFlashNvStorageFtwWorkingBase /
  # FtwSpareBase to be non-zero and a real FVB, which this DRAM-loaded
  # firmware has none of. Emulated NV variable mode does not use FTW. The
  # variable runtime support itself is SoC-specific (PlatformRuntimeDxe).
  #

  #
  # ACPI. Windows' bootia32.efi/winload aborts with 0xc0000225 ("the firmware
  # (BIOS) is not ACPI compatible") when no RSDP/XSDT is published. AcpiTableDxe
  # builds the RSDP/XSDT; the platform ACPI driver (device) installs the tables
  # from a raw storage file.
  #
  MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf

  #
  # SMBIOS. SmbiosDxe aggregates the records and installs the entry point (the
  # SMBIOS 3.0 structure is selected by PcdSmbiosVersion/PcdSmbiosEntryPointProvideMethod).
  # SmBiosTableDxe is the platform producer: it adds Type 0/1/2/3/4/16/17/19
  # records built from the device/generation PCDs and the EFI memory map.
  #
  MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
  Silicon/Intel/IntelPkg/Drivers/SmBiosTableDxe/SmBiosTableDxe.inf

  #
  # Block stack: SDHCI + SD/eMMC, bound to the SoC's register block via
  # NonDiscoverablePciDeviceDxe. The SoC's SdHostDxe (see
  # CloverviewPkg.dsc.inc) publishes that controller.
  #
  MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
  MdeModulePkg/Bus/Pci/SdMmcPciHcDxe/SdMmcPciHcDxe.inf
  MdeModulePkg/Bus/Sd/SdDxe/SdDxe.inf
  MdeModulePkg/Bus/Sd/EmmcDxe/EmmcDxe.inf

  #
  # Console / graphics
  #
  Silicon/Intel/IntelPkg/Library/PlatformGopLib/PlatformGopDxe.inf
  MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  # Serial console stack: DEBUG only. RELEASE builds BaseSerialPortLibNull and
  # the console is GOP-only (PlatformBootManagerLib registers just the GOP), so
  # SerialDxe/TerminalDxe could never produce a usable serial terminal there.
!if $(TARGET) == DEBUG
  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf
!endif
  MdeModulePkg/Logo/LogoDxe.inf

  #
  # Generic drivers
  #
  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf

  #
  # UiApp is the default boot option; the Shell is the fallback.
  #
  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  MdeModulePkg/Application/UiApp/UiApp.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DeviceManagerUiLib/DeviceManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootManagerUiLib/BootManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerUiLib.inf
  }

  ShellPkg/Application/Shell/Shell.inf {
    <LibraryClasses>
      NULL|ShellPkg/Library/UefiShellLevel2CommandsLib/UefiShellLevel2CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel1CommandsLib/UefiShellLevel1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel3CommandsLib/UefiShellLevel3CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDriver1CommandsLib/UefiShellDriver1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDebug1CommandsLib/UefiShellDebug1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellInstall1CommandsLib/UefiShellInstall1CommandsLib.inf
    <PcdsFixedAtBuild>
      gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE
      gEfiMdePkgTokenSpaceGuid.PcdUefiLibMaxPrintBufferSize|8000
  }