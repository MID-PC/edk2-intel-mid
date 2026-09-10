/** @file
  USB OTG0 host-mode variant (A502CG).

  Included from Dsdt.asl inside Scope (\\_SB) when KDNET_USB is NOT defined
  (the default). This is the full host-mode bring-up: Windows binds the controller
  as ACPI\\PNP0D20 / usbehci.sys and _PS0/_STA/_DSM force the Chipidea core into
  host mode and program the TUSB1211 ULPI repeater.

  SPDX-License-Identifier: BSD-2-Clause-Patent
*/

#ifdef T00K_USB_TRACE
#include "../Include/UsbTrace.h"
#endif

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

#ifdef T00K_USB_TRACE
            // Old CloverTrailPkg CTTR layout; STA marker and PS0 snapshot.
            OperationRegion (CTTR, SystemMemory, CT_USB_TRACE_BASE, CT_USB_TRACE_SIZE)
            Field (CTTR, DWordAcc, NoLock, Preserve)
            {
                TSIG,   32,     // signature 'CTAC' written at end of _PS0
                TSTA,   32,     // _STA evaluations
                TPS0,   32,     // _PS0 (D0 entry) calls
                TPS3,   32,     // unused (PS0-only probe)
                TDS0,   32,     // unused (PS0-only probe)
                TDS1,   32,     // unused (PS0-only probe)
                TMOD,   32,     // USBMODE_EX (BAR+0xF8) at end of _PS0
                THPC,   32,     // HOSTPC1 (BAR+0xB4) at end of _PS0
                TCMD,   32,     // USBCMD (BAR+0x30) at end of _PS0
                TSTS,   32,     // USBSTS (BAR+0x34) at end of _PS0
                TPRT,   32,     // PORTSC1 (BAR+0x74) at end of _PS0
                TVPT,   32,     // ULPIVP (BAR+0x60) at end of _PS0
                TOTG,   32,     // OTGSC (BAR+0xF4) at end of _PS0
                TFIL,   32,     // TXFILLTUNING (BAR+0x54)
                TINT,   32,     // USBINTR (BAR+0x38)
                TCFG,   32      // CONFIGFLAG (BAR+0x70)
            }

