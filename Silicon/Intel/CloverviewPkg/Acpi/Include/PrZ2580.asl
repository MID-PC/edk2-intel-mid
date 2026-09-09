//
// z2580 CPU performance/idle table (\_PR.P000..P003), included by the DSDT
// when the build selects SOC_VARIANT_Z2580 (Asus ZenFone 6 A600CG/T00G).
//
// P000 carries the real performance / idle state objects; the APs mirror
// them, exactly as the stock Clover Trail PmRef SSDTs (Cpu0Ist / Cpu0Cst /
// ApIst / ApCst) do. The stock tables use 0x80000000 placeholders that the
// stock firmware patches at runtime; we cannot patch tables here, so the
// final values are declared.
//
// Why this is needed: clvpep.sys (ACPI\INT3395, the Z25xx/Z27xx Power Engine
// Plug-in) drives P-states through the PPM interface. With no _PCT/_PSS/_PPC/
// _PSD/_CST on the processor objects there is nothing for it to attach to, so
// nothing ever raises the ratio and the CPU stays at its boot ratio.
//
// _PCT uses FFixedHW, i.e. control goes through IA32_PERF_CTL (MSR 0x199) via
// the PPM/PEP path, not through I/O ports - the same shape the stock Cpu0Ist
// SSDT uses.
//
// _PSS is captured from the device's own stock firmware: A600CG's SFI FREQ
// table (dmesg: "SFI: FREQ E02C9, 0060"; raw table committed in A600CG/FREQ)
// declares SIX P-states - 2000/1866/1600/1333/933/800 MHz, all at 100 us
// transition latency. Each entry's control word holds the ratio in bits 15:8
// (15/14/12/10/7/6 on the ~133 MHz interface clock) and the VFF/voltage hint
// in the low byte; the full 16-bit words are used verbatim from the table
// (0x0F64/0x0E5E/0x0C57/0x0A50/0x0749/0x0648). Power values are not in the
// FREQ table; they are anchored to the same 3 W TDP shape PrZ2560.asl uses,
// scaled up for the 2.0 GHz upper bin.
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
        Return (Package (0x06)
        {
            Package (0x06) { 0x07D0, 0x0EA6, 0x64, 0x64, 0x0F64, 0x0F64 },
            Package (0x06) { 0x074A, 0x0D48, 0x64, 0x64, 0x0E5E, 0x0E5E },
            Package (0x06) { 0x0640, 0x0BB8, 0x64, 0x64, 0x0C57, 0x0C57 },
            Package (0x06) { 0x0535, 0x0898, 0x64, 0x64, 0x0A50, 0x0A50 },
            Package (0x06) { 0x03A5, 0x0578, 0x64, 0x64, 0x0749, 0x0749 },
            Package (0x06) { 0x0320, 0x044C, 0x64, 0x64, 0x0648, 0x0648 }
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