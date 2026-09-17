#
# Cloverview Platform Setup
#
INF  CloverviewPkg/Drivers/PlatformRuntimeDxe/PlatformRuntimeDxe.inf

#
# SD slot registration
#
INF  CloverviewPkg/Drivers/SdHostDxe/SdHostDxe.inf

#
# USB ChipIdea OTG forced into EHCI host mode + generic USB EHCI stack
# Disabled in the no-USB KDNET variant
#
!ifndef KDNET_USB
INF  CloverviewPkg/Drivers/OtgHostDxe/OtgHostDxe.inf
INF  MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
INF  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
!endif
