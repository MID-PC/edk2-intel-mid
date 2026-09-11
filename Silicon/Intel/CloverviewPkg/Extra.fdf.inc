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
# ACPI fixed tables (FACS/FADT/MADT/DBG2/CSRT) are NOT packed here: which
# tables a board publishes is a device decision, so each device FDF packs the
# .aml blobs from Silicon/Intel/CloverviewPkg/Acpi/AcpiTables/ into its own
# FREEFORM file keyed on gCloverviewAcpiTableStorageGuid. The SoC-generic
# AcpiPlatformDxe (Silicon/Intel/CloverviewPkg/Drivers/AcpiPlatformDxe)
# installs whatever RAW sections it finds and skips what a device excluded.
# The .aml blobs are pre-compiled (Silicium-style); the sources they were
# extracted from are archived under /tmp/opencode/acpi_ref/.
#
# CSRT (Core System Resources Table) is the board-corrected version of the
# Z2760 CSRT.aml - only its GDMS group (INTL0005, "Intel(R) Serial IO DMA
# Controller") differs: GSI 0x71 -> 0x31 (interrupts.txt "49:
# INTEL_MID_DMAC2") and Interrupt Polarity 02 -> 01 (ActiveLow -> ActiveHigh,
# matching the GDMS._CRS RBUF template returned at STEP != 0x02). UDMS
# (INTL0004, "Intel(R) UART DMA") keeps the reference value (GSI 0x3B,
# polarity 01). The two groups feed the in-box HalExtIntcLpioDMA /
# HalExtIntcUartDMA extensions that service the FixedDMA descriptors on the
# SPI/I2C/UART nodes.
#

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
INF  Silicon/Intel/CloverviewPkg/Drivers/OtgHostDxe/OtgHostDxe.inf
INF  MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
!endif