DefinitionBlock ("Dsdt.aml", "DSDT", 0x02, "INTEL ", "MOOREFLD", 0x00000001)
{
    //
    // The FADT is hardware-reduced, so there is no ACPI fixed hardware at all
    // and the OS owns the whole power policy. Only the S0 working state and a
    // S5 stub are declared; no S3/S4 states exist on this part.
    //
    Name (_S0, Package () { 0x00 })
    Name (_S5, Package () { 0x00 })

    Scope (\_SB)
    {
        //
        // Performance states, from the SFI FREQ table on the device
        // (ZX551ML/tables/FREQ). Without them the OS has nothing to step through
        // and the cores stay at the ratio the boot code left them on, 1 GHz.
        //
        // Control is what sfi-cpufreq.c writes into the low 16 bits of
        // IA32_PERF_CTL; the bus ratio is bits 15:8 and the low byte is the
        // voltage ID. Status is compared on the ratio byte of IA32_PERF_STATUS
        // only (INTEL_MSR_BUSRATIO_MASK), so it holds the same value. Both
        // are reached through FFixedHW, so the OS uses the MSRs directly.
        //
        // The power column is not in the table. It is a monotonic estimate that
        // scales roughly with f * V^2 up to a ~2 W core budget; the OS only
        // uses it to order states.
        //
        // Latency is the 100 us the table gives; the bus master latency is
        // the same.
        //
        Name (PSSL, Package ()
        {
            Package () { 2333, 2000, 100, 100, 0x1C56, 0x1C56 },
            Package () { 2250, 1891, 100, 100, 0x1B54, 0x1B54 },
            Package () { 2166, 1785, 100, 100, 0x1A52, 0x1A52 },
            Package () { 2083, 1666, 100, 100, 0x194F, 0x194F },
            Package () { 2000, 1568, 100, 100, 0x184D, 0x184D },
            Package () { 1916, 1472, 100, 100, 0x174B, 0x174B },
            Package () { 1833, 1365, 100, 100, 0x1648, 0x1648 },
            Package () { 1750, 1276, 100, 100, 0x1546, 0x1546 },
            Package () { 1666, 1189, 100, 100, 0x1444, 0x1444 },
            Package () { 1583, 1094, 100, 100, 0x1341, 0x1341 },
            Package () { 1500, 1015, 100, 100, 0x123F, 0x123F },
            Package () { 1416, 937, 100, 100, 0x113D, 0x113D },
            Package () { 1333, 853, 100, 100, 0x103A, 0x103A },
            Package () { 1250, 791, 100, 100, 0x0F39, 0x0F39 },
            Package () { 1166, 730, 100, 100, 0x0E38, 0x0E38 },
            Package () { 1083, 670, 100, 100, 0x0D37, 0x0D37 },
            Package () { 1000, 612, 100, 100, 0x0C36, 0x0C36 },
            Package () { 916, 554, 100, 100, 0x0B35, 0x0B35 },
            Package () { 833, 498, 100, 100, 0x0A34, 0x0A34 },
            Package () { 750, 443, 100, 100, 0x0933, 0x0933 },
            Package () { 666, 389, 100, 100, 0x0832, 0x0832 },
            Package () { 583, 336, 100, 100, 0x0731, 0x0731 },
            Package () { 500, 285, 100, 100, 0x0630, 0x0630 }
        })

        Name (PCTB, Package ()
        {
            ResourceTemplate () { Register (FFixedHW, 0x00, 0x00, 0x0000000000000000) },
            ResourceTemplate () { Register (FFixedHW, 0x00, 0x00, 0x0000000000000000) }
        })

        //
        // Four enabled cores. _UID is the logical index and the ApicId of each
        // core is 2 * _UID (the SFI CPUS table lists 0, 2, 4, 6); both must
        // agree with the MADT Processor Local APIC structures in
        // AcpiPlatformDxe.c.
        //
        // Processor() is the ACPI-defined operator for a processor device and
        // is what every real firmware still emits; ASLC flags it as a legacy
        // keyword in favour of a plain Device() plus _HID, which is why the
        // compile logs warning 3168 here. That warning is expected.
        //
        // All four arguments must be given. The fourth, PblkLen, is optional
        // in ASL, but leaving it out made the compiler drop its byte from the
        // encoding, so the AML parser read the NameOp of the next object as
        // PblkLen and everything after this device failed to load.
        //
        // No _HID: a Processor object is identified by its object type, and
        // ACPI0010 is the Processor Container Device, not a processor.
        //
        Processor (CPU0, 0x00, 0x00000000, 0x00)
        {
            Name (_UID, 0x00)
            Name (_STA, 0x0F)
            Method (_PCT, 0, NotSerialized) { Return (PCTB) }
            Method (_PSS, 0, NotSerialized) { Return (PSSL) }
            Method (_PPC, 0, NotSerialized) { Return (Zero) }
            // Two cores per module share one clock; hardware coordinates them.
            Name (_PSD, Package () { Package () { 5, 0, 0x00, 0xFE, 2 } })
        }

        Processor (CPU1, 0x01, 0x00000000, 0x00)
        {
            Name (_UID, 0x01)
            Name (_STA, 0x0F)
            Method (_PCT, 0, NotSerialized) { Return (PCTB) }
            Method (_PSS, 0, NotSerialized) { Return (PSSL) }
            Method (_PPC, 0, NotSerialized) { Return (Zero) }
            // Two cores per module share one clock; hardware coordinates them.
            Name (_PSD, Package () { Package () { 5, 0, 0x00, 0xFE, 2 } })
        }

        Processor (CPU2, 0x02, 0x00000000, 0x00)
        {
            Name (_UID, 0x02)
            Name (_STA, 0x0F)
            Method (_PCT, 0, NotSerialized) { Return (PCTB) }
            Method (_PSS, 0, NotSerialized) { Return (PSSL) }
            Method (_PPC, 0, NotSerialized) { Return (Zero) }
            // Two cores per module share one clock; hardware coordinates them.
            Name (_PSD, Package () { Package () { 5, 0, 0x01, 0xFE, 2 } })
        }

        Processor (CPU3, 0x03, 0x00000000, 0x00)
        {
            Name (_UID, 0x03)
            Name (_STA, 0x0F)
            Method (_PCT, 0, NotSerialized) { Return (PCTB) }
            Method (_PSS, 0, NotSerialized) { Return (PSSL) }
            Method (_PPC, 0, NotSerialized) { Return (Zero) }
            // Two cores per module share one clock; hardware coordinates them.
            Name (_PSD, Package () { Package () { 5, 0, 0x01, 0xFE, 2 } })
        }

        //
        // Motherboard resources: every physical range that is neither RAM nor
        // claimed by a device below, so the OS keeps its hands off it. These
        // mirror the MMIO and reserved entries that PlatformPei hands to DXE
        // from the SFI memory map (ZX551ML/iomem.txt).
        //
        // The MADT claims the I/O APIC and the Local APIC, SDC0 claims the
        // SDHCI block and HPET claims its register block, so the surrounding
        // windows are split to keep the _CRS sets disjoint. In particular the
        // 0xFED00000 HPET block falls in the gap between the I/O APIC window
        // and the Local APIC window and is deliberately left unreserved here.
        //
        Device (SYSR)
        {
            Name (_HID, EisaId ("PNP0C02"))
            Name (_UID, 0x01)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00000000, 0x00001000)  // 0x00000000-0x00000FFF IVT, BDA
                Memory32Fixed (ReadWrite, 0x04000000, 0x01200000)  // 0x04000000-0x051FFFFF SoC reserved
                Memory32Fixed (ReadWrite, 0x05C00000, 0x00400000)  // 0x05C00000-0x05FFFFFF SoC reserved, audio
                Memory32Fixed (ReadWrite, 0x7F600000, 0x00A00000)  // 0x7F600000-0x7FFFFFFF MMCONFIG, framebuffer
                Memory32Fixed (ReadWrite, 0xFEC04000, 0x0001C000)  // 0xFEC04000-0xFEC1FFFF up to the I/O APIC
                Memory32Fixed (ReadWrite, 0xFEE01000, 0x001FF000)  // 0xFEE01000-0xFEFFFFFF above the Local APIC
                Memory32Fixed (ReadWrite, 0xFF000000, 0x003FA000)  // 0xFF000000-0xFF3F9FFF up to the SDHCI block
                Memory32Fixed (ReadWrite, 0xFF3FB000, 0x00C05000)  // 0xFF3FB000-0xFFFFFFFF above the SDHCI block
            })
        }

        //
        // IA-PC High Precision Event Timer. This is the only clock the OS can
        // use: the FADT is hardware-reduced so there is no PM timer block, and
        // the CMOS/RTC is declared absent, so ACPI.sys looks for a PNP0103
        // device and a matching "HPET" table (built in AcpiPlatformDxe.c).
        //
        // Same device as Bay Trail and Tangier, so the block and the IRQ match
        // what the existing drivers for this part already expect.
        //
        Device (HPET)
        {
            Name (_HID, EisaId ("PNP0103"))  // HPET System Timer
            Name (_UID, Zero)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite,
                    0xFED00000,         // Address Base
                    0x00000400,         // Address Length
                    )
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                {
                    0x00000008,
                }
            })
        }

        //
        // External SD card host. PNP0D40 is the ACPI-defined SD Host
        // Controller ID; INT33BB is the platform-specific parent bridge.
        //
        // GSI 37 is the IR Linux's mmc1 driver is using for this block, and on
        // this platform the I/O APIC pin is the IRQ number itself
        // (irq_attr.ioapic_pin = irq in intel_mid_sfi.c), so no rebasing is
        // needed. It is the only consumer of the line, hence Exclusive.
        //
        // Level and ActiveHigh both look wrong for PCI and are deliberate: the
        // Moorefield I/O APIC is programmed that way, not the PCI core. intel_
        // mid_sfi.c:607 hardcodes trigger = 1 (Level) for every SFI device, and
        // lines 608-631 select polarity = 0 (ActiveHigh) for TANGIER and
        // ANNIEDALE, which is this part. Only a few named touch controllers get
        // ActiveLow, and this is not one of them.
        //
        Device (SDC0)
        {
            Name (_HID, EisaId ("PNP0D40"))
            Name (_CID, EisaId ("PNP0D40"))
            Name (_UID, 0x00)
            Name (_CCA, 0x01)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFF3FA000, 0x00001000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                {
                    0x00000025,        // 37, mmc1
                }
            })
			Method (_DIS, 0, NotSerialized)
            {
            }
			Device (SDMD)
            {
                Name (_ADR, 0x08)
                Method (_RMV, 0, NotSerialized)
                {
                    Return (Zero) // force non-removable
                }
            }
        }

        //
        // USB host controller: the Synopsys DWC3 core (PCI 0000:00:11.0, ID
        // 8086:119E in the SFI kernel) running in host mode, whose xHCI
        // registers sit at the bottom of its 0xF9100000 BAR.
        //
        // Firmware (XhciDwc3Dxe) has already turned on VBUS, powered the USB2 PHY
        // and put the core in host mode, and the port is treated as a permanent
        // OTG A-port: there is no ID/extcon state machine in the OS, so nothing
        // here touches VBUS or the DWC3 global registers. What the OS gets is
        // a plain xHCI controller, hence PNP0D10 (Windows xhci.sys and Linux
        // xhci-plat both bind it).
        //
        // The interrupt is not hardcoded. On this SoC a PCI device's
        // INTERRUPT_LINE is the I/O APIC pin and GSI directly (intel_mid_pci_
        // irq_enable() in arch/x86/pci/mrst.c), so AcpiPlatformDxe reads it
        // from the device's config space and patches it into XIRQ before the
        // DSDT is installed. The initial value is only the fallback used if
        // that read fails. Level/ActiveHigh are what the same function programs
        // for Tangier and Annedale; Shared because the kernel requests the same
        // line with IRQF_SHARED (the DWC3 OTG block sits on it as well).
        //
        // The DMA is cache coherent, as it is for everything on the IA fabric.
        //
        Device (XHC0)
        {
            //
            // The capability list (ZX551ML boot log) has no xHCI debug
            // capability, so this is the "without debug" ID. PNP0D10 ("with
            // debug") stays as the compatible ID for INFs that only list it.
            //
            Name (_HID, EisaId ("PNP0D15"))
            Name (_CID, EisaId ("PNP0D10"))
            Name (_UID, 0x00)
            Name (_CCA, 0x01)
            Name (_STA, 0x0F)

            // Patched by AcpiPlatformDxe (MfPatchXhciIrq); keep the name.
            Name (XIRQ, 0x22)

            Name (RBUF, ResourceTemplate ()
            {
                // Must match PcdDwc3Base / PcdDwc3Size.
                Memory32Fixed (ReadWrite, 0xF9100000, 0x00100000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Shared, ,, IRQR)
                {
                    0x00000022,
                }
            })

            Method (_CRS, 0, NotSerialized)
            {
                CreateDWordField (RBUF, \_SB.XHC0.IRQR._INT, IRQV)
                Store (XIRQ, IRQV)
                Return (RBUF)
            }

            //
            // Root hub and ports, as the xHCI Supported Protocol capabilities
            // report them: port 1 is the USB 2.0 port (protocol 2.0, offset 1,
            // count 1) and port 2 the USB 3.0 port (protocol 3.0, offset 2,
            // count 1). The phone has one micro-AB OTG connector and it is
            // wired to the USB 2.0 pair only, so the SuperSpeed port goes
            // nowhere and is declared not connectable.
            //
            Device (RHUB)
            {
                Name (_ADR, 0x00)

                Device (HS01)
                {
                    Name (_ADR, 0x01)
                    Name (_UPC, Package () { 0xFF, 0x01, 0x00, 0x00 })  // connectable, Mini-AB (micro-AB)
                    Name (_PLD, Package ()
                    {
                        ToPLD (
                            PLD_Revision           = 0x2,
                            PLD_IgnoreColor        = 0x1,
                            PLD_UserVisible        = 0x1,
                            PLD_Panel              = "UNKNOWN",
                            PLD_VerticalPosition   = "CENTER",
                            PLD_HorizontalPosition = "CENTER",
                            PLD_Shape              = "OVAL",
                            PLD_Ejectable          = 0x0
                            )
                    })
                }

                Device (SS01)
                {
                    Name (_ADR, 0x02)
                    Name (_UPC, Package () { 0x00, 0xFF, 0x00, 0x00 })  // not connectable
                }
            }
        }
    }
}
