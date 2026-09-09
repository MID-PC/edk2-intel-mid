/** @file
  DSDT for Asus A502CG (Intel Atom Z2560/Z2580, Clover Trail+), PEP-focused
  revision.

  Goal of this revision
  ---------------------
  Bring up the Windows Power Engine Plug-in (clvpep.sys, ACPI\INT3395) with
  the resources and devices it requires, and (almost) nothing that depends on
  it yet. Reverse-engineering of the Z2760 clvpep.sys showed the PEP does NOT
  talk to its client devices over ACPI: it maps the SoC power blocks directly
  (MmMapIoSpace) and reaches the SCU/PUNIT over IPC + IOSF sideband. The only
  ACPI it needs is (a) its own INT3395 device with the presence _DSM, and
  (b) that the OS not hand the blocks it maps to anyone else. This table
  therefore keeps only:

    - \_PR processor objects: the PPM (P-state/C-state) surface the PEP
      drives via IA32_PERF_CTL (MSR 0x199) and MWAIT C-states
      (_PSS/_PCT/_PSD/_PPC/_CST, including HW-C6 in _CST).
    - \_SB.SYSR (PNP0C02): reserves the exact MMIO the PEP maps directly so
      no driver claims it:
        * 0xFF11D000  North-Complex PM unit (iomem_A502CG: intel_pmu_driver)
        * 0xFF11C000  SCU IPC-1 mailbox     (iomem_A502CG: intel_scu_ipc)
        * 0x3EF00000  PCI MMCONFIG - IOSF sideband / MBI / PUNIT fallback
                      path (base confirmed by the SFI MCFG table; NOT the
                      Z2760's 0xE0000000)
    - \_SB.PEP (INT3395 / _CID PNP0D80): the plug-in target itself, with the
      b8febfe0-baf8-454b-aecd-49fb91137b21 _DSM whose function 1 sets \PEPP.
    - \_SB.IPC (INT33B5): SCU IPC controller (0xFF11C000 + read-only mailbox
      at 0xFFFF7FC0), the one PEP-required companion device that IS enumerated
      over ACPI.
    - \_SB.PWRB (PNP0C0C): power button; MSIC/PMIC-serviced (SFI DEVS:
      msic_power_btn), independent of the PEP.
    - \_SB.SDC0 (INT33BB / PNP0D40): SD card host controller, restored
      WITHOUT its stock _DEP on \_SB.PEP - the bootloader already powers it
      on, so it must not wait for (or be power-managed by) the PEP.
    - \_SB.OTG0 (INT33B6): USB, kept verbatim except for PEP decoupling (see
      below).
    - All SIX \_SB.I2Cn (INT33B1) Designware I2C controllers declared (buses
      0 through 5 at 0xFF138000 + bus * 0x1000). GSIs confirmed against the
      board's own Linux IRQ map (interrupts.txt): I2C0 = 0x0A, I2C1 = 0x39,
I2C2 = 0x0C, I2C3 = 0x2C, I2C4 = 0x2D, I2C5 = 0x2E; _UID = bus + 1.
    - Like SPI and UART, I2C controllers now take the Z2760 SBUF (FixedDMA)
      arm: I2C0/1/2/4/5 carry GDMS request lines (I2C3 is the one Z2760
      controller with no DMA even on C0). The earlier "no satisfiable GDMS
      DMA engine" Code 10 was observed WITHOUT the CSRT request-line map;
      with CSRT published and the GDMS/UDMS HAL extensions bound, all three
      serial families get real DMA. STEP drives _HRV (= One, B0); controllers
      with a slave on them (I2C1 CODC, I2C2 CHGR/FGAS/TOUC, I2C5 ACC0) keep
      their PIO fallback for STEP == 0.
      Children: \_SB.I2C1.CODC (INTCFFD = "I2C CODEC Device Driver"
              ssti2c.inf, the CS42L73 audio codec @ 0x4A with codec-int
              GpioInt on GPO0 pin 0x20, docked via AUD1's _DEP), \_SB.I2C2
      CHGR/FGAS (SpbTestTool, smb347 @ 0x6A / max17047 @ 0x36) + TOUC (MSHW1003,
      FT6236 @ 0x38), \_SB.I2C5.ACC0 (BMA250E, bosch @ 0x18). Buses 0, 3 and 4
      are bare controllers (no subdevices yet; bus 4 holds the cameras on stock
      Android).
     - \_SB.SDC1 (INT33BB, NEW in this revision): Wi-Fi SDIO host controller
       for the Broadcom BCM4330 at 0xFFA48000 (PCI 00:04.1 [8086:08FA], mmc2),
       GSI 0x2A, _UID 0x03 (unique per _HID, not colliding with SDC0's 0x02),
       _DEP on PEP + GPO1. \_SB.SDC1._PS0 asserts GPO1.WLEN to power up the
       WLAN chip; child \_SB.SDC1.BRCM (_ADR 1, _RMV = 0) has the WLAN OOB
       host-interrupt GpioInt (\\_SB.GPO0 pin 0x02, "WLAN-interrupt") plus
       _PRW/_S0W/_S4W and WLEN power methods, matching the Z2760 reference.
       bcmdhd63.inf (Drivers/WIFI) binds function 1 via
       SD\VID_02D0&PID_4330&FN_1.

  Devices REMOVED in this revision (each carried _DEP on \_SB.PEP, directly
  or through a GPIO bank, i.e. they actually require the PEP to start):
    EMM0 (eMMC). Dropped for now so the PEP itself can be brought up and
    validated first; its MMIO stays covered by SYSR so nothing else grabs it,
    and it can be layered back later. AUD1 (SST audio engine) was RE-ADDED in
    this revision - see the AUD1 block below - since the PEP is now known-good
    on Windows (the reservation windows for the old MST/ASD do not overlap it).

  Audio (\_SB.AUD1 + \_SB.I2C1.CODC)
  -----------------------------------
  The A502CG has the Intel SST (Smart Sound Technology) low-power audio
  engine plus a Cirrus Logic CS42L73 codec on I2C bus 1 @ 0x4A (stock SFI
  DEVS "cs42l73 @ 0x4A", dmesg-stock "Cirrus Logic CS42L73, Revision: B0").
  SST is NOT HD Audio: the PCM path is DSP/SRAM (the "sst" MEG/AIO) driven
  by the Windows isst.sys (isstrtc.inf/isst.inf), and the codec control is
  I2C (ssti2c.sys, "I2C CODEC Device Driver", binds ACPI\INTCFFD), with the
  codec interrupt on GPO0 pin 0x20 (SFI GPIO "gpio_codec_int" = pin 32).
  TWO nodes are declared:
   - \_SB.AUD1 _HID "INT33B3" (SST engine): the Cirrus isst.inf binds
     ACPI\INT33B3 (ships + loads cirrus_fw_sst.bin); the Realtek isstrtc.inf
     binds ACPI\INTC33B3 and the reference DSDT's Realtek board used that, so
     we use INT33B3 to select the Cirrus lane for the CS42L73). MMIO/GSI from
     this board's own
     iomem + interrupts: the engine owns 0xFFAE8000/0x1000 (BAR1) and
     0xFFFF1000/0x2000 (BAR2) plus the big DSP SRAM 0xFFB00000/0x80000
     (BAR3) and 0xFFB80000/0x80000 (BAR4); IRQ 0x1E is the driver's own
     "intel_sst_driver" (30); the i2s ssp ports are GSI 0x41/0x42 (65/66),
     matching interrupts.txt "i2s ssp". The 0xFFA22000/0x400 BAR0 window is
     the SST shim/PCP and is included as the reference does.
   - \_SB.I2C1.CODC becomes the real codec (was SPBT0001 SpbTestTool
     placeholder): _HID "INTCFFD" (+_CID) so ssti2c.sys binds, I2cSerialBus
     0x4A on bus 1, and the codec-int GpioInt+GpioIo on GPO0 pin 0x20 like
     the reference RTEK node (which used pin 0x33; this board wires the
     codec int to AON pin 32). AUD1._DEP lists { PEP, I2C1.CODC } exactly
     like the reference AUD1._DEP { PEP, I2C5.RTEK }.

  GPIO (\_SB.GPO0, \_SB.GPO1) and button array (\_SB.TBAD)
  --------------------------------------------------------
  BOTH Cloverview GPIO banks are IN (ported from the working DSDT2, known-good
  GPIO + WLAN state for this board): the AON bank GPO0 (INT33B2, UID 1,
  MMIO 0xFF119000, GSI 0x15 = IOAPIC id2 input 21, ActiveLow Shared) and the
  Corewell bank GPO1 (INT33B2, UID 2, MMIO 0xFF13F000, GSI 0x38 = IOAPIC
  id2 input 56, ActiveLow Shared, the real PCI IRQ from 00:03.5). GPO1 carries
  a GeneralPurposeIo operation region (GPOP) with a single-bit field WLEN
  (CORE bank pin 0x4A = global 170, the WLAN_EN pad); GPO1._REG sets AVBL=1
  once GpioClv.sys attaches. Both banks use _DEP on \_SB.PEP and fixed
  _HRV Zero. TBAD (INTCFD9, PNP0C40 5-button array) has _UID 0x10, _DEP on
  GPO0, and uses in-range but unused pins (1/4/5) for the power/home/
  rotation-lock slots; volume-up/down are AON pins 30/31. Index 1 (Home) uses
  ActiveLow/PullUp so it idles as released, avoiding the phantom Win+VolUp
  chord.

  USB (\_SB.OTG0)
  ---------------
  Kept untouched EXCEPT that its PEP coupling is removed: the controller is
  in forced host mode, so it no longer carries _DEP on \_SB.PEP and the
  PEPP-gated _S0W wake method is dropped. With no _S0W Windows will not
  runtime-idle the controller into D3 (the stock PEP path did that 512:2
  against _PS0 and parked the PHY), which is what a forced-host, always-on
  port wants. All host-mode/PHY bring-up in _STA/_PS0/_DSM is as before. \PEPP
  still exists (set by \_SB.PEP._DSM function 1) but no device reads it any more.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

DefinitionBlock ("Dsdt.aml", "DSDT", 0x01, "INTEL ", "CLOVERVW", 0x00000011)
{
    //
    // System state packages, same values as the stock Clover Trail DSDT.
    //
    Name (\_S0, Package (0x04) { 0x05, 0x00, 0x00, 0x00 })
    Name (\_S5, Package (0x04) { 0x00, 0x00, 0x00, 0x00 })

    //
    // Set to One by \_SB.PEP._DSM function 1 when Windows' power engine
    // plug-in attaches. Retained for the PEP presence handshake; no device
    // gates on it in this revision (USB no longer reads it).
    //
    Name (\PEPP, Zero)

    //
    // Platform configuration values that the OTG0 and I2C1 methods read.
    // Declared as plain integers with this board's values.
    //
    // STEP: CPU stepping. CPU-Z reports stepping 1 on this board, so STEP is
    // One (B0 silicon): \_SB.I2C1._HRV returns it, making inteli2c.inf bind
    // through ACPI\VEN_INT&DEV_33B1&REV_0001 ("Z2760 B0 I2C Controller":
    // REVISION=1, FIFOSIZE=0x100, ForceDma=0, so the missing DMA controller
    // is not required). OTG0 is deliberately untouched: its hardcoded
    // _UID/_HRV of 2 stay, and its internal (STEP == One) || (STEP == 0x02)
    // checks still pass with STEP = One.
    //
    Name (\STEP, One)
    Name (\USBS, 0x00)
    Name (\UOFD, 0x00)
    // SPD0/SPI1/SPI2: the earlier SPI FixedDMA experiment was reverted because
    // the GDMS HAL extension had no satisfiable DMA engine without the CSRT;
    // with the board-corrected CSRT installed those request lines are now
    // serviced again (see the SPI1/SPI2 node comment).

    Scope (\_PR)
    {
        //
        // CPU-specific PPM block (Processor P000..P003), selected by the
        // build's SOC_VARIANT define (Acer A1-830 = SOC_VARIANT_Z2560, see
        // ducatiPkg.dsc:
        //   PrZ2520  1.2 GHz part on a 100 MHz FSB (7 P-states)
        //   PrZ2560  1.6 GHz part on a ~133 MHz FSB (4 P-states)
        //   PrZ2580  2.0 GHz part - not authored yet (stub, #error)
        //
        // Sources live in Silicon/Intel/CloverviewPkg/Acpi/Include/ so every
        // Cloverview-derived platform selects its own \_PR without forking the
        // device DSDT.
        //
        // Why this is needed: clvpep.sys (ACPI\INT3395, the Z25xx/Z27xx Power
        // Engine Plug-in) drives P-states through the PPM interface. With no
        // _PCT/_PSS/_PPC/_PSD/_CST on the processor objects there is nothing
        // for it to attach to, so nothing ever raises the ratio and the CPU
        // stays at its boot ratio.
        //
#ifdef SOC_VARIANT_Z2520
        #include "PrZ2520.asl"
#elif defined (SOC_VARIANT_Z2560)
        #include "PrZ2560.asl"
#elif defined (SOC_VARIANT_Z2580)
        #include "PrZ2580.asl"
#else
        #error "No SOC_VARIANT_* define for the ASL build (set SOC_VARIANT in the platform DSC)"
#endif
    }

    Scope (\_SB)
    {
        //
        // System resource consumer: claims the MMIO windows the OS must not
        // hand out to drivers, so the PEP can MmMapIoSpace its blocks (NC PM
        // unit at 0xFF11D000, SCU IPC at 0xFF11C000, MMCONFIG at 0x3EF00000)
        // without conflict. Mirrors the SFI MMAP / iomem_A502CG.txt map. The
        // 0xFF000000 window stays split so the PNP0C02 reservation does not
        // collide with OTG0's 0xFFA60000 block or the IPC device's own _CRS.
        // The MMIO of the removed PEP-client devices (GPIO banks, SDHCs,
        // SST) deliberately remains inside these claims - EXCEPT the I2C
        // controller windows 0xFF139000..0xFF1393FF (bus 1),
        // 0xFF13A000..0xFF13A3FF (bus 2) and 0xFF13D000..0xFF13D3FF (bus 5),
        // carved out of the claims for \_SB.I2C1, \_SB.I2C2 and \_SB.I2C5
        // below. SDC0's block at 0xFFA58000 is inside the 0xFF11C400 claim,
        // which is a plain PNP0C02 reservation, so its _CRS does not conflict.
        //
        Device (SYSR)
        {
            Name (_HID, EisaId ("PNP0C02"))
            Name (_UID, 0x01)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00D00000, 0x00300000) // low MMIO
                Memory32Fixed (ReadWrite, 0x36FF0000, 0x00A0D000) // SoC MMIO
                Memory32Fixed (ReadWrite, 0x3EF00000, 0x00100000) // MMCONFIG
                // 0x3F000000+0x01000000 (framebuffer) is now produced by \_SB.PCI0 so
                // the GPU can claim it if Windows assigns a BAR there; the firmware
                // GOP keeps using it until an OS display driver takes over.
                // 0x40000000+0x10000000 (GPU) is claimed by \_SB.PCI0.GFX0 instead
                Memory32Fixed (ReadWrite, 0xDFC00000, 0x00200000) // unclaimed gap 0xDFC00000-0xDFDFFFFF
                Memory32Fixed (ReadWrite, 0xFA000000, 0x04000000)
                Memory32Fixed (ReadWrite, 0xFEC00000, 0x00001000) // IOAPIC 0
                Memory32Fixed (ReadWrite, 0xFEC10000, 0x00001000) // IOAPIC 1
                Memory32Fixed (ReadWrite, 0xFEE00000, 0x00001000) // LAPIC
                Memory32Fixed (ReadWrite, 0xFF000000, 0x0011C000) // SoC MMIO below SCU IPC
                Memory32Fixed (ReadWrite, 0xFF11C400, 0x00943C00) // SoC MMIO above SCU IPC
                Memory32Fixed (ReadWrite, 0xFFA61000, 0x00596FC0) // below IPC mailbox
                Memory32Fixed (ReadWrite, 0xFFFF8000, 0x00008000) // above IPC mailbox
                Memory32Fixed (ReadWrite, 0x3EEFD000, 0x00001000) // firmware console-state page (FrameBufferSerialPortLib)
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // Windows-compatible System Power Management Controller: the Power
        // Engine Plug-in target (clvpep.sys, ACPI\INT3395, _CID PNP0D80).
        // _DSM function 1 (UUID b8febfe0-baf8-454b-aecd-49fb91137b21) sets
        // \PEPP to record that the plug-in attached. The PEP needs no _CRS:
        // it maps the NC PM unit, SCU IPC and IOSF/MMCONFIG windows itself
        // (reserved by SYSR above) and reaches the PUNIT over IOSF sideband
        // or an MBI driver, and the SCU through the IPC mailbox.
        //
        Device (PEP)
        {
            Name (_HID, "INT3395")
            Name (_CID, EisaId ("PNP0D80"))
            Name (_UID, One)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Method (_DSM, 4, Serialized)
            {
                If ((Arg0 == ToUUID ("b8febfe0-baf8-454b-aecd-49fb91137b21")))
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
                        PEPP = One
                        Return (0x0F)
                    }
                }

                Return (One)
            }

            PowerResource (ID3C, 0x00, 0x0000)
            {
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_ON, 0, NotSerialized)
                {
                }

                Method (_OFF, 0, NotSerialized)
                {
                }
            }
        }

        //
        // Cloverview Low Power Audio Engine (Intel SST, NOT HD Audio).
        // RE-ADDED this revision: PEP is now known-good on Windows, so the
        // audio path the stock firmware ships can come back. The engine is the
        // PCI 00:01.3 [8086:08E7] "Multimedia audio controller" the board's
        // Linux boots (iomem.txt / dmesg-7.2.2) map with intel_sst_driver on
        // these BARs:
        //     BAR0 0xFFA22000/0x400   BAR1 0xFFAE8000/0x1000
        //     BAR2 0xFFFF1000/0x2000  BAR3 0xFFB00000/0x80000
        //     BAR4 0xFFB80000/0x80000
        // and its own interrupt line is GSI 30 (0x1E) - interrupts.txt
        // "intel_sst_driver"; interrupts.txt also shows the two I2S serial
        // ports (the codec PCM / SSP lanes) on GSI 65/66 = 0x41/0x42.
        // The codec is a Cirrus Logic CS42L73 on I2C bus 1 @ 0x4A (dmesg-stock
        // "cs42l73 1-004a"), control plane over I2C, so the codec node is a
        // child of \_SB.I2C1 (CODC below) and AUD1._DEP lists it like the
        // reference AUD1._DEP { PEP, I2C5.RTEK }.
        //
        // Binding: _HID "INT33B3" matches isst.inf's IntelSSTAudio entry
        // (the Cirrus lane, ships + loads cirrus_fw_sst.bin); the Realtek
        // isstrtc.inf uses "INTC33B3". The reference DSDT used INTC33B3 (it
        // was a Realtek RTEK board); we use the Cirrus ID so isst.inf wins.
        // The codec _HID "INTCFFD" matches ssti2c.inf ("I2C CODEC Device
        // Driver", the Cirrus codec function driver from the AudioCirrus
        // tree). Codec interrupt: SFI GPIO "gpio_codec_int" = AON/CLV_GPIO_0
        // pin 32 = 0x20 (not the reference's 0x33 which was a W511 board).
        //
        // _CRS mirrors the reference AUD1 layout: two register windows, the
        // engine interrupt, the codec GpioInt, then the mailbox + the two DSP
        // SRAM windows, then the i2s ssp port interrupts - addresses/GSIs are
        // this board's own. Resources overlap the PNP0C02 SYSR reservation
        // below, which "does not conflict" (see SDC0's note) - same story as
        // the removed devices whose MMIO stayed inside the claims.
        //
        Device (AUD1)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B3")
            Name (_CID, "INT33B3")
            Name (_DDN, "Cloverview Low Power Audio Engine")
            Name (_UID, 0x02)
            Name (_DEP, Package (0x03)
            {
                \_SB.PEP,
                \_SB.I2C1.CODC,
                \_SB.GPO0
            })
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFFA22000,         // BAR0: SST shim / registers
                        0x00000400,
                        )
                    Memory32Fixed (ReadWrite,
                        0xFFAE8000,         // BAR1
                        0x00001000,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000001E,         // GSI 30 = intel_sst_driver
                    }
                    GpioInt (Edge, ActiveHigh, ExclusiveAndWake, PullNone, 0xEA60,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {   // gpio_codec_int (AON pin 32)
                            0x00000020
                        }
                    Memory32Fixed (ReadWrite,
                        0xFFFF1000,         // BAR2: mailbox / shared SRAM
                        0x00002000,
                        )
                    Memory32Fixed (ReadWrite,
                        0xFFB00000,         // BAR3: DSP SRAM
                        0x00080000,
                        )
                    Memory32Fixed (ReadWrite,
                        0xFFB80000,         // BAR4: DSP SRAM
                        0x00080000,
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000041,         // i2s ssp 0 (GSI 65)
                    }
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000042,         // i2s ssp 1 (GSI 66)
                    }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // PCI root bridge + the two REAL PCI devices on this SoC.
        //
        // iomem.txt shows exactly two genuinely PCI-addressable functions on
        // the A502CG; everything else (I2C/SPI/GPIO/USB/SDHCI/HSU/DMA) is
        // Linux "pci glue" and is declared here as its own ACPI device, not as
        // a PCI function:
        //   0000:00:02.0 - PowerVR SGX544MP GPU, owers 40000000-4fffffff,
        //                  dfec0000-dfefffff and dff00000-dfffffff (pvrsrvkm)
        //   0000:00:03.0 - Intel ISP 2300 camera, owns df800000-dfbfffff
        // Exposing these under a PNP0A03 root (matching the stock Z2760 PCI0;
        // no PNP0A08, no _OSC) lets Windows' pci.sys enumerate them by PCI
        // VID/DID, matching Graphics/49889.inf
        // (PCI\VEN_8086&DEV_08C7..) and Camera/camera.inf
        // (PCI\VEN_8086&DEV_08D0). The stock Z2760 DSDT and the working DSDT2
        // declare the GPU the same way (_ADR 0x00020000 under PCI0).
        //
        // Config space is reached through the legacy CF8/CFC ports - the same
        // path the firmware PCICFG probe uses successfully on this board.
        //
        // The ISP's _DEP on \_SB.PEP and _PR3 to \_SB.PEP.ID3C mirror the
        // stock Z2760 ISP0; GFX0 additionally matches the existing DSDT2
        // declaration. The _CRS memory producer windows cover both devices'
        // real BAR ranges from iomem.txt (GPU at 0x3F000000-0x4FFFFFFF and
        // 0xDFE00000-0xDFFFFFFF, ISP at 0xDF800000-0xDFBFFFFF).
        //
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_ADR, Zero)
            Name (_BBN, Zero)
            Name (_UID, Zero)
            Name (_DEP, Package (0x01)
            {
                \_SB.PEP
            })
            //
            // Routing for the two real PCI devices. GPU pin 0 -> GSI 0x10
            // (already in this board's GSI-base-0 space, per DSDT2). ISP pin 0
            // is provisional (GSI base 0, GSI == PCI IRQ line); replace with
            // the IRQ line the PCICFG probe prints for 00:03.0 if a Code 12/29
            // appears.
            //
            Name (_PRT, Package (0x02)
            {
                Package (0x04) { 0x0002FFFF, Zero, Zero, 0x10 },
                Package (0x04) { 0x0003FFFF, Zero, Zero, 0x0B }
            })
            //
            // Minimal producer set modelled on the stock Z2760 PCI0._CRS and
            // DSDT2, trimmed to what actually decodes here: bus 0..255, the
            // CF8/CFC aperture, and the GPU + ISP memory windows from
            // iomem.txt. The lower window starts at 0x3F000000 so the
            // framebuffer aperture (40000000-4fffffff) sits inside the
            // producer range and can be handed to the GPU.
            //
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    0x0000, 0x0000, 0x00FF, 0x0000, 0x0100,,, )
                IO (Decode16, 0x0CF8, 0x0CF8, 0x01, 0x08)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0000, 0x006F, 0x0000, 0x0070,,, , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0078, 0x0CF7, 0x0000, 0x0C80,,, , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0D00, 0xFFFF, 0x0000, 0xF300,,, , TypeStatic, DenseTranslation)
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite,
                    0x00000000, 0x000A0000, 0x000BFFFF, 0x00000000, 0x00020000,,,
                    , AddressRangeMemory, TypeStatic)
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0x3F000000, 0x4FFFFFFF, 0x00000000, 0x11000000,,, )
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0xDF800000, 0xDFBFFFFF, 0x00000000, 0x00400000,,, )
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0xDFE00000, 0xDFFFFFFF, 0x00000000, 0x00200000,,, )
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Device (GFX0)
            {
                Name (_ADR, 0x00020000)
                Name (_DDN, "PowerVR SGX544MP Graphics")
                //
                // Order the GPU start behind the PEP (clvpep.sys), mirroring the
                // stock Z2760 GFX0._DEP. On a driver-less install the PEP may be
                // the thing that keeps the bootloader-initialized DSI panel alive
                // through Setup's PNP/graphics phase, so GFX0 must not be probed
                // before it. (PCI0 already waits on \_SB.PEP; this also covers
                // the device itself.)
                //
                Name (_DEP, Package (0x01)
                {
                    \_SB.PEP
                })
                //
                // PCI config-space BAR access, ported from the stock Z2760 GFX0:
                // these OperationRegions read (and _PS0 rewrites) the GPU's own
                // config space. They do NOT claim resources - pci.sys takes the
                // real BAR decode from config space and PCI0._CRS provides the
                // producer windows (0x40000000-0x4FFFFFFF aperture, 0xDFE00000+
                // for the control BARs). The SSID write matches the subsystem ID
                // this board's driver package knows (Graphics/49889.inf entries
                // PCI\VEN_8086&DEV_08C7..08CF&SUBSYS_074B1025).
                //
                OperationRegion (BAR0, PCI_Config, 0x10, 0x04)
                OperationRegion (BAR1, PCI_Config, 0x14, 0x04)
                OperationRegion (BAR2, PCI_Config, 0x18, 0x04)
                OperationRegion (BAR3, PCI_Config, 0x1C, 0x04)
                OperationRegion (DEV2, PCI_Config, Zero, 0xFF)
                Field (DEV2, DWordAcc, NoLock, Preserve)
                {
                    Offset (0x2C),
                    SSID,   32
                }
                Method (_PS0, 0, NotSerialized)
                {
                    SSID = 0x074B1025
                }
                Method (_PS3, 0, NotSerialized)
                {
                }
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }
            }

            Device (ISP0)
            {
                Name (_ADR, 0x00030000)
                Name (_DDN, "Intel Imaging Signal Processor 2300")
                Name (_DEP, Package (0x01)
                {
                    \_SB.PEP
                })
                Method (_PR3, 0, NotSerialized)
                {
                    Return (Package (0x01)
                    {
                        \_SB.PEP.ID3C
                    })
                }
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }
            }
        }

        //
        // Cloverview Inter-Processor (x86 <-> SCU) Communication controller,
        // the one PEP-required companion device enumerated over ACPI: IPC-1
        // command block at 0xFF11C000 (iomem: intel_scu_ipc) plus the
        // read-only IPC mailbox at 0xFFFF7FC0. The stock LIPC/IPCC helpers
        // are omitted: they call a global IPCC method this table does not
        // declare.
        //
        // Interrupt is provisional: 0x17 is the stock value; on this board
        // GSI == PCI IRQ line, so replace it with the line the PCICFG probe
        // reports for the function owning 0xFF11C000 if it differs.
        //
        Device (IPC)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B5")
            Name (_CID, "INT33B5")
            Name (_DDN, "Cloverview Inter-Processor (x86/SCU) Communication controller")
            Name (_UID, One)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFF11C000, 0x00000400)
                Memory32Fixed (ReadOnly, 0xFFFF7FC0, 0x00000040)
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive, ,, )
                {
                    0x00000017,
                }
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Device (MREG)
            {
                Name (_ADR, Zero)
                Name (_HID, "INTCFFA")
                Name (_CID, "INTCFFA")
                Name (_DDN, "MSIC Register Access")
                Name (_UID, One)
                Name (_CRS, ResourceTemplate ()
                {
                })
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }
            }
        }

        //
        // Power button. On the A502CG the power key is serviced by the
        // MSIC/PMIC (SFI DEVS lists msic_power_btn; the SFI GPIO table has
        // no power-key pin), so it is a plain PNP0C0C device that Windows'
        // ACPI button driver claims. The GPIO volume-key array (TBAD) is
        // declared separately below (volume-up/down are AON GPIO 31/30).
        //
        Device (PWRB)
        {
            Name (_HID, EisaId ("PNP0C0C"))
            Name (_UID, One)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // GPIO controllers (Langwell/Cloverview banks), ported from the
        // working DSDT2 (known-good GPIO + WLAN state for this board).
        //   GPO0 = AON  bank, 0xFF119000, _UID 1, _HRV 0 (REV_0000 = A0/B0 AON)
        //   GPO1 = CORE bank, 0xFF13F000, _UID 2, _HRV 1 (REV_0001 = A0/B0 CORE)
        // _DEP on \_SB.PEP (not IPC): PEP gates the GPIO power island and
        // must attach before the controller can start. Both interrupts are
        // ActiveLow/Shared as the stock CLV kernel programs every pin.
        //
        // GPO0 interrupt: GSI 0x15 (IOAPIC id2 input 21), confirmed by the
        // board's U-Boot southcluster.asl ("parent interrupt is GSI 21").
        //
        // GPO1 interrupt: GSI 0x38 (IOAPIC id2 input 56), the real PCI IRQ
        // line read from PCI config space of 00:03.5 on this board. The
        // Z2760's 0x74 does NOT apply here (different board).
        //
        // GPO1 carries a GeneralPurposeIo operation region (GPOP) with a
        // single-bit field WLEN bound to CORE bank local pin 0x4A (global
        // 170, the WLAN_EN pad). Once GpioClv.sys registers the region
        // handler, _REG sets AVBL=1 and SDC1._PS0 can drive WLAN_EN high
        // through this field. This is the same mechanism the stock Clover
        // Trail DSDT uses.
        //
        Device (GPO0)
        {
            Name (_HID, "INT33B2")
            Name (_CID, "INT33B2")
            Name (_DDN, "Cloverview AON General Purpose Input/Output (GPIO) controller")
            Name (_UID, One)
            //
            // _HRV sets the GpioClv driver variant (REV_0000 = A0/B0 AON; this
            // board is B0 STEP=1, matching the stock Z2760 GPO0 _HRV method).
            //
            Method (_HRV, 0, NotSerialized)
            {
                If (((STEP == One) || (STEP == Zero)))
                {
                    Return (Zero)
                }
                ElseIf ((STEP == 0x02))
                {
                    Return (0x02)
                }
                Else
                {
                    Return (0x04)
                }
            }
            Name (_DEP, Package (0x01) { \_SB.PEP })
            Method (_CRS, 0, NotSerialized)
            {
                Name (CBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF119000, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveLow, Shared, ,, ) { 0x00000015 }
                })
                Return (CBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            //
            // Touchscreen reset (ts_rst): AON pin 58, active-low. Same pattern
            // as GPO1's WLEN (GeneralPurposeIo operation region). While held
            // low the FocalTech FT5x06 can hold the I2C2 bus in a bad state,
            // so it must be pulsed high before any I2C traffic. TOUC._PS0 does
            // the timed pulse; this exposes the bit so ASL can drive it.
            //
            Name (AVBL, Zero)
            Method (_REG, 2, NotSerialized)
            {
                If ((Arg0 == 0x08))
                {
                    AVBL = Arg1
                }
            }

            Name (TRST, ResourceTemplate ()
            {
                GpioIo (Exclusive, PullDefault, 0x0000, 0x0000, IoRestrictionOutputOnly,
                    "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                    )
                    {
                        0x003A      // ts_rst (pin 58)
                    }
            })
            OperationRegion (GPOP, GeneralPurposeIo, Zero, 0x0C)
            Field (\_SB.GPO0.GPOP, ByteAcc, NoLock, Preserve)
            {
                Connection (TRST),
                TSTP, 1
            }
        }

        //
        // 5-button array (INTCFD9 / PNP0C40 "Standard Button Controller").
        // Ported from the working DSDT2. Volume-up = AON pin 30 (0x1E),
        // volume-down = AON pin 31 (0x1F). Indices 0/1/4 are wired to
        // in-range but unused AON pins (1/4/5) that never fire; index 1
        // (Home) uses ActiveLow/PullUp so it idles as released (avoids the
        // phantom Win+VolUp chord). _DEP on GPO0 for GpioInt resolution.
        //
        Device (TBAD)
        {
            Name (_HID, "INTCFD9")
            Name (_CID, "PNP0C40")
            Name (_DDN, "Keyboard less system - 5 Button Array Device")
            Name (_UID, 0x10)
            Name (_DEP, Package (0x01) { \_SB.GPO0 })
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    // 0: Power (not wired on this board - MSIC/PMIC key)
                    GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x0001
                        }
                    // 1: Windows / Home (not present on this board)
                    // ActiveLow/PullUp so pad idles high (released); avoids
                    // phantom Win-key chord (Win+VolUp = Narrator).
                    GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x0004
                        }
                    // 2: Volume Up - clv_gpio_0 pin 30
                    GpioInt (Edge, ActiveBoth, ExclusiveAndWake, PullNone, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x001E
                        }
                    // 3: Volume Down - clv_gpio_0 pin 31
                    GpioInt (Edge, ActiveBoth, ExclusiveAndWake, PullNone, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x001F
                        }
                    // 4: Rotation lock (not present on this board)
                    GpioInt (Edge, ActiveLow, Exclusive, PullUp, 0x1770,
                        "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                        )
                        {
                            0x0005
                        }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // GPO1: Corewell GPIO bank (PCI 00:03.5, 0xFF13F000). Ported from
        // the working DSDT2. Carries the WLEN field for WLAN_EN power control
        // through its GeneralPurposeIo operation region (GPOP).
        //
        Device (GPO1)
        {
            Name (_HID, "INT33B2")
            Name (_CID, "INT33B2")
            Name (_DDN, "Cloverview Corewell Powered General Purpose Input/Output (GPIO) controller")
            Name (_UID, 0x02)
            //
            // _HRV selects the GpioClv driver variant. The INF has distinct
            // entries for AON vs CORE banks: REV_0000 = A0/B0 AON, REV_0001 =
            // A0/B0 CORE (this board is B0, STEP=1). Zero would bind BOTH banks
            // to the AON variant, so GpioClv would resolve the WLEN write to pin
            // 0x4A (CORE offset 74 = global 170, the WLAN-enable pad) against the
            // wrong bank's layout and drive the wrong physical pad - WLAN_EN
            // never asserted, radio stays off. Matches the stock Z2760 GPO1 _HRV.
            // Use a method so it follows STEP like the reference does.
            //
            Method (_HRV, 0, NotSerialized)
            {
                If (((STEP == One) || (STEP == Zero)))
                {
                    Return (One)
                }
                ElseIf ((STEP == 0x02))
                {
                    Return (0x03)
                }
                Else
                {
                    Return (0x04)
                }
            }
            Name (_DEP, Package (0x01) { \_SB.PEP })
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF13F000, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveLow, Shared, ,, ) { 0x00000038 }
                })
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (AVBL, Zero)
            Method (_REG, 2, NotSerialized)
            {
                If ((Arg0 == 0x08))
                {
                    AVBL = Arg1
                }
            }

            Name (GMOD, ResourceTemplate ()
            {
                GpioIo (Exclusive, PullDefault, 0x0000, 0x0000, IoRestrictionOutputOnly,
                    "\\_SB.GPO1", 0x00, ResourceConsumer, ,
                    )
                    {
                        0x004A
                    }
            })
            OperationRegion (GPOP, GeneralPurposeIo, Zero, 0x0C)
            Field (\_SB.GPO1.GPOP, ByteAcc, NoLock, Preserve)
            {
                Connection (GMOD),
                WLEN,   1,
            }
        }

        //
        // DMA controllers. GDMS = Intel MID DMA2 (intel_mid_dmac @ 0xFF13E000,
        // PCI 00:02.5, GSI 0x31 - interrupts.txt "INTEL_MID_DMAC2"); UDMS =
        // HSU DMA @ 0xFFA28400, GSI 0x3B (interrupts.txt "hsu dma"). Windows
        // 8.1 binds these with in-box drivers: ACPI\INTL0005 -> HalExtIntc-
        // LpioDMA.dll (halextintclpiodma.inf, "Intel(R) Serial IO DMA Control-
        // ler") and ACPI\INTL0004 -> HalExtIntcUartDMA.dll (halextintcuartdma.
        // inf, "Intel(R) UART DMA" - both DLLs present in installed System32).
        // Declaring them provides the FixedDMA request lines used by SPI1/SPI2,
        // UART0-2 (UDMS) and I2C0/1/2/4/5 (GDMS) with a real provider, matching
        // the ship BIOS exactly. STEP=One -> GDMS returns RBUF
        // (GSI 0x71 variant is only for STEP == 0x02). Resource templates
        // verbatim from the Z2760 reference DSDT (2760-acpi/DSDT.dsl:4503-4550).
        //
        Device (GDMS)
        {
            Name (_HID, "INTL0005")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (CBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF13E000, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive, ,, )
                    {
                        0x00000071,
                    }
                })
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF13E000, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000031,
                    }
                })
                If ((STEP == 0x02))
                {
                    Return (CBUF) /* \_SB_.GDMS._CRS.CBUF */
                }
                Else
                {
                    Return (RBUF) /* \_SB_.GDMS._CRS.RBUF */
                }
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (UDMS)
        {
            Name (_HID, "INTL0004")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFFA28400, 0x00000800)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000003B,
                    }
                })
                Return (RBUF) /* \_SB_.UDMS._CRS.RBUF */
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // High-Speed UART controllers (Intel HSU) - PCI 00:05.0 through
        // 00:05.2 [8086:08FC/08FD/08FE], HSU DMA on 00:05.3 (UDMS above).
        // Stock SFI on this board (dmesg-stock.txt): hsu_bt_port_p @
        // 0xFFA28080/irq 60, hsu_gps_port_p + hsu_modem_port_p @
        // 0xFFA28100/irq 61, hsu_debug_port_p @ 0xFFA28180/irq 62, all
        // matching the Z2760 reference DSDT (2760-acpi/DSDT.dsl:280-459).
        // Windows binds ACPI\INT33BC with the in-box HSU UART stack
        // (Uart16550pc.sys, SerCx-based). FixedDMA was tried once (channels
        // 0-5 on UDMS, serviced by the "Intel(R) UART DMA" HalExtIntcUartDMA
        // ext) but every UART failed to start with Code 10 - however that run
        // predates the CSRT table, so HalExtIntcUartDMA had no request-line
        // map to drive the channels and broke exactly like SPI did pre-CSRT.
        // With the CSRT UDMS group now installed (base req line 0x0, 10 ch)
        // the reference Z2760 descriptors are reinstated verbatim: URT0
        // (0x0000,0x0000)+(0x0001,0x0001), URT1 (0x0002,0x0002)+(0x0003,0x0003),
        // URT2 (0x0004,0x0004)+(0x0005,0x0005), all Width8bit (2760-acpi/
        // DSDT.dsl:280-459), matching the 8250_mid.c dma_index 0/1/2 layout.
        // No _DEP on the DMA controllers - SPI regression lesson (dependent
        // devices stopped enumerating).
        //
        Device (URT0)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33BC")
            Name (_CID, "INT33BC")
            Name (_DDN, "Cloverview UART Controller")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFFA28080, 0x00000040)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000003C,
                    }
                    FixedDMA (0x0000, 0x0000, Width8bit, )
                    FixedDMA (0x0001, 0x0001, Width8bit, )
                })
                Return (RBUF) /* \_SB_.URT0._CRS.RBUF */
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            //
            // Bluetooth (Broadcom BCM4330 combo) - HCI runs over HSU port 0
            // at 115200 with hardware flow control, exactly as U-Boot models
            // it (\_SB.PCI0.HSU0.BT0, southcluster.asl:480-523). _HID BCM2E01
            // matches U-Boot (the Z2760 reference used BCM2E05 for its own
            // module). GPIOs, same as U-Boot: [0] device-wakeup = Langwell
            // AON GPIO 45 (dmesg-stock "bt_uart_enable ... pin 45"), i.e.
            // \_SB.GPO0 pin 45; [1] shutdown = cloverview_core GPIO 13
            // (global 109), i.e. \_SB.GPO1 pin 13. Host-wakeup is not wired
            // on this board (U-Boot comment), so no GpioInt.
            //
            Device (BT0)
            {
                Name (_HID, "BCM2E01")
                Method (_CRS, 0, NotSerialized)
                {
                    Name (PBUF, ResourceTemplate ()
                    {
                        UartSerialBusV2 (0x0001C200, DataBitsEight, StopBitsOne,
                            0xFC, LittleEndian, ParityTypeNone, FlowControlHardware,
                            0x0020, 0x0020, "\\_SB.URT0",
                            0x00, ResourceConsumer, , Exclusive,
                            )
                        GpioIo (Exclusive, PullDefault, 0x0000, 0x0000, IoRestrictionOutputOnly,
                            "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                            )
                            {   // Pin list
                                0x002D      // device-wakeup (AON GPIO 45)
                            }
                        GpioIo (Exclusive, PullDefault, 0x0000, 0x0000, IoRestrictionOutputOnly,
                            "\\_SB.GPO1", 0x00, ResourceConsumer, ,
                            )
                            {   // Pin list
                                0x000D      // shutdown (Core GPIO 13 / global 109)
                            }
                    })
                    Return (PBUF) /* \_SB_.URT0.BT0._CRS.PBUF */
                }
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }
            }
        }

        Device (URT1)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33BC")
            Name (_CID, "INT33BC")
            Name (_DDN, "Cloverview UART Controller")
            Name (_UID, 0x02)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFFA28100, 0x00000040)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000003D,
                    }
                    FixedDMA (0x0002, 0x0002, Width8bit, )
                    FixedDMA (0x0003, 0x0003, Width8bit, )
                })
                Return (RBUF) /* \_SB_.URT1._CRS.RBUF */
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (URT2)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33BC")
            Name (_CID, "INT33BC")
            Name (_DDN, "Cloverview UART Controller")
            Name (_UID, 0x03)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFFA28180, 0x00000040)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000003E,
                    }
                    FixedDMA (0x0004, 0x0004, Width8bit, )
                    FixedDMA (0x0005, 0x0005, Width8bit, )
                })
                Return (RBUF) /* \_SB_.URT2._CRS.RBUF */
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // SPI controllers (Intel MID SPI, synopsys-designware "dw_spi"). Board
        // resources confirmed from Linux (iomem.txt / interrupts.txt): SPI1 =
        // dw_spi0 @ 0xFF135000, GSI 9; SPI2 = dw_spi1 @ 0xFF136000, GSI 0x26.
        // The MMIO/IRQ windows here are byte-identical to the Z2760 reference
        // DSDT (2760-acpi/DSDT.dsl:184-278), which is exactly how the shipped
        // Windows 8.1 firmware described them.
        //
        // The _CRS methods reproduce the reference's STEP switch: STEP == 0
        // (A0 silicon) gets RBUF without DMA, everything else gets SBUF with
        // FixedDMA descriptors. On this B0 board (STEP = One) the SBUF arm is
        // always taken, so both controllers carry their real hardware DMA
        // request lines. Those request lines (0x0010..0x0013) are serviced by
        // the GDMS engine (ACPI\INTL0005 -> HalExtIntcLpioDMA "Intel(R) Serial
        // IO DMA Controller"), whose 8 channels (CHA0..CHA7, request lines
        // 0x10-0x1F) are published by the board-corrected CSRT table
        // (GDMS GSI 0x31). This is the DMA sanity test: with the CSRT in
        // place the LpioDMA extension should claim GDMS and hand real DMA to
        // these two SPI clients.
        //
        Device (SPI1)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B0")
            Name (_CID, "INT33B0")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF135000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000009,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF135000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000009,
                    }
                    FixedDMA (0x0011, 0x0006, Width32bit, )
                    FixedDMA (0x0010, 0x0007, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF) /* \_SB_.SPI1._CRS.RBUF */
                }
                Else
                {
                    Return (SBUF) /* \_SB_.SPI1._CRS.SBUF */
                }
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (SPI2)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B0")
            Name (_CID, "INT33B0")
            Name (_UID, 0x02)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF136000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000026,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF136000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000026,
                    }
                    FixedDMA (0x0013, 0x0004, Width32bit, )
                    FixedDMA (0x0012, 0x0005, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF) /* \_SB_.SPI2._CRS.RBUF */
                }
                Else
                {
                    Return (SBUF) /* \_SB_.SPI2._CRS.SBUF */
                }
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // SD card host controller (mmc1 at 0xFFA58000 per iomem_A502CG.txt,
        // PCI 00:04.0), RESTORED in this revision - but WITHOUT the stock
        // _DEP on \_SB.PEP. The bootloader already powers the controller on
        // and leaves it running, so it does not need the PEP for D0 entry,
        // and decoupling it means it enumerates and starts even if
        // clvpep.sys is absent or fails to attach. Everything else is the
        // previous working configuration, byte-identical: _HID INT33BB /
        // _CID PNP0D40 so Windows binds its own sdbus/sdhc stack, GSI 0x29
        // (the real IRQ line of 00:04.0 on this board), and the SDMD child
        // (_ADR 8, _RMV = 0) that keeps Windows treating the SD volume as
        // non-removable. Without a PEP relationship the controller simply
        // stays in D0 - no runtime D-state transitions - which is the
        // intended always-powered behaviour for now.
        //
        Device (SDC0)
        {
            Name (_ADR, 0x00000000)
            Name (_HID, "INT33BB")
            Name (_CID, EisaId ("PNP0D40"))
            Name (_UID, 0x02)
            Name (_HRV, 0x01)
            Name (RBUF, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFFA58000, 0x00000100)
                // GSI 41 (0x29) - real IRQ line read from PCI config space of
                // 00:04.0 (SD host controller) on this board. The stock Z2760
                // table's value is in its 0x60-based IOAPIC space and does not
                // apply here (single IOAPIC id 4 @ 0xFEC00000, GSI base 0).
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive, ,, ) { 0x00000029 }
            })
            Method (_CRS, 0, NotSerialized)
            {
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
            Method (_DIS, 0, NotSerialized)
            {
            }
            // No card detect GPIO is declared (no GPIO driver on this platform);
            // the slot is treated as always populated, matching the firmware
            // SdHostDxe behaviour (SDHCI_QUIRK2_BAD_SD_CD downstream).
            //
            // Non-removable child, same shape as EMM0's EMMD: _ADR 0x08 with
            // _RMV returning Zero. Without this Windows classifies the SD volume
            // as removable media and the offline specialize pass refuses to
            // configure Windows on it ("Windows Setup could not configure Windows
            // to run on this computer's hardware"), even though files copy fine
            // and the volume is readable from Shift+F10.
            //
            Device (SDMD)
            {
                Name (_ADR, 0x08)
                Method (_RMV, 0, NotSerialized)
                {
                    Return (Zero)
                }
            }
        }

        //
        // Wi-Fi SDIO host controller (mmc2 at 0xFFA48000, PCI 00:04.1
        // [8086:08FA]), the Broadcom BCM4330 on this board. Ported from the
        // working DSDT2 (known-good WLAN state for this board).
        //
        // Key differences from my earlier version (which caused WLAN bluescreen):
        //  - _UID 0x03 (unique, not colliding with SDC0's 0x02)
        //  - _DEP on PEP + GPO1 (must wait for GPO1 region handler for WLEN)
        //  - _PS0 asserts GPO1.WLEN to power up the WLAN chip
        //  - BRCM child carries the WLAN OOB host-interrupt GpioInt on
        //    \\_SB.GPO0 pin 0x02 (WLAN-interrupt, the dongle's only event
        //    path), plus _PRW/_S0W/_S4W and WLEN power methods. Matching the
        //    Z2760 reference; without the GpioInt, bcmdhd63.sys has no SDIO
        //    event/interrupt line and scans silently (WiFi "on", no networks).
        //
        Device (SDC1)
        {
            Name (_ADR, 0x00000000)
            Name (_HID, "INT33BB")
            Name (_CID, EisaId ("PNP0D40"))
            Name (_UID, 0x03)
            Name (_HRV, 0x02)
            Name (_DEP, Package (0x02) { \_SB.PEP, \_SB.GPO1 })
            Name (PSTS, Zero)
            Name (RBUF, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFFA48000, 0x00000100)
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive, ,, ) { 0x0000002A }
            })
            Method (_CRS, 0, NotSerialized)
            {
                Return (RBUF)
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
            Method (_PS0, 0, NotSerialized)
            {
                If ((PSTS == Zero))
                {
                    If ((\_SB.GPO1.AVBL == One))
                    {
                        \_SB.GPO1.WLEN = One
                        PSTS = One
                        Sleep (0x32)
                    }
                }
            }
            Method (_PS3, 0, NotSerialized)
            {
            }
            Method (_DIS, 0, NotSerialized)
            {
            }
            Device (BRCM)
            {
                Name (_ADR, One)
                Name (_DEP, Package (0x01) { \_SB.GPO1 })
                Method (_RMV, 0, NotSerialized)
                {
                    Return (Zero)
                }
                Name (_PRW, Package (0x02) { Zero, Zero })
                Name (_S4W, 0x02)
                Name (_S0W, 0x02)
                Method (_CRS, 0, NotSerialized)
                {
                    Name (RBUF, ResourceTemplate ()
                    {
                        GpioInt (Edge, ActiveHigh, ExclusiveAndWake, PullNone, 0x0000,
                            "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                            )
                            {
                                0x0002
                            }
                    })
                    Return (RBUF)
                }
                Method (_PS3, 0, NotSerialized)
                {
                    If ((\_SB.GPO1.AVBL == One))
                    {
                        \_SB.GPO1.WLEN = Zero
                        \_SB.SDC1.PSTS = Zero
                    }
                }
                Method (_PS0, 0, NotSerialized)
                {
                    If ((\_SB.SDC1.PSTS == Zero))
                    {
                        If ((\_SB.GPO1.AVBL == One))
                        {
                            \_SB.GPO1.WLEN = One
                            \_SB.SDC1.PSTS = One
                        }
                    }
                }
            }
        }

        //
        // Designware I2C controller for bus 0 (first controller at
        // 0xFF138000; interrupts.txt "i2c-designware-0" irq 10 / GSI 0x0A).
        // Controller only - no subdevices: on stock Android this bus is
        // empty on the A1-830 (the Z2760 reference hosts its Atmel touch
        // @ 0x5B here, this board moved it to I2C2). Modeled on the stock
        // W511 (Z2760) \_SB.I2C0: _UID = bus + 1 = 1, _HRV = STEP, _DEP on
        // PEP + IPC. The _CRS reproduces the reference STEP switch: STEP == 0
        // (A0 silicon) gets RBUF (PIO), everything else gets SBUF with the
        // FixedDMA descriptors serviced by the GDMS engine (INTL0005 ->
        // HalExtIntcLpioDMA) via the CSRT table. On this B0 board (STEP =
        // One) the SBUF arm is taken, as with SPI1/SPI2 and URT0-2.
        //
        Device (I2C0)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, One)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP)
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF138000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000000A,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF138000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000000A,
                    }
                    FixedDMA (0x0017, 0x0000, Width32bit, )
                    FixedDMA (0x0016, 0x0001, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
            }
        }

        //
        // Designware I2C controller for bus 1 (second of the six controllers
        // at 0xFF138000 + bus * 0x1000; iomem_A502CG: i2c-designware at
        // 0xFF139000). First and only I2C bus brought up in this revision.
        // Bus 2 (smb345 charger @ 0x6A) was tried first and gave SpbTestTool
        // error 995 (ERROR_OPERATION_ABORTED: the transfer never completed
        // and was cancelled). Bus 1 is the stronger candidate because a
        // slave on it is KNOWN to answer on this exact board: the EFI-time
        // I2C test read the RT5647 codec's vendor/device ID at 0x1B here.
        //
        // Modeled on the stock W511 (Z2760) \_SB.I2C1 and identical in the
        // essentials to the controller in the earlier EFI-era A502CG port:
        // same path/name (the PEP identifies clients by ACPI path),
        // _UID = bus + 1 = 2, GSI 0x39. NOTE: the stock CLV I2C GSIs are
        // NOT a regular 0x0A + bus pattern (I2C0 = 0x0A, I2C1 = 0x39,
        // I2C2 = 0x0C); 0x39 matches BOTH the stock W511 table and the
        // "real PCI IRQ line of the function owning 0xFF139000" comment in
        // the earlier working port. Level ActiveHigh. _HRV returns STEP
        // (= One -> REV_0001, B0). The _CRS STEP switch mirrors the Z2760
        // SBUF branch (FixedDMA 0x0019,0x0006 / 0x0018,0x0007 on GDMS,
        // serviced by HalExtIntcLpioDMA via the CSRT table); STEP == 0 (A0)
        // falls back to RBUF (PIO). Earlier Code 10 was observed without the
        // CSRT (no request-line map) - retested now that GDMS channels are
        // published, like SPI1/SPI2 and URT0-2.
        //
        // _DEP on PEP + IPC matches stock W511 now that both load normally;
        // the PEP is what un-gates the I2C power island for D0 entry. If the
        // controller sticks in Code 10/D3, the island never powered - that
        // failure implicates the PEP<->IPC path, not this declaration.
        //
        Device (I2C1)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x02)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP)
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF139000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000039,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF139000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000039,
                    }
                    FixedDMA (0x0019, 0x0006, Width32bit, )
                    FixedDMA (0x0018, 0x0007, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            //
            // Cirrus Logic CS42L73 audio codec @ 0x4A (stock SFI DEVS "cs42l73
            // @ 0x4A" on bus 1). REVERSED from the SpbTestTool placeholder: it
            // is the REAL codec now, _HID "INTCFFD" so ssti2c.inf's "I2C CODEC
            // Device Driver" binds (the stock SFI DEVS table had "cs42l73" but
            // no ACPI node for the real codec; the reference 2760 DSDT's codec
            // RTEK showed the pattern: GpioInt + GpioIo on the codec-int GPIO,
            // then the I2cSerialBusV2 connection). 100 kHz control bus.
            //
            // The codec interrupt is board "gpio_codec_int" = AON/CLV_GPIO_0
            // pin 32 = 0x20 (SFI GPIO, dmesg-stock "pin = 32"), NOT the
            // reference board's 0x33. Both AUD1 (above) and CODC declare it,
            // mirroring the reference where AUD1 and RTEK shared the pin.
            // _DEP on GPO0 so the GPIO controller is up before _CRS resolves.
            //
            Device (CODC)
            {
                Name (_HID, "INTCFFD")
                Name (_CID, "INTCFFD")
                Name (_UID, 0x02)
                Name (_DEP, Package (0x01)
                {
                    \_SB.GPO0
                })
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_CRS, 0, NotSerialized)
                {
                    Name (RBUF, ResourceTemplate ()
                    {
                        GpioInt (Edge, ActiveBoth, SharedAndWake, PullNone, 0x0000,
                            "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                            )
                            {   // gpio_codec_int (AON pin 32)
                                0x00000020
                            }
                        GpioIo (Shared, PullDefault, 0x0000, 0x0000, IoRestrictionInputOnly,
                            "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                            )
                            {   // gpio_codec_int (AON pin 32)
                                0x00000020
                            }
                        I2cSerialBusV2 (0x004A, ControllerInitiated, 0x000186A0,
                            AddressingMode7Bit, "\\_SB.I2C1",
                            0x00, ResourceConsumer, , Exclusive,
                            )
                    })
                    Return (RBUF)
                }
            }
        }

        //
        // Designware I2C controller for bus 2 (third controller at
        // 0xFF138000 + bus * 0x1000; iomem: i2c-designware at 0xFF13A000).
        // Modeled on the stock W511 (Z2760) \_SB.I2C2: _UID = 3, GSI 0x0C,
        // Level ActiveHigh, _HRV = STEP. _DEP on PEP + IPC (the POWER island
        // for this controller is PEP-gated like the others). The _CRS STEP
        // switch mirrors the Z2760 SBUF branch (FixedDMA 0x001B,0x0006 /
        // 0x001A,0x0007 on GDMS), taken on this B0 board (STEP = One); PIO
        // (RBUF) remains the fallback for A0 silicon.
        //
        // Bus 2 is the bus the stock Android board uses for the battery and
        // charger and the FocalTech touchscreen (stock SFI DEVS + dmesg:
        // "max17047 @ 0x36", "smb347 @ 0x6A", "ft5x0x_ts @ 0x38"; ts_int
        // GPIO pin 62). Three children are declared:
        //   - CHGR (smb347 charger @ 0x6A) as an SpbTestTool placeholder,
        //     since no inbox function driver targets this part on x86.
        //   - FGAS (max17047 fuel gauge @ 0x36) as an SpbTestTool
        //     placeholder for the same reason.
        //   - TOUC (FocalTech FT5x06 touchscreen @ 0x38) bound to the
        //     FocalTechTouch (FT8607) Windows driver, which matches
        //     ACPI\MSHW1003.
        //
        Device (I2C2)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x03)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13A000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000000C,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13A000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000000C,
                    }
                    FixedDMA (0x001B, 0x0006, Width32bit, )
                    FixedDMA (0x001A, 0x0007, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
            }

            //
            // smb347 charger @ 0x6A (stock SFI DEVS "smb347 @ 0x6A" on bus
            // 2; dmesg-stock: "I2C bus = 2, name = smb347, addr = 0x6A").
            // Declared as an SpbTestTool target (_HID SPBT0001) as a
            // placeholder - no inbox Windows function driver for this cell
            // charger exists, so this only proves the bus path until a real
            // battery/charger stack is wired up.
            //
            Device (CHGR)
            {
                Name (_HID, "SPBT0001")
                Name (_UID, One)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_CRS, 0, NotSerialized)
                {
                    Name (RBUF, ResourceTemplate ()
                    {
                        I2cSerialBusV2 (0x006A, ControllerInitiated, 0x000186A0,
                            AddressingMode7Bit, "\\_SB.I2C2",
                            0x00, ResourceConsumer, , Exclusive,
                            )
                    })
                    Return (RBUF)
                }
            }

            //
            // max17047 fuel gauge @ 0x36 (stock SFI DEVS "max17047 @ 0x36"
            // on bus 2; dmesg-stock: "I2C bus = 2, name = max17047,
            // addr = 0x36"; the stock max_fg_alert GPIO is pin 94).
            // Declared as an SpbTestTool target (_HID SPBT0001) placeholder
            // for the same reason as CHGR.
            //
            Device (FGAS)
            {
                Name (_HID, "SPBT0001")
                Name (_UID, 0x02)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_CRS, 0, NotSerialized)
                {
                    Name (RBUF, ResourceTemplate ()
                    {
                        I2cSerialBusV2 (0x0036, ControllerInitiated, 0x000186A0,
                            AddressingMode7Bit, "\\_SB.I2C2",
                            0x00, ResourceConsumer, , Exclusive,
                            )
                    })
                    Return (RBUF)
                }
            }

            //
            // FocalTech FT5x06 touchscreen @ 0x38 (stock SFI DEVS
            // "ft5x0x_ts @ 0x38" on bus 2; dmesg-stock: "I2C bus = 2, name
            // = ft5x0x_ts, addr = 0x38"; FTS firmware v0x17; ts_int GPIO
            // pin 62). _HID MSHW1003 binds the FocalTechTouch (FT8607)
            // Windows digitizer driver (matches ACPI\MSHW1003). _CID
            // PNP0C50 (HID-over-I2C) plus a GpioInt on \\_SB.GPO0 pin 62
            // (ts_int) lets the driver receive touch interrupts/wake. The
            // GpioInt is Edge ActiveLow with PullUp, matching the board's
            // U-Boot DSDT (southcluster.asl TCH0, which Linux used to bring
            // the FT6236 up) rather than the Z2760 reference tablet's Atmel
            // Level/PullNone. The FocalTech asserts INT only after a scan,
            // so the GPIO pad must catch the falling pulse; a level-triggered
            // pad would only re-fire while the line is held low and miss it.
            //
            // _DEP on GPO0 so the TSTP operation-region handler is attached
            // before _PS0. _PS0 performs the ts_rst timed pulse (pin 58, AON,
            // active-low: assert 0, wait 10ms, release 1), mirroring what
            // U-Boot's ducati_gpio_set(board/acer/ducati/ducati.c) did before
            // the OS on the Linux side. While held low the FT5x06 can hold the
            // I2C2 bus in a bad state, so it must be released before the
            // touch driver issues any I2C traffic. The pulse is done once at
            // first D0 entry (guarded by PSTS), matching the firmware
            // one-shot behavior.
            //
            Device (TOUC)
            {
                Name (_ADR, Zero)
                Name (_HID, "MSHW1003")
                Name (_CID, "PNP0C50")
                Name (_UID, One)
                Name (_DEP, Package (0x01) { \_SB.GPO0 })
                Name (PSTS, Zero)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                //
                // HID-over-I2C _DSM: required by hidi2c.sys to discover the
                // HID descriptor register address and supported functions.
                // UUID 3cdff6f7 = HID I2C Device. Function 0 = query,
                // returns 0x03 (supports fn 0+1). Function 1 = HID
                // descriptor address (returns 0 = default discovery).
                // Matches the Z2760 reference DSDT TOUC._DSM.
                //
                Method (_DSM, 4, NotSerialized)
                {
                    If ((Arg0 == ToUUID ("3cdff6f7-4267-4555-ad05-b30a3d8938de") /* HID I2C Device */))
                    {
                        If ((Arg2 == Zero))
                        {
                            If ((Arg1 == One))
                            {
                                Return (Buffer (One) { 0x03 })
                            }
                            Else
                            {
                                Return (Buffer (One) { 0x00 })
                            }
                        }
                        ElseIf ((Arg2 == One))
                        {
                            Return (Zero)
                        }
                    }
                    Return (Buffer (One) { 0x00 })
                }

                Method (_PS0, 0, NotSerialized)
                {
                    If ((PSTS == Zero))
                    {
                        If ((\_SB.GPO0.AVBL == One))
                        {
                            \_SB.GPO0.TSTP = Zero
                            Sleep (0x0A)
                            \_SB.GPO0.TSTP = One
                            PSTS = One
                        }
                    }
                }

                Method (_PS3, 0, NotSerialized)
                {
                    If ((\_SB.GPO0.AVBL == One))
                    {
                        \_SB.GPO0.TSTP = Zero
                    }
                }

                Method (_CRS, 0, NotSerialized)
                {
                    Name (RBUF, ResourceTemplate ()
                    {
                        I2cSerialBusV2 (0x0038, ControllerInitiated, 0x000186A0,
                            AddressingMode7Bit, "\\_SB.I2C2",
                            0x00, ResourceConsumer, , Exclusive,
                            )
                        GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0x0000,
                            "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                            )
                            {   // Pin list
                                0x003E      // ts_int (pin 62)
                            }
                    })
                    Return (RBUF)
                }
            }
        }

        //
        // Designware I2C controller for bus 3 (fourth controller at
        // 0xFF13B000; interrupts.txt "i2c-designware-3" irq 44 / GSI 0x2C).
        // Controller only - no subdevices (bus 3 has no slaves on stock
        // Android). Modeled on the stock W511 (Z2760) \_SB.I2C3: _UID = 4,
        // _HRV = STEP (the Z2760 table returns HSTP, which this DSDT does
        // not define). NOTE: I2C3 is the one Z2760 controller with no
        // FixedDMA even in the C0 (STEP == 2) branch; here every I2C
        // controller is PIO, so it is indistinguishable from the others.
        // _DEP on PEP + IPC (power island gated like the others).
        //
        Device (I2C3)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x04)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP)
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13B000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002C,
                    }
                })
                Return (RBUF)
            }
        }

        //
        // Designware I2C controller for bus 4 (fifth controller at
        // 0xFF13C000; interrupts.txt "i2c-designware-4" irq 45 / GSI 0x2D).
        // Controller only - no subdevices yet. Stock Android holds both
        // cameras on bus 4 (OV5645 @ 0x3C and OV8825 @ 0x30, dmesg-stock
        // "camera pdata: I2C bus = 4"); they are not declared here.
        // Modeled on the stock W511 (Z2760) \_SB.I2C4: _UID = 5, _HRV = STEP.
        // Z2760 gated I2C4 on _STA (LCAM) and used STEP==2->CBUF (GSI 0x72,
        // omitted here - B0 is STEP One); the SBUF arm (GDMS FixedDMA
        // 0x001D,0x0004 / 0x001C,0x0005) is taken on this board exactly like
        // the other GDMS clients now that the CSRT publishes the request
        // lines. RBUF (PIO) is the A0 fallback.
        //
        Device (I2C4)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x05)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP)
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13C000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002D,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13C000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002D,
                    }
                    FixedDMA (0x001D, 0x0004, Width32bit, )
                    FixedDMA (0x001C, 0x0005, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
            }
        }

        //
        // Designware I2C controller for bus 5 (sixth controller at
        // 0xFF138000 + bus * 0x1000; iomem_A502CG: i2c-designware at
        // 0xFF13D000). Added to test the bus with a PRODUCTION driver
        // instead of SpbTestTool (both bus-2 and bus-1 SpbTestTool attempts
        // ended in error 995, so the tool itself is a suspect): bus 5 hosts
        // the Bosch BMA250E accelerometer at 0x18 (stock SFI DEVS + dmesg:
        // "I2C bus = 5, name = bma250, addr = 0x18"), and _HID BMA250E binds
        // the inbox Windows bma2x2 accelerometer driver. The stock "accel_int"
        // GPIO (pin 60) is declared as a GpioInt in ACC0._CRS - the Bosch UMDF
        // driver fails Code 10 at start without the data-ready line.
        //
        // Modeled on the stock W511 (Z2760) \_SB.I2C5: _UID = bus + 1 = 6,
        // GSI 0x2E, Level ActiveHigh, _HRV = STEP. The stock _DEP is
        // {PEP, IPC, GPO0}; GPO0 is now declared again but kept OUT of this
        // _DEP - it was only needed for slave GpioInts, which the ACC0 child
        // does not use. The _CRS STEP switch reproduces the Z2760 SBUF branch
        // (GDMS FixedDMA 0x001F,0x0002 / 0x001E,0x0003); on B0 (STEP = One)
        // the SBUF arm is taken, with RBUF (PIO) retained for A0 silicon.
        //
        Device (I2C5)
        {
            Name (_ADR, Zero)
            Name (_HID, "INT33B1")
            Name (_CID, "INT33B1")
            Name (_UID, 0x06)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_DEP, Package (0x02)
            {
                \_SB.PEP,
                \_SB.IPC
            })

            Method (_HRV, 0, NotSerialized)
            {
                Return (STEP)
            }

            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13D000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002E,
                    }
                })
                Name (SBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xFF13D000,         // Address Base
                        0x00000400,         // Address Length
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x0000002E,
                    }
                    FixedDMA (0x001F, 0x0002, Width32bit, )
                    FixedDMA (0x001E, 0x0003, Width32bit, )
                })
                If ((STEP == Zero))
                {
                    Return (RBUF)
                }
                Else
                {
                    Return (SBUF)
                }
            }

            //
            // Bosch BMA250E 3-axis accelerometer @ 0x18 (stock SFI DEVS "bma250
            // @ 0x18" on bus 5; dmesg-stock: "I2C bus = 5, name = bma250,
            // addr = 0x18"). _HID BMA250E binds the inbox Windows "BMA250E
            // accelerometer" driver (bma2x2 / UMDF). 100 kHz. Data-ready GpioInt on
            // the stock "accel_int" GPIO (pin 60 = 0x3C): the Bosch UMDF driver
            // fails Code 10 at OnPrepareHardware without it, even though it can
            // otherwise poll over the SPB bus.
            //
            Device (ACC0)
            {
                Name (_ADR, Zero)
                Name (_HID, "BMA250E")
                Name (_CID, "BMA250E")
                Name (_UID, One)
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0F)
                }

                Method (_CRS, 0, NotSerialized)
                {
                    Name (RBUF, ResourceTemplate ()
                    {
                        //
                        // Data-ready line. The Bosch BMA2x2 UMDF driver
                        // (17.58.19.497) fails with CM_PROB_FAILED_START (Code 10)
                        // at OnPrepareHardware when the sensor node has no
                        // GpioInt resource, so re-add the stock accel_int pin
                        // (GPO0 pin 60 = 0x3C, dmesg-stock line 225). Same format
                        // as the TOUC GpioInt (\_SB.GPO0, PullDefault).
                        //
                        GpioInt (Level, ActiveLow, Exclusive, PullDefault, 0x0000,
                            "\\_SB.GPO0", 0x00, ResourceConsumer, ,
                            )
                            {   // Pin list
                                0x003C
                            }
                        I2cSerialBusV2 (0x0018, ControllerInitiated, 0x000186A0,
                            AddressingMode7Bit, "\\_SB.I2C5",
                            0x00, ResourceConsumer, , Exclusive,
                            )
                    })
                    Return (RBUF)
                }
            }
        }

        //
        // Chipidea/ARC dual-role OTG controller. Two variants, selected by the
        // KDNET_USB build flag (see Platforms/ducatiPkg/ducatiPkg.dsc):
        //   - default: usb-host.asl  - forced host mode, Windows binds it as
        //     ACPI\\PNP0D20 / usbehci.sys (full _PS0/_STA/_DSM bring-up).
        //   - KDNET:   usb-debug.asl - stripped device-mode stub so the \\_SB.OTG0
        //     namepath the DBG2 table references resolves, while kdnet.sys drives
        //     the controller as a USB device via the DBG2 base addresses.
        //
#ifdef KDNET_USB
#include "usb-debug.asl"
#else
#include "usb-host.asl"
#endif
    }
}
