/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembling to symbolic ASL+ operators
 *
 * Disassembly of SsdPpm2520.aml
 *
 * Original Table Header:
 *     Signature        "SSDT"
 *     Length           0x00000309 (777)
 *     Revision         0x02
 *     Checksum         0x02
 *     OEM ID           "INTEL"
 *     OEM Table ID     "CLVPRM"
 *     OEM Revision     0x00000001 (1)
 *     Compiler ID      "INTL"
 *     Compiler Version 0x20251212 (539300370)
 */
DefinitionBlock ("", "SSDT", 2, "INTEL", "CLVPRM", 0x00000001)
{
    Scope (\_PR)
    {
        Processor (P000, 0x00, 0x00000000, 0x00)
        {
            Name (_PPC, Zero)  // _PPC: Performance Present Capabilities
            Method (_PCT, 0, NotSerialized)  // _PCT: Performance Control
            {
                Return (Package (0x02)
                {
                    ResourceTemplate ()
                    {
                        Register (FFixedHW, 
                            0x00,               // Bit Width
                            0x00,               // Bit Offset
                            0x0000000000000000, // Address
                            ,)
                    }, 

                    ResourceTemplate ()
                    {
                        Register (FFixedHW, 
                            0x00,               // Bit Width
                            0x00,               // Bit Offset
                            0x0000000000000000, // Address
                            ,)
                    }
                })
            }

            Method (_PSD, 0, NotSerialized)  // _PSD: Power State Dependencies
            {
                Return (Package (0x01)
                {
                    Package (0x05)
                    {
                        0x05, 
                        Zero, 
                        Zero, 
                        0xFE, 
                        0x04
                    }
                })
            }

            Method (_PSS, 0, NotSerialized)  // _PSS: Performance Supported States
            {
                Return (Package (0x07)
                {
                    Package (0x06)
                    {
                        0x04B0, 
                        0x1770, 
                        0x0A, 
                        0x0A, 
                        0x0C00, 
                        0x0C00
                    }, 

                    Package (0x06)
                    {
                        0x044C, 
                        0x1518, 
                        0x0A, 
                        0x0A, 
                        0x0B00, 
                        0x0B00
                    }, 

                    Package (0x06)
                    {
                        0x03E8, 
                        0x12C0, 
                        0x0A, 
                        0x0A, 
                        0x0A00, 
                        0x0A00
                    }, 

                    Package (0x06)
                    {
                        0x0384, 
                        0x1068, 
                        0x0A, 
                        0x0A, 
                        0x0900, 
                        0x0900
                    }, 

                    Package (0x06)
                    {
                        0x0320, 
                        0x0E10, 
                        0x0A, 
                        0x0A, 
                        0x0800, 
                        0x0800
                    }, 

                    Package (0x06)
                    {
                        0x02BC, 
                        0x0BB8, 
                        0x0A, 
                        0x0A, 
                        0x0700, 
                        0x0700
                    }, 

                    Package (0x06)
                    {
                        0x0258, 
                        0x0960, 
                        0x0A, 
                        0x0A, 
                        0x0600, 
                        0x0600
                    }
                })
            }

            Method (_CST, 0, NotSerialized)  // _CST: C-States
            {
                Return (Package (0x04)
                {
                    0x03, 
                    Package (0x04)
                    {
                        ResourceTemplate ()
                        {
                            Register (FFixedHW, 
                                0x01,               // Bit Width
                                0x02,               // Bit Offset
                                0x0000000000000000, // Address
                                0x01,               // Access Size
                                )
                        }, 

                        One, 
                        One, 
                        0x03E8
                    }, 

                    Package (0x04)
                    {
                        ResourceTemplate ()
                        {
                            Register (FFixedHW, 
                                0x01,               // Bit Width
                                0x02,               // Bit Offset
                                0x0000000000000010, // Address
                                0x01,               // Access Size
                                )
                        }, 

                        0x02, 
                        0x14, 
                        0x01F4
                    }, 

                    Package (0x04)
                    {
                        ResourceTemplate ()
                        {
                            Register (FFixedHW, 
                                0x01,               // Bit Width
                                0x02,               // Bit Offset
                                0x0000000000000030, // Address
                                0x03,               // Access Size
                                )
                        }, 

                        0x03, 
                        0x64, 
                        0x64
                    }
                })
            }
        }

        Processor (P001, 0x01, 0x00000000, 0x00)
        {
            Method (_PPC, 0, NotSerialized)  // _PPC: Performance Present Capabilities
            {
                Return (\_PR.P000._PPC)
            }

            Method (_PCT, 0, NotSerialized)  // _PCT: Performance Control
            {
                Return (\_PR.P000._PCT ())
            }

            Method (_PSS, 0, NotSerialized)  // _PSS: Performance Supported States
            {
                Return (\_PR.P000._PSS ())
            }

            Method (_PSD, 0, NotSerialized)  // _PSD: Power State Dependencies
            {
                Return (\_PR.P000._PSD ())
            }

            Method (_CST, 0, NotSerialized)  // _CST: C-States
            {
                Return (\_PR.P000._CST ())
            }
        }

        Processor (P002, 0x02, 0x00000000, 0x00)
        {
            Method (_PPC, 0, NotSerialized)  // _PPC: Performance Present Capabilities
            {
                Return (\_PR.P000._PPC)
            }

            Method (_PCT, 0, NotSerialized)  // _PCT: Performance Control
            {
                Return (\_PR.P000._PCT ())
            }

            Method (_PSS, 0, NotSerialized)  // _PSS: Performance Supported States
            {
                Return (\_PR.P000._PSS ())
            }

            Method (_PSD, 0, NotSerialized)  // _PSD: Power State Dependencies
            {
                Return (\_PR.P000._PSD ())
            }

            Method (_CST, 0, NotSerialized)  // _CST: C-States
            {
                Return (\_PR.P000._CST ())
            }
        }

        Processor (P003, 0x03, 0x00000000, 0x00)
        {
            Method (_PPC, 0, NotSerialized)  // _PPC: Performance Present Capabilities
            {
                Return (\_PR.P000._PPC)
            }

            Method (_PCT, 0, NotSerialized)  // _PCT: Performance Control
            {
                Return (\_PR.P000._PCT ())
            }

            Method (_PSS, 0, NotSerialized)  // _PSS: Performance Supported States
            {
                Return (\_PR.P000._PSS ())
            }

            Method (_PSD, 0, NotSerialized)  // _PSD: Power State Dependencies
            {
                Return (\_PR.P000._PSD ())
            }

            Method (_CST, 0, NotSerialized)  // _CST: C-States
            {
                Return (\_PR.P000._CST ())
            }
        }
    }
}