#endif
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
            // ULPI viewport (Chipidea ULPIVP, BAR+0x60). Windows has no driver
            // for the external TI TUSB1211 ULPI repeater, and usbehci.sys never
            // touches this register, so the A-device termination the repeater
            // needs (DpPulldown/DmPulldown/DrvVbus/DrvVbusExternal in ULPI OTG
            // Control 0x0B, plus leaving PHY reset and setting SuspendM in
            // Function Control 0x05/0x06) must be programmed from _DSM here -
            // exactly the sequence OtgHostDxe performs for the UEFI-side EHCI
            // stack. The ACPI trace showed Windows really does evaluate _STA,
            // _PS0 and _DSM function 1, so this is the one bring-up step that
            // was still missing under Windows.
            //
            // Write layout: RUN (bit 30) | RW (bit 29) | address << 16 | data.
            //
            Field (KEYS, DWordAcc, NoLock, Preserve)
            {
                Offset (0x60),
                UVPT,   32
            }

            //
            // Read-only views used by the debug snapshot at the end of _PS0.
            //
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

            //
            // Power management, as in the stock table: bit 22 of HOSTPC1 is the
            // PHY low-power (PHCD) control and bit 0 is the software suspend.
            //
            Method (_PS3, 0, NotSerialized)
            {
                //
                // DO NOT park the PHY here.
                //
                // The Windows trace counted hundreds of D3 entries after only a
                // few plug/unplug cycles: with PEP attached (PEPP = One, _S0W =
                // 0x03) Windows runtime-idles this controller constantly, while
                // _PS0 (D0 entry) runs only twice. The stock _PS3 sets HOSTPC1
                // bit 22 (PHCD), which stops the ULPI clock from the TUSB1211.
                // On this dual-role core the PHY clock is also what keeps
                // USBMODE.CM = 3 and the A-device termination alive, so after
                // the first idle transition the port is powered but dead -
                // exactly the observed behaviour (PORTSC CCS=1/PE=1, USBSTS
                // interrupts pending, no enumeration).
                //
                // Leaving the PHY running costs idle power only.
                //
                // UFOT |= 0x00400000
            }

            Method (_PS0, 0, NotSerialized)
            {
                UFOT &= 0xFFBFFFFE

                //
                // Full host-mode/PHY bring-up on every D0 entry, not just in
                // _DSM function 1.
                //
                // The WinPE trace proved Windows enumerates OTG0, assigns
                // resources (Control\AllocConfig present) and binds usbehci,
                // and that _DSM function 1 runs exactly once (DSMfn1 = 1).
                // usbehci then issues HCRESET, which on this Chipidea core
                // clears USBMODE.CM back to device/idle, drops TXFILLTUNING and
                // re-arms HOSTPC1's automatic PHY low-power bit - exactly the
                // state that made every UEFI-side transfer fail with
                // "HC halted at entrance" until OtgHostDxe/EhciDxe re-asserted
                // it after the reset. Windows has no hook for that, but it does
                // evaluate _PS0 on each D0 transition, so the sequence is
                // repeated here as well.
                //
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
#ifdef T00K_USB_TRACE
                // Snapshot only here, after the existing USB host bring-up.
                // Mark complete last so a partial write is not a valid CTAC.
                TSIG = Zero
                TPS0 = (TPS0 + One)
                TMOD = UFOS
                THPC = UFOT
                TCMD = UCMD
                TSTS = USTS
                TPRT = UPRT
                TVPT = UVPT
                TOTG = UOTG
                TFIL = UFOR
                TINT = UINT
                TCFG = UCFG
                TSIG = CT_USB_TRACE_SIGNATURE
#endif
            }

            Method (_STA, 0, NotSerialized)
            {
#ifdef T00K_USB_TRACE
                TSTA = (TSTA + One)
                TSIG = CT_USB_TRACE_SIGNATURE
#endif
                If ((USBS == Zero))
                {
                    If ((UOFD == One))
                    {
                        UFOT |= 0x00800000
                    }

                    //
                    // Re-arm host mode from _STA as well.
                    //
                    // The Windows trace showed _STA is evaluated many times
                    // (STA = 18) while _PS0 runs only twice (PS0 = 2) and
                    // _DSM function 1 only once. usbehci.sys issues HCRESET
                    // after those calls, and on this Chipidea core HCRESET
                    // clears USBMODE.CM back to device/idle, drops
                    // TXFILLTUNING and re-arms HOSTPC1's PHY low-power bit -
                    // the same state that made every UEFI-side transfer fail
                    // with "HC halted at entrance" until EhciDxe re-asserted
                    // it. _STA is the most frequently evaluated hook Windows
                    // gives us, so the whole bring-up is repeated here:
                    // USBMODE_EX = 0x23 (HC | VBPS), HOSTPC1 PHCD/ASUS clear,
                    // TXFILLTUNING TXFIFOTHRES = 0x0B and the three TUSB1211
                    // ULPI writes (leave PHY reset, SuspendM, DrvVbus plus
                    // Dp/Dm pulldowns for A-device termination).
                    //
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

            //
            // USB Controller _DSM. Function 0 reports functions {0,1};
            // function 1 performs the host-mode/PHY bring-up:
            //   USBMODE_EX = 0x23 (HC | VBPS), HOSTPC1 software suspend clear,
            //   TXFILLTUNING TXFIFOTHRES = 0x0B on stepping 1/2.
            //
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

                        //
                        // TUSB1211 ULPI host (A-device) setup - see the UVPT
                        // comment above. Function Control: leave PHY reset
                        // (clear 0x20 via register 0x06), set SuspendM (set
                        // 0x40 via register 0x05); OTG Control: set
                        // DrvVbusExternal|DrvVbus|DmPulldown|DpPulldown (0x66
                        // via register 0x0B). Each write is polled until the
                        // viewport RUN bit (0x40000000) clears, bounded so a
                        // silent PHY cannot hang the method.
                        //
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
