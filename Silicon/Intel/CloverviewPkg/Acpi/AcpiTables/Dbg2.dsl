/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembly of Dbg2.aml
 *
 * ACPI Data Table [DBG2]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue (in hex)
 */

[000h 0000 004h]                   Signature : "DBG2"    [Debug Port Table type 2]
[004h 0004 004h]                Table Length : 00000072
[008h 0008 001h]                    Revision : 00
[009h 0009 001h]                    Checksum : 00     /* Incorrect checksum, should be 8E */
[00Ah 0010 006h]                      Oem ID : "INTEL "
[010h 0016 008h]                Oem Table ID : "INTLDBGO"
[018h 0024 004h]                Oem Revision : 00000003
[01Ch 0028 004h]             Asl Compiler ID : "MSFT"
[020h 0032 004h]       Asl Compiler Revision : 0100000D

[024h 0036 004h]                 Info Offset : 0000002C
[028h 0040 004h]                  Info Count : 00000001

[02Ch 0044 001h]                    Revision : 00
[02Dh 0045 002h]                      Length : 0046
[02Fh 0047 001h]              Register Count : 02
[030h 0048 002h]             Namepath Length : 0010
[032h 0050 002h]             Namepath Offset : 0036
[034h 0052 002h]             OEM Data Length : 0000 [Optional field not present]
[036h 0054 002h]             OEM Data Offset : 0000 [Optional field not present]
[038h 0056 002h]                   Port Type : 8002
[03Ah 0058 002h]                Port Subtype : 0005
[03Ch 0060 002h]                    Reserved : 0000
[03Eh 0062 002h]         Base Address Offset : 0016
[040h 0064 002h]         Address Size Offset : 002E

[042h 0066 00Ch]       Base Address Register : [Generic Address Structure]
[042h 0066 001h]                    Space ID : 00 [SystemMemory]
[043h 0067 001h]                   Bit Width : 20
[044h 0068 001h]                  Bit Offset : 00
[045h 0069 001h]        Encoded Access Width : 00 [Undefined/Legacy]
[046h 0070 008h]                     Address : 00000000FFA60000


[04Eh 0078 00Ch]       Base Address Register : [Generic Address Structure]
[04Eh 0078 001h]                    Space ID : 00 [SystemMemory]
[04Fh 0079 001h]                   Bit Width : 20
[050h 0080 001h]                  Bit Offset : 00
[051h 0081 001h]        Encoded Access Width : 00 [Undefined/Legacy]
[052h 0082 008h]                     Address : 000000000B176000

[05Ah 0090 004h]                Address Size : 00000200
[05Eh 0094 004h]                Address Size : 00004000

[062h 0098 00Ah]                    Namepath : "\_SB.OTG0"

Raw Table Data: Length 114 (0x72)

    0000: 44 42 47 32 72 00 00 00 00 00 49 4E 54 45 4C 20  // DBG2r.....INTEL 
    0010: 49 4E 54 4C 44 42 47 4F 03 00 00 00 4D 53 46 54  // INTLDBGO....MSFT
    0020: 0D 00 00 01 2C 00 00 00 01 00 00 00 00 46 00 02  // ....,........F..
    0030: 10 00 36 00 00 00 00 00 02 80 05 00 00 00 16 00  // ..6.............
    0040: 2E 00 00 20 00 00 00 00 A6 FF 00 00 00 00 00 20  // ... ........... 
    0050: 00 00 00 60 17 0B 00 00 00 00 00 02 00 00 00 40  // ...`...........@
    0060: 00 00 5C 5F 53 42 2E 4F 54 47 30 00 00 00 00 00  // ..\_SB.OTG0.....
    0070: 00 00                                            // ..
