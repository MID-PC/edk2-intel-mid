##
#  DXE driver list for t00gPkg (generic core, console/graphics, ACPI drivers,
#  block/SD, generic drivers + UI).
#
#  The Cloverview SoC drivers live in Silicon/Intel/CloverviewPkg/Extra.fdf.inc;
#  the raw ACPI tables are listed inline in t00gPkg.fdf.
#
#  Included from [FV.FVDXE]; see t00gPkg.fdf.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

INF  MdeModulePkg/Core/Dxe/DxeMain.inf
INF  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

INF  UefiCpuPkg/CpuDxe/CpuDxe.inf
INF  MdeModulePkg/Universal/Metronome/Metronome.inf
#
# EFI_TIMER_ARCH_PROTOCOL. No 8254/PIT and no usable HPET on this SoC, so the
# local APIC timer is the only tick source.
#
INF  OvmfPkg/LocalApicTimerDxe/LocalApicTimerDxe.inf
INF  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
INF  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
INF  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
INF  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
INF  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
INF  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
INF  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf

#
# ACPI: table protocol producer and the platform tables (FADT/FACS/MADT, DSDT).
# The raw DSDT storage file is listed inline in t00gPkg.fdf.
#
INF  MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
INF  Silicon/Intel/CloverviewPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf

#
# SMBIOS: SmbiosDxe aggregates the records and publishes the entry point;
# SmBiosTableDxe adds the platform records (depex on the SMBIOS protocol).
#
INF  MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
INF  Silicon/Intel/IntelPkg/Drivers/SmBiosTableDxe/SmBiosTableDxe.inf

INF  MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
INF  MdeModulePkg/Bus/Pci/SdMmcPciHcDxe/SdMmcPciHcDxe.inf
INF  MdeModulePkg/Bus/Sd/SdDxe/SdDxe.inf
INF  MdeModulePkg/Bus/Sd/EmmcDxe/EmmcDxe.inf

#
# Console / graphics. PlatformGopDxe (fixed-mode GOP on the framebuffer) lives
# in Silicon/Intel/IntelPkg.
#
INF  Silicon/Intel/IntelPkg/Library/PlatformGopLib/PlatformGopDxe.inf
INF  MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
INF  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
INF  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
# Serial console stack: DEBUG only (RELEASE SerialPortLib is a no-op and the
# console is GOP-only, so a serial terminal would never be usable).
!if $(TARGET) == DEBUG
INF  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
INF  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf
!endif
INF  MdeModulePkg/Logo/LogoDxe.inf

INF  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
INF  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
INF  MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
INF  MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
INF  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
INF  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
INF  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
INF  FatPkg/EnhancedFatDxe/Fat.inf

#
# UiApp is the default boot option; the Shell is the fallback.
#
INF  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
INF  MdeModulePkg/Application/UiApp/UiApp.inf
INF  ShellPkg/Application/Shell/Shell.inf