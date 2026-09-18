/*
  USB OTG0 KDNET / device-mode variant
  Stubbed, so that usbehci.sys never binds and KDNET can take over the USB device
*/
        Device (OTG0)
        {
            Name (_ADR, Zero)

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

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }
