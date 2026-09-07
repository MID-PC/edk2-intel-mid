/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembly of Apic.aml
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue (in hex)
 */

[000h 0000 004h]                   Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004 004h]                Table Length : 00000058
[008h 0008 001h]                    Revision : 01
[009h 0009 001h]                    Checksum : 00     /* Incorrect checksum, should be 7D */
[00Ah 0010 006h]                      Oem ID : "INTEL "
[010h 0016 008h]                Oem Table ID : "LANFORDC"
[018h 0024 004h]                Oem Revision : 00000003
[01Ch 0028 004h]             Asl Compiler ID : "MSFT"
[020h 0032 004h]       Asl Compiler Revision : 0100000D

[024h 0036 004h]          Local Apic Address : FEE00000
[028h 0040 004h]       Flags (decoded below) : 00000000
                         PC-AT Compatibility : 0

[02Ch 0044 001h]               Subtable Type : 00 [Processor Local APIC]
[02Dh 0045 001h]                      Length : 08
[02Eh 0046 001h]                Processor ID : 00
[02Fh 0047 001h]               Local Apic ID : 00
[030h 0048 004h]       Flags (decoded below) : 00000001
                           Processor Enabled : 1
                      Runtime Online Capable : 0

[034h 0052 001h]               Subtable Type : 00 [Processor Local APIC]
[035h 0053 001h]                      Length : 08
[036h 0054 001h]                Processor ID : 01
[037h 0055 001h]               Local Apic ID : 01
[038h 0056 004h]       Flags (decoded below) : 00000001
                           Processor Enabled : 1
                      Runtime Online Capable : 0

[03Ch 0060 001h]               Subtable Type : 00 [Processor Local APIC]
[03Dh 0061 001h]                      Length : 08
[03Eh 0062 001h]                Processor ID : 02
[03Fh 0063 001h]               Local Apic ID : 02
[040h 0064 004h]       Flags (decoded below) : 00000001
                           Processor Enabled : 1
                      Runtime Online Capable : 0

[044h 0068 001h]               Subtable Type : 00 [Processor Local APIC]
[045h 0069 001h]                      Length : 08
[046h 0070 001h]                Processor ID : 03
[047h 0071 001h]               Local Apic ID : 03
[048h 0072 004h]       Flags (decoded below) : 00000001
                           Processor Enabled : 1
                      Runtime Online Capable : 0

[04Ch 0076 001h]               Subtable Type : 01 [I/O APIC]
[04Dh 0077 001h]                      Length : 0C
[04Eh 0078 001h]                 I/O Apic ID : 04
[04Fh 0079 001h]                    Reserved : 00
[050h 0080 004h]                     Address : FEC00000
[054h 0084 004h]                   Interrupt : 00000000

Raw Table Data: Length 88 (0x58)

    0000: 41 50 49 43 58 00 00 00 01 00 49 4E 54 45 4C 20  // APICX.....INTEL 
    0010: 4C 41 4E 46 4F 52 44 43 03 00 00 00 4D 53 46 54  // LANFORDC....MSFT
    0020: 0D 00 00 01 00 00 E0 FE 00 00 00 00 00 08 00 00  // ................
    0030: 01 00 00 00 00 08 01 01 01 00 00 00 00 08 02 02  // ................
    0040: 01 00 00 00 00 08 03 03 01 00 00 00 01 0C 04 00  // ................
    0050: 00 00 C0 FE 00 00 00 00                          // ........
