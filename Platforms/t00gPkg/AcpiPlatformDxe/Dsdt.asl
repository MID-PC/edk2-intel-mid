
DefinitionBlock ("Dsdt.aml", "DSDT", 0x01, "INTEL ", "CLOVERVW", 0x00000011)
{
    // System power states
    Name (\_S0, Package (0x04) { 0x05, 0x00, 0x00, 0x00 })
    Name (\_S5, Package (0x04) { 0x00, 0x00, 0x00, 0x00 })

    // Global NVS
    Name (\PEPP, Zero)
    Name (\STEP, One)
    Name (\USBS, 0x00)
    Name (\UOFD, 0x00)

    Scope (\_SB)
    {
        // System memory reservations
        Device (SYSR)
        {
            Name (_HID, EisaId ("PNP0C02"))
            Name (_UID, 0x01)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00D00000, 0x00300000) // low MMIO
                Memory32Fixed (ReadWrite, 0x36FF0000, 0x00A0D000) // SoC MMIO
                Memory32Fixed (ReadWrite, 0x7FB00000, 0x00100000) // MMCONFIG
                // Framebuffer (0x7FC00000) and GPU (0x80000000) windows are produced by \_SB.PCI0 (GFX0)
                Memory32Fixed (ReadWrite, 0xDF800000, 0x00600000) // 0xDFE00000+ produced by PCI0 for GFX0
                Memory32Fixed (ReadWrite, 0xFA000000, 0x04000000)
                Memory32Fixed (ReadWrite, 0xFEC00000, 0x00001000) // IOAPIC 0
                Memory32Fixed (ReadWrite, 0xFEC10000, 0x00001000) // IOAPIC 1
                Memory32Fixed (ReadWrite, 0xFEE00000, 0x00001000) // LAPIC
                Memory32Fixed (ReadWrite, 0xFF000000, 0x0011C000) // SoC MMIO below SCU IPC
                Memory32Fixed (ReadWrite, 0xFF11C400, 0x00943C00) // SoC MMIO above SCU IPC
                Memory32Fixed (ReadWrite, 0xFFA61000, 0x00596FC0) // below IPC mailbox
                Memory32Fixed (ReadWrite, 0xFFFF8000, 0x00008000) // above IPC mailbox
                Memory32Fixed (ReadWrite, 0x7FAFF000, 0x00001000) // console state page
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (PEP)
        {
            Name (_HID, "INT3395")
            Name (_CID, EisaId ("PNP0D80"))
            Name (_UID, One)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Method (_DSM, 4, Serialized)
            {
                If ((Arg0 == ToUUID ("b8febfe0-baf8-454b-aecd-49fb91137b21")))
                {
                    If ((Arg2 == Zero))
                    {
                        Return (Buffer (One)
                        {
                             0x03
                        })
                    }

                    If ((Arg2 == One))
                    {
                        PEPP = One
                        Return (0x0F)
                    }
                }

                Return (One)
            }

            PowerResource (ID3C, 0x00, 0x0000)
            {
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_ON, 0, NotSerialized)
                {
                }

                Method (_OFF, 0, NotSerialized)
                {
                }
            }
        }

        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_ADR, Zero)
            Name (_BBN, Zero)
            Name (_UID, Zero)
            Name (_DEP, Package (0x01)
            {
                \_SB.PEP
            })
            Name (_PRT, Package (0x02)
            {
                Package (0x04) { 0x0002FFFF, Zero, Zero, 0x10 }, // GPU pin 0
                Package (0x04) { 0x0003FFFF, Zero, Zero, 0x0B }  // ISP pin 0
            })
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    0x0000, 0x0000, 0x00FF, 0x0000, 0x0100,,, )
                IO (Decode16, 0x0CF8, 0x0CF8, 0x01, 0x08)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0000, 0x006F, 0x0000, 0x0070,,, , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0078, 0x0CF7, 0x0000, 0x0C80,,, , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0D00, 0xFFFF, 0x0000, 0xF300,,, , TypeStatic, DenseTranslation)
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite,
                    0x00000000, 0x000A0000, 0x000BFFFF, 0x00000000, 0x00020000,,,
                    , AddressRangeMemory, TypeStatic)
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0x7FC00000, 0x8FFFFFFF, 0x00000000, 0x10400000,,, )
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0xDF800000, 0xDFBFFFFF, 0x00000000, 0x00400000,,, )
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0xDFE00000, 0xDFFFFFFF, 0x00000000, 0x00200000,,, )
            })
            // Give the OS control over PCIe features (native hotplug/PME/AER)
            Method (_OSC, 4, NotSerialized)
            {
                If (LEqual (Arg0, ToUUID ("33db4d5b-1ff7-401c-9657-7441c03dd766")))
                {
                    Return (Arg3)
                }
                Else
                {
                    CreateDWordField (Arg3, 0, CDW1)
                    Or (CDW1, 4, CDW1)
                    Return (Arg3)
                }
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Device (GFX0)
            {
                Name (_ADR, 0x00020000)
                Name (_DDN, "PowerVR SGX544MP Graphics")
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }
            }

            // Camera subsystem/Intel ISP 2300 (disabled for now, lowest on the priority list)
            Device (ISP0)
            {
                Name (_ADR, 0x00030000)
                Name (_DDN, "Intel Imaging Signal Processor 2300")
                Name (_DEP, Package (0x01)
                {
                    \_SB.PEP
                })
                Method (_PR3, 0, NotSerialized)
                {
                    Return (Package (0x01)
                    {
                        \_SB.PEP.ID3C
                    })
                }
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }
            }
        }

        Device (IPC)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B5")
            Name (_CID, "INT33B5")
            Name (_DDN, "Cloverview Inter-Processor (x86/SCU) Communication controller")
            Name (_UID, One)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFF11C000, 0x00000400)
                Memory32Fixed (ReadOnly, 0xFFFF7FC0, 0x00000040)
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive, ,, )
                {
                    0x00000017,
                }
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Device (MREG)
            {
                Name (_ADR, Zero)
                Name (_HID, "INTCFFA")
                Name (_CID, "INTCFFA")
                Name (_DDN, "MSIC Register Access")
                Name (_UID, One)
                Name (_CRS, ResourceTemplate ()
                {
                })
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }
            }
        }

        Device (MBID)
        {
            Name (_HID, "INT33BD")
            Name (_CID, "INT33BD")
            Name (_UID, One)
            Method (_CRS, 0, Serialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xE00000D0,
                        0x00000008)
                })
                Return (RBUF)
            }
        }

        Device (PWRB)
        {
            Name (_HID, EisaId ("PNP0C0C"))
            Name (_UID, One)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (GPO0)
        {
            Name (_HID, "INT33B2")
            Name (_CID, "INT33B2")
            Name (_DDN, "Cloverview AON General Purpose Input/Output (GPIO) controller")
            Name (_UID, One)
            Method (_HRV, 0, NotSerialized) // Driver variant selection: A0/B0 ES AON
            {
                If (((STEP == One) || (STEP == Zero)))
                {
                    Return (Zero)
                }
                ElseIf ((STEP == 0x02))
                {
                    Return (0x02)
                }
                Else
                {
                    Return (0x04)
                }
            }
            Name (_DEP, Package (0x01) { \_SB.PEP })
            Method (_CRS, 0, NotSerialized)
            {
                Name (CBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF119000, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveLow, Shared, ,, ) { 0x00000015 }
                })
                Return (CBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (AVBL, Zero)
            Method (_REG, 2, NotSerialized)
            {
                If ((Arg0 == 0x08))
                {
                    AVBL = Arg1
                }
            }
        }

        Device (TBAD)
        {
            Name (_HID, "INTCFD9")
            Name (_CID, "PNP0C40")
            Name (_DDN, "Keyboard less system - 5 Button Array Device")
            Name (_UID, 0x10)
            Name (_DEP, Package (0x01) { \_SB.GPO0 })
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    // 0: Power (MSIC/PMIC key)
                    GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x0001
                        }
                    // 1: Windows / Home
                    GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x0004
                        }
                    // 2: Volume Up - clv_gpio_0 pin 30
                    GpioInt (Edge, ActiveBoth, ExclusiveAndWake, PullNone, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x001E
                        }
                    // 3: Volume Down - clv_gpio_0 pin 31
                    GpioInt (Edge, ActiveBoth, ExclusiveAndWake, PullNone, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x001F
                        }
                    // 4: Rotation lock
                    GpioInt (Edge, ActiveLow, Exclusive, PullUp, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x0005
                        }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (GPO1)
        {
            Name (_HID, "INT33B2")
            Name (_CID, "INT33B2")
            Name (_DDN, "Cloverview Corewell Powered General Purpose Input/Output (GPIO) controller")
            Name (_UID, 0x02)
            Method (_HRV, 0, NotSerialized) // Driver variant selection: A0/B0 ES CORE
            {
                If (((STEP == One) || (STEP == Zero)))
                {
                    Return (One)
                }
                ElseIf ((STEP == 0x02))
                {
                    Return (0x03)
                }
                Else
                {
                    Return (0x04)
                }
            }
            Name (_DEP, Package (0x01) { \_SB.PEP })
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF13F000, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveLow, Shared, ,, ) { 0x00000038 }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (AVBL, Zero)
            Method (_REG, 2, NotSerialized)
            {
                If ((Arg0 == 0x08))
                {
                    AVBL = Arg1
                }
            }

            Name (GMOD, ResourceTemplate ()
            {
                GpioIo (Exclusive, PullDefault, 0x0000, 0x0000, IoRestrictionOutputOnly,
                    "\\_SB.GPO1", 0x00, ResourceConsumer, ,
                    )
                    {
                        0x004A      // WLAN_EN (CORE pin 0x4A)
                    }
            })
            OperationRegion (GPOP, GeneralPurposeIo, Zero, 0x0C)
            Field (\_SB.GPO1.GPOP, ByteAcc, NoLock, Preserve)
            {
                Connection (GMOD),
                WLEN,   1,
            }
        }

        Device (GDMS) // Super IO DMA
        {
            Name (_HID, "INTL0005")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (CBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF13E000, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive, ,, )
                    {
                        0x00000071,
                    }
                })
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF13E000, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000031,
                    }
                })
                If ((STEP == 0x02))
                {
                    Return (CBUF)
                }
                Else
                {
                    Return (RBUF)
                }
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (UDMS) // UART DMA
        {
            Name (_HID, "INTL0004")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFFA28400, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000003B,
                    }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (URT0)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33BC")
            Name (_CID, "INT33BC")
            Name (_DDN, "Cloverview UART Controller")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFFA28080, 0x00000040)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000003C,
                    }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Device (BT0)
            {
                Name (_HID, "BCM2E01")
                Method (_CRS, 0, NotSerialized)
                {
                    Name (PBUF, ResourceTemplate ()
                    {
                        UartSerialBusV2 (0x0001C200, DataBitsEight, StopBitsOne,
                            0xFC, LittleEndian, ParityTypeNone, FlowControlHardware,
                            0x0020, 0x0020, "\\_SB.URT0",
                            0x00, ResourceConsumer, , Exclusive,
                            )
                        GpioIo (Exclusive, PullDefault, 0x0000, 0x0000, IoRestrictionOutputOnly,
                            "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                            )
                            {
                                0x002D      // device-wakeup (AON GPIO 45)
                            }
                        GpioIo (Exclusive, PullDefault, 0x0000, 0x0000, IoRestrictionOutputOnly,
                            "\\_SB.GPO1", 0x00, ResourceConsumer, ,
                            )
                            {
                                0x000D      // shutdown (Core GPIO 13)
                            }
                    })
                    Return (PBUF)
                }
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }
            }
        }

        Device (URT1)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33BC")
            Name (_CID, "INT33BC")
            Name (_DDN, "Cloverview UART Controller")
            Name (_UID, 0x02)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFFA28100, 0x00000040)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000003D,
                    }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (URT2)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33BC")
            Name (_CID, "INT33BC")
            Name (_DDN, "Cloverview UART Controller")
            Name (_UID, 0x03)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFFA28180, 0x00000040)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000003E,
                    }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (SPI1)
        {
            Name (_HID, "INT33B0")
            Name (_CID, "INT33B0")
            Name (_DDN, "Cloverview SPI Controller")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF135000, 0x00000400)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000009,
                    }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (SPI2)
        {
            Name (_HID, "INT33B0")
            Name (_CID, "INT33B0")
            Name (_DDN, "Cloverview SPI Controller")
            Name (_UID, 0x02)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF136000, 0x00000400)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000026,
                    }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (SDC0)
        {
            Name (_ADR, 0x00000000)
            Name (_HID, "INT33BB")
            Name (_CID, EisaId ("PNP0D40"))
            Name (_UID, 0x02)
            Name (_HRV, 0x01)
            Name (RBUF, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFFA58000, 0x00000100)
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive, ,, ) { 0x00000029 }
            })
            Method (_CRS, 0, NotSerialized)
            {
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
            Method (_DIS, 0, NotSerialized)
            {
            }
            // No card-detect GPIO is declared
            Device (SDMD)
            {
                Name (_ADR, 0x08)
                Method (_RMV, 0, NotSerialized)
                {
                    Return (Zero) // force non-removable
                }
            }
        }

        Device (SDC1)
        {
            Name (_ADR, 0x00000000)
            Name (_HID, "INT33BB")
            Name (_CID, EisaId ("PNP0D40"))
            Name (_UID, 0x03)
            Name (_HRV, 0x02)
            Name (_DEP, Package (0x02) { \_SB.PEP, \_SB.GPO1 })
            Name (PSTS, Zero)
            Name (RBUF, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFFA48000, 0x00000100)
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive, ,, ) { 0x0000002A }
            })
            Method (_CRS, 0, NotSerialized)
            {
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
            Method (_PS0, 0, NotSerialized)
            {
                If ((PSTS == Zero))
                {
                    If ((\_SB.GPO1.AVBL == One))
                    {
                        \_SB.GPO1.WLEN = One
                        PSTS = One
                        Sleep (0x32)
                    }
                }
            }
            Method (_PS3, 0, NotSerialized)
            {
            }
            Method (_DIS, 0, NotSerialized)
            {
            }
            Device (BRCM)
            {
                Name (_ADR, One)
                Method (_RMV, 0, NotSerialized)
                {
                    Return (Zero)
                }
            }
        }

        // USB ChipIdea OTG
#ifdef KDNET_USB
#include "usb-debug.asl"
#else
#include "usb-host.asl"
#endif
    }
}
