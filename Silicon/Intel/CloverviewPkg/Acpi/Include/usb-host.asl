/**
  USB OTG0 host-mode variant

  Windows binds ACPI\\PNP0D20 / usbehci.sys; _PS0/_STA/_DSM force the Chipidea core into
  host mode and program the TUSB1211 ULPI repeater
*/
        Device (OTG0)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B6")
            Name (_CID, "ACPI\\PNP0D20")
            Name (_UID, 0x02)
            Name (_HRV, 0x02)

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFFA60000,
                        0x00000100,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveLow, Shared, ,, )
                    {
                        0x20,
                    }
                })
                Return (RBUF)
            }

            OperationRegion (KEYS, SystemMemory, 0xFFA60000, 0x0100)
            Field (KEYS, DWordAcc, NoLock, WriteAsZeros)
            {
                Offset (0x54),
                UFOR,   32
            }

            Field (KEYS, ByteAcc, NoLock, Preserve)
            {
                Offset (0xF8),
                UFOS,   8
            }

            Field (KEYS, DWordAcc, NoLock, WriteAsZeros)
            {
                Offset (0xB4),
                UFOT,   32
            }

            //
            // ULPI viewport (ULPIVP, BAR+0x60)
            // OtgHostDxe sequence: OTG Control 0x0B pulldowns/DrvVbus + Function Control 0x05/0x06 leave reset, SuspendM
            // Write layout: RUN (bit 30) | RW (bit 29) | address << 16 | data
            //
            Field (KEYS, DWordAcc, NoLock, Preserve)
            {
                Offset (0x60),
                UVPT,   32
            }

            Field (KEYS, DWordAcc, NoLock, Preserve)
            {
                Offset (0x30),
                UCMD,   32,
                USTS,   32
            }

            Field (KEYS, DWordAcc, NoLock, Preserve)
            {
                Offset (0x38),
                UINT,   32
            }

            Field (KEYS, DWordAcc, NoLock, Preserve)
            {
                Offset (0x70),
                UCFG,   32
            }

            Field (KEYS, DWordAcc, NoLock, Preserve)
            {
                Offset (0x74),
                UPRT,   32
            }

            Field (KEYS, DWordAcc, NoLock, Preserve)
            {
                Offset (0xF4),
                UOTG,   32
            }

            Method (_PS3, 0, NotSerialized)
            {

            }

            Method (_PS0, 0, NotSerialized)
            {
                UFOT &= 0xFFBFFFFE

                // Full bring-up on every D0 entry
                UFOS = 0x23                     // USBMODE_EX = HC | VBPS
                UFOT &= 0xFFBFFFFE              // HOSTPC1: clear PHCD + ASUS
                UFOR &= 0xFFC0FFFF
                UFOR |= 0x000B0000              // TXFILLTUNING TXFIFOTHRES

                UVPT = 0x60060020               // FuncCtrl clear: leave reset
                Local0 = 0x64
                While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                {
                    Stall (0x0A)
                    Local0--
                }

                UVPT = 0x60050040               // FuncCtrl set: SuspendM
                Local0 = 0x64
                While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                {
                    Stall (0x0A)
                    Local0--
                }

                UVPT = 0x600B0066               // OtgCtrl: DrvVbus + pulldowns
                Local0 = 0x64
                While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                {
                    Stall (0x0A)
                    Local0--
                }

            }

            Method (_STA, 0, NotSerialized)
            {
                If ((USBS == Zero))
                {
                    If ((UOFD == One))
                    {
                        UFOT |= 0x00800000
                    }

                    // Re-arm host mode from _STA as well
                    // Windows' most frequently evaluated hook
                    UFOS = 0x23
                    UFOT &= 0xFFBFFFFE
                    UFOR &= 0xFFC0FFFF
                    UFOR |= 0x000B0000

                    UVPT = 0x60060020
                    Local0 = 0x64
                    While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                    {
                        Stall (0x0A)
                        Local0--
                    }

                    UVPT = 0x60050040
                    Local0 = 0x64
                    While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                    {
                        Stall (0x0A)
                        Local0--
                    }

                    UVPT = 0x600B0066
                    Local0 = 0x64
                    While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                    {
                        Stall (0x0A)
                        Local0--
                    }

                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }

            // _DSM: function 0 reports functions {0,1}; function 1 performs the
            // host-mode/PHY bring-up (USBMODE_EX = 0x23, HOSTPC1 software
            // suspend clear, TXFILLTUNING TXFIFOTHRES = 0x0B on stepping 1/2)
            Method (_DSM, 4, Serialized)
            {
                If ((Arg0 == ToUUID ("ce2ee385-00e6-48cb-9f05-2edb927c4899")))
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
                        UFOS = 0x23
                        UFOT &= 0xFFFFFFFE
                        If (((STEP == One) || (STEP == 0x02)))
                        {
                            UFOR &= 0xFFC0FFFF
                            UFOR |= 0x000B0000
                        }

                        // TUSB1211 ULPI writes: FuncCtrl clear 0x20 (leave PHY
                        // reset), set SuspendM (0x40); OtgCtrl 0x66 (DrvVbus +
                        // pulldowns); each polled until the viewport RUN bit
                        // (0x40000000) clears, bounded so a silent PHY cannot
                        // hang the method
                        UVPT = 0x60060020
                        Local0 = 0x64
                        While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                        {
                            Stall (0x0A)
                            Local0--
                        }

                        UVPT = 0x60050040
                        Local0 = 0x64
                        While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                        {
                            Stall (0x0A)
                            Local0--
                        }

                        UVPT = 0x600B0066
                        Local0 = 0x64
                        While (((UVPT & 0x40000000) != Zero) && (Local0 > Zero))
                        {
                            Stall (0x0A)
                            Local0--
                        }

                        Return (Zero)
                    }
                }

                Return (One)
            }
        }
