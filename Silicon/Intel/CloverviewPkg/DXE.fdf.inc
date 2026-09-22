# DXE Core
INF  MdeModulePkg/Core/Dxe/DxeMain.inf
INF  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

# Generic UEFI drivers
INF  UefiCpuPkg/CpuDxe/CpuDxe.inf
INF  MdeModulePkg/Universal/Metronome/Metronome.inf
INF  OvmfPkg/LocalApicTimerDxe/LocalApicTimerDxe.inf
INF  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
INF  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
INF  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
INF  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
INF  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
INF  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
INF  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf

# ACPI
INF  MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
INF  CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf

# SMBIOS
INF  MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
INF  IntelPkg/Drivers/SmBiosTableDxe/SmBiosTableDxe.inf

# SDHCI
INF  MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
INF  MdeModulePkg/Bus/Pci/SdMmcPciHcDxe/SdMmcPciHcDxe.inf
INF  MdeModulePkg/Bus/Sd/SdDxe/SdDxe.inf

# Graphics
INF  IntelPkg/Library/PlatformGopLib/PlatformGopDxe.inf
INF  MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
INF  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
INF  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
!if $(TARGET) == DEBUG
INF  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
INF  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf
!endif
INF  IntelPkg/Logo/LogoDxe.inf

# Block/filesystem
INF  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
INF  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
INF  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
INF  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
INF  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
INF  FatPkg/EnhancedFatDxe/Fat.inf
INF  MdeModulePkg/Universal/Disk/UdfDxe/UdfDxe.inf

# BDS
INF  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
