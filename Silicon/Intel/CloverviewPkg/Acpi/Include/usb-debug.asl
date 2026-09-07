/** @file
  USB OTG0 KDNET / device-mode variant (A502CG / Acer Iconia A1-830).

  Included from Dsdt.asl inside Scope (\_SB) when KDNET_USB IS defined.

  This is deliberately a PASSIVE "present but unclaimed" declaration - it
  mirrors the working A502CG KDNET-over-USB setup, which the user confirmed
  was stripped to the bare minimum so that Windows binds NO driver to it:

    * NO _HID / no _CID, so neither usbehci.sys (Intel USB2 EHCI HID INT33B6)
      nor UfxChipidea.sys / any in-box driver can match and bind the controller,
      which would latch the Chipidea core into host mode and break device-mode
      KDNET. The KDNET/KDUSB debug transport is what drives the controller in
      device mode, using the namepath and base addresses from the DBG2 table.
    * no _PS0/_PS3/_DSM/register bring-up of any kind

  Only _CRS is kept (to reserve the 0xFFA60000 range and IRQ so no other
  driver is allocated them) and _STA reports present only, so the \_SB.OTG0
  namepath that DBG2 references still resolves.

  SPDX-License-Identifier: BSD-2-Clause-Patent
*/
        Device (OTG0)
        {
            Name (_ADR, Zero)

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFFA60000,         // Address Base
                        0x00000100,         // Address Length
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
