
DefinitionBlock ("Dsdt.aml", "DSDT", 0x01, "INTEL ", "CLOVERVW", 0x00000017)
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
                    0x00000000, 0x3F000000, 0x4FFFFFFF, 0x00000000, 0x11000000,,, )
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
                Name (_DEP, Package (0x01)
                {
                    \_SB.PEP
                })
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
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                {
                    0x00000017,
                }
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            // SCU IPC-1 register block
            OperationRegion (IPCR, SystemMemory, 0xFF11C000, 0x00000100)
            Field (IPCR, DWordAcc, NoLock, Preserve)
            {
                CMDR,   32,
                STSR,   32,
                SPTR,   32,
                DPTR,   32,
                Offset (0x80),
                WBUF,   128,
                RBUF,   128
            }

            Method (IPCC, 4, Serialized)
            {
                Name (RTRY, Zero)
                Name (PKG, Package (0x02) { Zero, Zero })

                // Wait for any previous command to complete
                While (((STSR & One) != Zero) && (RTRY < 0x000186A0))
                {
                    RTRY++
                }

                WBUF = Arg3
                SPTR = Arg1
                DPTR = Arg2
                CMDR = Arg0
                RTRY = Zero
                While (((STSR & One) != Zero) && (RTRY < 0x000186A0))
                {
                    RTRY++
                }

                If (((STSR & 0x02) != Zero))
                {
                    PKG [Zero] = ((STSR >> 0x10) & 0xFF)
                    Return (PKG)
                }

                PKG [One] = RBUF
                Return (PKG)
            }

            Method (LIPC, 4, Serialized)
            {
                Return (IPCC (Arg0, Arg1, Arg2, Arg3))
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

        // SCU virtual RTC
        // Registers taken from the Android rtc_mrst driver, year base is 1972
        Device (ATAD)
        {
            Name (_ADR, Zero)
            Name (_HID, "ACPI000E")
            Name (_CID, "ACPI000E")
            Name (_UID, One)
            Name (TMZN, 0x07FF)
            Name (DAYL, Zero)
            Name (ALWS, 0x0F)
            Mutex (RTCL, 0x00)
            OperationRegion (VRTC, SystemMemory, 0xFF108C00, 0x38)
            Field (VRTC, ByteAcc, NoLock, Preserve)
            {
                SEC,    8,
                Offset (0x04),
                SECA,   8,
                Offset (0x08),
                MIN,    8,
                Offset (0x0C),
                MINA,   8,
                Offset (0x10),
                HOR,    8,
                Offset (0x14),
                HORA,   8,
                Offset (0x18),
                DAYW,   8,
                Offset (0x1C),
                DAY,    8,
                Offset (0x20),
                MON,    8,
                Offset (0x24),
                YEAR,   8,
                Offset (0x28),
                STAA,   8,
                Offset (0x2C),
                STAB,   8,
                Offset (0x30),
                STAC,   8,
                Offset (0x34),
                STAD,   8
            }

            Method (INUP, 0, Serialized)
            {
                Name (TSTB, Zero)
                TSTB = STAB
                TSTB |= 0x80
                STAB = TSTB
            }

            Method (ALLW, 0, Serialized)
            {
                Name (TSTB, Zero)
                TSTB = STAB
                TSTB &= 0x7F
                STAB = TSTB
            }

            // Wait for the update-in-progress flag (STAA bit 7) to clear.
            // The caller holds RTCL; a timeout falls through so the mutex is never leaked.
            Method (RTWT, 0, NotSerialized)
            {
                Name (RTRY, Zero)
                While (((STAA & 0x80) != Zero) && (RTRY < 0x000186A0))
                {
                    RTRY++
                }
            }

            Method (_GCP, 0, NotSerialized)
            {
                // ACPI 6.2 section 9.18.2: bit 0 = AC wake timer, bit 2 =
                // real-time get/set, so 0x05 is "AC alarm plus real time".
                // Other _GCP objects use a different bit layout.
                Return (0x05)
            }

            Method (_GRT, 0, Serialized)
            {
                Name (LCTR, Zero)
                Name (RBUF, Buffer (0x10)
                {
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                })
                CreateWordField (RBUF, Zero, TYAR)
                CreateByteField (RBUF, 0x02, TMON)
                CreateByteField (RBUF, 0x03, TDAY)
                CreateByteField (RBUF, 0x04, THOR)
                CreateByteField (RBUF, 0x05, TMIN)
                CreateByteField (RBUF, 0x06, TSEC)
                CreateByteField (RBUF, 0x07, TVAL)
                CreateWordField (RBUF, 0x0A, TZN)
                CreateByteField (RBUF, 0x0C, DYL)
                Acquire (RTCL, 0x00C8)
                RTWT ()
                TSEC = SEC
                TMIN = MIN
                THOR = HOR
                TDAY = DAY
                TMON = MON
                TYAR = YEAR
                TYAR += 0x07B4
                TVAL = One
                TZN = TMZN
                DYL = DAYL
                Release (RTCL)
                Return (RBUF)
            }

            Method (_SRT, 1, Serialized)
            {
                CreateWordField (Arg0, Zero, TYAR)
                CreateByteField (Arg0, 0x02, TMON)
                CreateByteField (Arg0, 0x03, TDAY)
                CreateByteField (Arg0, 0x04, THOR)
                CreateByteField (Arg0, 0x05, TMIN)
                CreateByteField (Arg0, 0x06, TSEC)
                CreateWordField (Arg0, 0x0A, TZN)
                CreateByteField (Arg0, 0x0C, DYL)
                Name (LYR, Zero)
                Name (LCTR, Zero)
                LYR = (TYAR - 0x07B4)
                Acquire (RTCL, 0x00C8)
                RTWT ()
                INUP ()
                YEAR = LYR
                MON = TMON
                DAY = TDAY
                HOR = THOR
                MIN = TMIN
                SEC = TSEC
                ALLW ()
                \_SB.IPC.IPCC (0x10FA, Zero, Zero, Zero)
                Release (RTCL)
                Return (Zero)
            }

            Method (_STV, 2, Serialized)
            {
                Name (THOR, Zero)
                Name (TMIN, Zero)
                Name (TSEC, Zero)
                Name (RSEC, Zero)
                Name (RMIN, Zero)
                Name (RHOR, Zero)
                Name (TSTB, Zero)
                Name (TSTC, Zero)
                Name (TSTD, Zero)
                If ((Zero != Arg0))
                {
                    Return (One)
                }

                If ((Arg1 > 0x0001517F))
                {
                    Return (One)
                }

                Divide (Arg1, 0x3C, TSEC, TMIN)
                Divide (TMIN, 0x3C, TMIN, THOR)
                Acquire (RTCL, 0x00C8)
                RTWT ()
                RSEC = SEC
                RMIN = MIN
                RHOR = HOR
                TSEC += RSEC
                If ((TSEC > 0x3B))
                {
                    TSEC -= 0x3C
                    TMIN++
                }

                TMIN += RMIN
                If ((TMIN > 0x3B))
                {
                    TMIN -= 0x3C
                    THOR++
                }

                THOR += RHOR
                If ((THOR > 0x17))
                {
                    THOR -= 0x18
                }

                // Disable the alarm interrupt while changing the alarm registers, but preserve RTC_CONTROL's mode bits
                // (0x06 = 24-hour binary; 0x22 would clear RTC_DM_BINARY).
                TSTB = STAB
                TSTB &= 0xDF
                STAB = TSTB
                INUP ()
                SECA = TSEC
                MINA = TMIN
                HORA = THOR
                TSTB = STAB
                TSTB |= 0x20
                STAB = TSTB
                ALLW ()
                \_SB.IPC.IPCC (0x20FA, Zero, Zero, Zero)
                TSTC = STAC
                TSTD = STAD
                Release (RTCL)
                Return (Zero)
            }

            Method (_TIV, 1, Serialized)
            {
                Name (AHOR, Zero)
                Name (AMIN, Zero)
                Name (ASEC, Zero)
                Name (RSEC, Zero)
                Name (RMIN, Zero)
                Name (RHOR, Zero)
                If ((Zero != Arg0))
                {
                    Return (Ones)
                }

                Acquire (RTCL, 0x00C8)
                RTWT ()
                ASEC = SECA
                AMIN = MINA
                AHOR = HORA
                RSEC = SEC
                RMIN = MIN
                RHOR = HOR
                Release (RTCL)
                If ((ASEC < RSEC))
                {
                    If ((AMIN == Zero))
                    {
                        AMIN = 0x3C
                        If ((AHOR == Zero))
                        {
                            AHOR = 0x18
                        }

                        AHOR--
                    }

                    AMIN--
                }

                ASEC -= RSEC
                If ((AMIN < RMIN))
                {
                    If ((AHOR == Zero))
                    {
                        AHOR = 0x18
                    }

                    AHOR--
                }

                AMIN -= RMIN
                If ((AHOR < RHOR))
                {
                    AHOR += 0x18
                }

                AHOR -= RHOR
                AMIN *= 0x3C
                ASEC += AMIN
                AHOR *= 0x0E10
                ASEC += AHOR
                Return (ASEC)
            }

            Method (_GWS, 1, Serialized)
            {
                Name (TSTC, Zero)
                If ((Zero != Arg0))
                {
                    Return (Zero)
                }

                If ((0x0F == ALWS))
                {
                    Acquire (RTCL, 0x00C8)
                    TSTC = STAC
                    Release (RTCL)
                    TSTC &= 0x20
                    If (TSTC)
                    {
                        ALWS = 0x03
                        Return (ALWS)
                    }
                }

                Return (Zero)
            }

            Method (_CWS, 1, Serialized)
            {
                Name (TSTC, Zero)
                // Only the AC timer exists.  _GWS accepts timer ID 0, so _CWS must accept it as well
                If ((Zero != Arg0))
                {
                    Return (One)
                }

                If ((0x0F == ALWS))
                {
                    Acquire (RTCL, 0x00C8)
                    TSTC = STAC
                    Release (RTCL)
                }

                ALWS = 0x0F
                Return (Zero)
            }

            Method (_STP, 2, Serialized)
            {
                Return (One)
            }

            Method (_TIP, 1, Serialized)
            {
                Return (Ones)
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
            Name (_DEP, Package (0x01) { \_SB.IPC })
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

            Name (TRST, ResourceTemplate ()
            {
                GpioIo (Exclusive, PullDefault, 0x0000, 0x0000, IoRestrictionOutputOnly,
                    "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                    )
                    {
                        0x003A      // ts_rst (pin 58)
                    }
            })
            OperationRegion (GPOP, GeneralPurposeIo, Zero, 0x0C)
            Field (\_SB.GPO0.GPOP, ByteAcc, NoLock, Preserve)
            {
                Connection (TRST),
                TSTP, 1
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
            Name (_DEP, Package (0x01) { \_SB.IPC })
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
                    FixedDMA (0x0000, 0x0000, Width8bit, )
                    FixedDMA (0x0001, 0x0001, Width8bit, )
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
                    FixedDMA (0x0002, 0x0002, Width8bit, )
                    FixedDMA (0x0003, 0x0003, Width8bit, )
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
                    FixedDMA (0x0004, 0x0004, Width8bit, )
                    FixedDMA (0x0005, 0x0005, Width8bit, )
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
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF135000, 0x00000400)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000009,
                    }
                    FixedDMA (0x0011, 0x0006, Width32bit, )
                    FixedDMA (0x0010, 0x0007, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
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
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF136000, 0x00000400)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000026,
                    }
                    FixedDMA (0x0013, 0x0004, Width32bit, )
                    FixedDMA (0x0012, 0x0005, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
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
            Name (_DEP, Package (0x01) { \_SB.PEP })
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

        Device (I2C0)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, One)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP) // Driver variant selection: B0 stepping
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF138000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000000A,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF138000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000000A,
                    }
                    FixedDMA (0x0017, 0x0000, Width32bit, )
                    FixedDMA (0x0016, 0x0001, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
            }
        }

        Device (I2C1)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x02)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP) // Driver variant selection: B0 stepping
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF139000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000039,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF139000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000039,
                    }
                    FixedDMA (0x0019, 0x0006, Width32bit, )
                    FixedDMA (0x0018, 0x0007, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

        }

        Device (I2C2)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x03)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Method (_HRV, 0, NotSerialized)
            {
                Return (0x02)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13A000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveLow, Shared, ,, )
                    {
                        0x0000000C,
                    }
                    FixedDMA (0x001B, 0x0006, Width32bit, )
                    FixedDMA (0x001A, 0x0007, Width32bit, )
                })
                Return (RBUF)
            }

            Device (CHGR)
            {
                Name (_HID, "SPBT0001") // bind to SpbTestTool peripheral driver
                Name (_UID, One)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_CRS, 0, NotSerialized)
                {
                    Name (RBUF, ResourceTemplate ()
                    {
                        I2cSerialBusV2 (0x006A, ControllerInitiated, 0x000186A0,
                            AddressingMode7Bit, "\\_SB.I2C2",
                            0x00, ResourceConsumer, , Exclusive,
                            )
                    })
                    Return (RBUF)
                }
            }

        }

        Device (I2C3)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x04)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP) // Driver variant selection: B0 stepping
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13B000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002C,
                    }
                })
                Return (RBUF)
            }
        }

        Device (I2C4)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x05)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP) // Driver variant selection: B0 stepping
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13C000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002D,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13C000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002D,
                    }
                    FixedDMA (0x001D, 0x0004, Width32bit, )
                    FixedDMA (0x001C, 0x0005, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
            }
        }

        Device (I2C5)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x06)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP) // Driver variant selection: B0 stepping
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13D000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002E,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13D000,
                        0x00000400,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002E,
                    }
                    FixedDMA (0x001F, 0x0002, Width32bit, )
                    FixedDMA (0x001E, 0x0003, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
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
