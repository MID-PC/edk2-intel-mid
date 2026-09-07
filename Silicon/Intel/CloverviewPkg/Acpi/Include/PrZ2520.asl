//
// z2520 CPU performance/idle table (\_PR.P000..P003), included by the DSDT
// when the build selects SOC_VARIANT_Z2520 (the A502CG-class part).
//
// Extracted verbatim from the older Dsdt2.asl revision. Structure and
// rationale are identical to PrZ2560.asl (P000 owns the objects, APs mirror
// them, _PCT via FFixedHW/IA32_PERF_CTL, HW-C6 in _CST); only _PSS differs:
// the Z2520 is a 1.2 GHz part on a 100 MHz bus (PcdFSBClock = 100000000), so
// ratios 12..6 give 1200..600 MHz. Control/status words hold the ratio in
// bits 15:8 (IA32_PERF_CTL encoding).
//

Processor (P000, 0x00, 0x00000000, 0x00)
{
    Name (_PPC, Zero)

    Method (_PCT, 0, NotSerialized)
    {
        Return (Package (0x02)
        {
            ResourceTemplate () { Register (FFixedHW, 0x00, 0x00, 0x0000000000000000, ,) },
            ResourceTemplate () { Register (FFixedHW, 0x00, 0x00, 0x0000000000000000, ,) }
        })
    }

    Method (_PSD, 0, NotSerialized)
    {
        Return (Package (0x01)
        {
            Package (0x05) { 0x05, Zero, Zero, 0xFE, 0x04 }
        })
    }

    Method (_PSS, 0, NotSerialized)
    {
        Return (Package (0x07)
        {
            Package (0x06) { 0x04B0, 0x1770, 0x0A, 0x0A, 0x0C00, 0x0C00 },
            Package (0x06) { 0x044C, 0x1518, 0x0A, 0x0A, 0x0B00, 0x0B00 },
            Package (0x06) { 0x03E8, 0x12C0, 0x0A, 0x0A, 0x0A00, 0x0A00 },
            Package (0x06) { 0x0384, 0x1068, 0x0A, 0x0A, 0x0900, 0x0900 },
            Package (0x06) { 0x0320, 0x0E10, 0x0A, 0x0A, 0x0800, 0x0800 },
            Package (0x06) { 0x02BC, 0x0BB8, 0x0A, 0x0A, 0x0700, 0x0700 },
            Package (0x06) { 0x0258, 0x0960, 0x0A, 0x0A, 0x0600, 0x0600 }
        })
    }

    //
    // Idle states, the richest branch the stock Cpu0Cst SSDT can
    // select, limited to what exists here: C1 (MWAIT 0x00),
    // C2 (0x10) and HW-C6 (0x30). All FFixedHW / MWAIT-based, so no
    // chipset I/O ports are required. clvpep.sys logs
    // "HW-C6 not exposed in the _CST!" when this is missing.
    //
    Method (_CST, 0, NotSerialized)
    {
        Return (Package (0x04)
        {
            0x03,
            Package (0x04)
            {
                ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000000, 0x01,) },
                One, One, 0x03E8
            },
            Package (0x04)
            {
                ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000010, 0x01,) },
                0x02, 0x14, 0x01F4
            },
            Package (0x04)
            {
                ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000030, 0x03,) },
                0x03, 0x64, 0x64
            }
        })
    }
}

Processor (P001, 0x01, 0x00000000, 0x00)
{
    Method (_PPC, 0, NotSerialized) { Return (\_PR.P000._PPC) }
    Method (_PCT, 0, NotSerialized) { Return (\_PR.P000._PCT ()) }
    Method (_PSS, 0, NotSerialized) { Return (\_PR.P000._PSS ()) }
    Method (_PSD, 0, NotSerialized) { Return (\_PR.P000._PSD ()) }
    Method (_CST, 0, NotSerialized) { Return (\_PR.P000._CST ()) }
}

Processor (P002, 0x02, 0x00000000, 0x00)
{
    Method (_PPC, 0, NotSerialized) { Return (\_PR.P000._PPC) }
    Method (_PCT, 0, NotSerialized) { Return (\_PR.P000._PCT ()) }
    Method (_PSS, 0, NotSerialized) { Return (\_PR.P000._PSS ()) }
    Method (_PSD, 0, NotSerialized) { Return (\_PR.P000._PSD ()) }
    Method (_CST, 0, NotSerialized) { Return (\_PR.P000._CST ()) }
}

Processor (P003, 0x03, 0x00000000, 0x00)
{
    Method (_PPC, 0, NotSerialized) { Return (\_PR.P000._PPC) }
    Method (_PCT, 0, NotSerialized) { Return (\_PR.P000._PCT ()) }
    Method (_PSS, 0, NotSerialized) { Return (\_PR.P000._PSS ()) }
    Method (_PSD, 0, NotSerialized) { Return (\_PR.P000._PSD ()) }
    Method (_CST, 0, NotSerialized) { Return (\_PR.P000._CST ()) }
}