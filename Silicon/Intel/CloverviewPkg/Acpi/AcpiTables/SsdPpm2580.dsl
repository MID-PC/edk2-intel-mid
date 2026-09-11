/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembling to symbolic ASL+ operators
 *
 * Disassembly of SsdPpm2580.aml
 *
 * Original Table Header:
 *     Signature        "SSDT"
 *     Length           0x000002F6 (758)
 *     Revision         0x02
 *     Checksum         0xB5
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
                Return (Package (0x06)
                {
                    Package (0x06)
                    {
                        0x07D0, 
                        0x0EA6, 
                        0x64, 
                        0x64, 
                        0x0F64, 
                        0x0F64
                    }, 

                    Package (0x06)
                    {
                        0x074A, 
                        0x0D48, 
                        0x64, 
                        0x64, 
                        0x0E5E, 
                        0x0E5E
                    }, 

                    Package (0x06)
                    {
                        0x0640, 
                        0x0BB8, 
                        0x64, 
                        0x64, 
                        0x0C57, 
                        0x0C57
                    }, 

                    Package (0x06)
                    {
                        0x0535, 
                        0x0898, 
                        0x64, 
                        0x64, 
                        0x0A50, 
                        0x0A50
                    }, 

                    Package (0x06)
                    {
                        0x03A5, 
                        0x0578, 
                        0x64, 
                        0x64, 
                        0x0749, 
                        0x0749
                    }, 

                    Package (0x06)
                    {
                        0x0320, 
                        0x044C, 
                        0x64, 
                        0x64, 
                        0x0648, 
                        0x0648
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

