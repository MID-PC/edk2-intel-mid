##
#  Extra.fdf.inc - Cloverview SoC drivers.
#
#  Per-generation INF list for the Intel Atom Z25xx SoC, included from the
#  platform FDF's [FV.FVDXE] (alongside the generic DXE.inc).
#  Other generations provide their own Extra.fdf.inc.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

#
# ACPI fixed tables for the Cloverview SoC (FADT/FACS/MADT/DBG2): one FREEFORM
# file keyed on gCloverviewAcpiTableStorageGuid whose RAW sections
# AcpiPlatformDxe reads and installs. The tables are pre-compiled blobs
# (Silicium-style) rather than being built from .aslc sources; the sources the
# blobs were extracted from are archived under /tmp/opencode/acpi_ref/. The
# device DSDT FREEFORM stays in the platform FDF (inline) and is compiled
# from ASL at build time.
#
FILE  FREEFORM = F77A9DA4-8DF3-4B2C-A1E6-0C57B2D34E91 {
  SECTION RAW = Silicon/Intel/CloverviewPkg/Acpi/AcpiTables/Facs.aml
  SECTION RAW = Silicon/Intel/CloverviewPkg/Acpi/AcpiTables/Facp.aml
  SECTION RAW = Silicon/Intel/CloverviewPkg/Acpi/AcpiTables/Apic.aml
  SECTION RAW = Silicon/Intel/CloverviewPkg/Acpi/AcpiTables/Dbg2.aml
}

#
# Runtime variable support without NV storage (DRAM-loaded firmware).
# No FaultTolerantWriteDxe: it requires PcdFlashNvStorageFtwWorkingBase /
# FtwSpareBase to be non-zero and a real FVB, which this firmware has none of.
# Emulated NV variable mode does not use FTW.
#
INF  Silicon/Intel/CloverviewPkg/Drivers/PlatformRuntimeDxe/PlatformRuntimeDxe.inf

#
# SD card stack. There is no real PCI on this SoC, so the SDHCI block at
# 0xFFA58000 is registered by SdHostDxe as a non-discoverable device;
# SdMmcPciHcDxe binds to it through NonDiscoverablePciDeviceDxe (which lives in
# the generic DXE.inc). Card detect is not used (assume connected).
#
INF  Silicon/Intel/CloverviewPkg/Drivers/SdHostDxe/SdHostDxe.inf

#
# USB: OTG0 (Chipidea HDRC @ 0xFFA60000) forced to EHCI host mode, plus the
# generic USB stack. UsbMassStorageDxe gives boot-media support, UsbKbDxe an
# input device in the Shell/UiApp. Hub support is built into UsbBusDxe
# (devices behind a USB hub, e.g. a keyboard on an OTG Y-cable hub, work out
# of the box; there is no separate UsbHubDxe module in this edk2).
#
# In the KDNET-over-USB variant (KDNET_USB defined) the host stack is
# left out so nothing forces the OTG core into host mode.
#
!ifndef KDNET_USB
INF  Platforms/t00kPkg/OtgHostDxe/OtgHostDxe.inf
INF  MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
!endif