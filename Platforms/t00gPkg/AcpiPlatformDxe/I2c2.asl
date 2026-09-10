/** @file
  T00G I2C2 / 0x6A test endpoint. Diagnostics temporarily disabled.
  SPDX-License-Identifier: BSD-2-Clause-Patent
  MMIO from iomem; GSI 12/high remains the provisional W511 routing.
**/
Device (I2C2)
{
    Name (_HID, "INT33B1")
    Name (_CID, "INT33B1")
    Name (_UID, 0x03)
    Name (_DDN, "Cloverview I2C controller 2 (PIO diagnostics)")
    Name (_DEP, Package (0x02) { \_SB.PEP, \_SB.IPC })
    Method (_HRV, 0, NotSerialized) { Return (\STEP) }

    Name (RBUF, ResourceTemplate ()
    {
        Memory32Fixed (ReadWrite, 0xFF13A000, 0x00000400)
        // Provisional board routing: stock W511 I2C2 uses GSI 12/high.
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, ) { 0x0C }
    })

    // I2C diagnostics temporarily removed; keep the controller/test endpoint.
    Method (_STA, 0, NotSerialized) { Return (0x0F) }
    Method (_CRS, 0, NotSerialized) { Return (RBUF) }

    Device (SPBT)
    {
        // Preserve the user-selected test-driver HID.
        Name (_HID, "SPBT0001")
        Name (_UID, One)
        Name (_DDN, "I2C2 7-bit 0x6A test endpoint")
        Name (_DEP, Package (One) { \_SB.I2C2 })
        Name (_CRS, ResourceTemplate ()
        {
            I2cSerialBus (0x006A, ControllerInitiated, 100000,
                AddressingMode7Bit, "\\_SB.I2C2", 0x00, ResourceConsumer, ,)
        })
        Method (_STA, 0, NotSerialized) { Return (0x0F) }
    }
}
