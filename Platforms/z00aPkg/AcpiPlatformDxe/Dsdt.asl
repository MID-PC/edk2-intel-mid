DefinitionBlock ("Dsdt.aml", "DSDT", 0x02, "INTEL ", "MOOREFLD", 0x00000001)
{
    // System power states
    Name (_S0, Package () { 0x00 })
    Name (_S5, Package () { 0x00 })

    Scope (\_SB)
    {
        // Performance states
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

        // Processor cores
        Processor (CPU0, 0x00, 0x00000000, 0x00)
        {
            Name (_UID, 0x00)
            Name (_STA, 0x0F)
            Method (_PCT, 0, NotSerialized) { Return (PCTB) }
            Method (_PSS, 0, NotSerialized) { Return (PSSL) }
            Method (_PPC, 0, NotSerialized) { Return (Zero) }
            // 2 cores per module share a clock
            Name (_PSD, Package () { Package () { 5, 0, 0x00, 0xFE, 2 } })
        }

        Processor (CPU1, 0x01, 0x00000000, 0x00)
        {
            Name (_UID, 0x01)
            Name (_STA, 0x0F)
            Method (_PCT, 0, NotSerialized) { Return (PCTB) }
            Method (_PSS, 0, NotSerialized) { Return (PSSL) }
            Method (_PPC, 0, NotSerialized) { Return (Zero) }
            // 2 cores per module share a clock
            Name (_PSD, Package () { Package () { 5, 0, 0x00, 0xFE, 2 } })
        }

        Processor (CPU2, 0x02, 0x00000000, 0x00)
        {
            Name (_UID, 0x02)
            Name (_STA, 0x0F)
            Method (_PCT, 0, NotSerialized) { Return (PCTB) }
            Method (_PSS, 0, NotSerialized) { Return (PSSL) }
            Method (_PPC, 0, NotSerialized) { Return (Zero) }
            // 2 cores per module share a clock
            Name (_PSD, Package () { Package () { 5, 0, 0x01, 0xFE, 2 } })
        }

        Processor (CPU3, 0x03, 0x00000000, 0x00)
        {
            Name (_UID, 0x03)
            Name (_STA, 0x0F)
            Method (_PCT, 0, NotSerialized) { Return (PCTB) }
            Method (_PSS, 0, NotSerialized) { Return (PSSL) }
            Method (_PPC, 0, NotSerialized) { Return (Zero) }
            // 2 cores per module share a clock
            Name (_PSD, Package () { Package () { 5, 0, 0x01, 0xFE, 2 } })
        }

        // System memory reservations
        Device (SYSR)
        {
            Name (_HID, EisaId ("PNP0C02"))
            Name (_UID, 0x01)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00000000, 0x00001000)  // IVT, BDA
                Memory32Fixed (ReadWrite, 0x04000000, 0x01200000)  // SoC reserved
                Memory32Fixed (ReadWrite, 0x05C00000, 0x00400000)  // SoC reserved, audio
                Memory32Fixed (ReadWrite, 0x7F600000, 0x00A00000)  // MMCONFIG, framebuffer
                Memory32Fixed (ReadWrite, 0xFEC04000, 0x0001C000)  // up to the I/O APIC
                Memory32Fixed (ReadWrite, 0xFEE01000, 0x001FF000)  // above the Local APIC
                Memory32Fixed (ReadWrite, 0xFF000000, 0x003FA000)  // up to the SDHCI block
                Memory32Fixed (ReadWrite, 0xFF3FB000, 0x00C05000)  // above the SDHCI block
            })
        }

        // IA-PC High Precision Event Timer
        Device (HPET)
        {
            Name (_HID, EisaId ("PNP0103"))
            Name (_UID, Zero)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFED00000, 0x00000400)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                {
                    0x00000008,
                }
            })
        }

        // SD card host controller
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

        // USB host controller
        Device (XHC0)
        {
            Name (_HID, EisaId ("PNP0D15"))
            Name (_CID, EisaId ("PNP0D10"))
            Name (_UID, 0x00)
            Name (_CCA, 0x01)
            Name (_STA, 0x0F)

            // Patched by AcpiPlatformDxe; keep the name
            Name (XIRQ, 0x22)

            Name (RBUF, ResourceTemplate ()
            {
                // Must match PcdDwc3Base / PcdDwc3Size
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

            // Root hub
            Device (RHUB)
            {
                Name (_ADR, 0x00)

                Device (HS01)
                {
                    Name (_ADR, 0x01)
                    Name (_UPC, Package () { 0xFF, 0x01, 0x00, 0x00 })  // micro-AB, connectable
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
