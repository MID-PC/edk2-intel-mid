/** @file
  DSDT for Asus ZenFone 6 A600CG/T00G (Intel Atom Z2580, Clover Trail+),
  PEP-focused revision.

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
        * 0xFF11D000  North-Complex PM unit (iomem: intel_pmu_driver)
        * 0xFF11C000  SCU IPC-1 mailbox     (iomem: intel_scu_ipc)
        * 0x7FB00000  PCI MMCONFIG - IOSF sideband / MBI / PUNIT fallback
                      path (base confirmed by the SFI MCFG table and A600CG
                      iomem: "7fb00000-7fbfffff : PCI MMCONFIG"; NOT the
                      A502CG's 0x3EF00000)
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
    - NO \_SB.I2Cn controllers are declared. The six Designware I2C engines
      (buses 0..5 at 0xFF138000 + bus * 0x1000) and their board children
      (codec/charger/fuel-gauge/touchscreen/accelerometer) were deliberately
      dropped in this revision: they are board-inventory devices, not part of
      the PEP bring-up, and the A502CG-derived declarations don't match the
      ZenFone 6's silicon (RT5647 codec @ 0x1B, smb345 @ 0x6A, max17048 @ 0x36,
      Synaptics touch, KXTJ9 @ 0x0F) anyway. Their MMIO windows are still
      covered by the SYSR reservation, so nothing will grab them.
     - \_SB.SDC1 (INT33BB, NEW in this revision): Wi-Fi SDIO host controller
       for the Broadcom BCM4330 at 0xFFA48000 (PCI 00:04.1 [8086:08FA], mmc2),
       GSI 0x2A, _UID 0x03 (unique per _HID, not colliding with SDC0's 0x02),
       _DEP on PEP + GPO1. \_SB.SDC1._PS0 asserts GPO1.WLEN to power up the
       WLAN chip; child \_SB.SDC1.BRCM (_ADR 1, _RMV = 0) is minimal (no
       GpioInt, no _PRW). bcmdhd63.inf (Drivers/WIFI) binds function 1 via
       SD\VID_02D0&PID_4330&FN_1.

  Devices REMOVED in this revision (each carried _DEP on \_SB.PEP, directly
  or through a GPIO bank, i.e. they actually require the PEP to start):
    AUD1 (SST audio engine), EMM0 (eMMC). They are dropped for now so the PEP
    itself can be brought up and validated first; their MMIO stays covered by
    SYSR so nothing else grabs it, and they can be layered back one group at a
    time.

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
    // Platform configuration values that the OTG0 methods read.
    // Declared as plain integers with this board's values.
    //
    // STEP: CPU stepping. CPU-Z reports stepping 1 on this board, so STEP is
    // One (B0 silicon). OTG0's internal (STEP == One) || (STEP == 0x02)
    // checks pass with STEP = One.
    //
    Name (\STEP, One)
    Name (\USBS, 0x00)
    Name (\UOFD, 0x00)
    // (SPI FixedDMA/DMAS experiment reverted - see the SPI1/SPI2 comment.)

    Scope (\_PR)
    {
        //
        // CPU-specific PPM block (Processor P000..P003), selected by the
        // build's SOC_VARIANT define (Asus ZenFone 6 = SOC_VARIANT_Z2580, see
        // t00gPkg.dsc:
        //   PrZ2520  1.2 GHz part on a 100 MHz FSB (7 P-states)
        //   PrZ2560  1.6 GHz part on a ~133 MHz FSB (4 P-states)
        //   PrZ2580  2.0 GHz part on a ~133 MHz FSB (6 P-states,
        //            SFI FREQ: 2000/1866/1600/1333/933/800)
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
        // unit at 0xFF11D000, SCU IPC at 0xFF11C000, MMCONFIG at 0x7FB00000)
        // without conflict. Mirrors the SFI MMAP / iomem_A600CG.txt map. The
        // 0xFF000000 window stays split so the PNP0C02 reservation does not
        // collide with OTG0's 0xFFA60000 block or the IPC device's own _CRS.
        // The MMIO of the PEP-client devices not declared in this revision
        // (I2C controllers, GPIO banks, SDHCs, SST) deliberately remains
        // inside these claims so nothing else can grab it.
        // SDC0's block at 0xFFA58000 is inside the 0xFF11C400 claim, which is
        // a plain PNP0C02 reservation, so its _CRS does not conflict.
        //
        Device (SYSR)
        {
            Name (_HID, EisaId ("PNP0C02"))
            Name (_UID, 0x01)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00D00000, 0x00300000) // low MMIO
                Memory32Fixed (ReadWrite, 0x36FF0000, 0x00A0D000) // SoC MMIO (IMR4 / RAR stolen base 0x36FF0000, same as A502CG)
                Memory32Fixed (ReadWrite, 0x7FB00000, 0x00100000) // MMCONFIG
                // 0x7FC00000+0x00400000 (graphics stolen framebuffer) is now
                // produced by \_SB.PCI0 so the GPU can claim it if Windows
                // assigns a BAR there; the firmware GOP keeps using it until
                // an OS display driver takes over.
                // 0x80000000+0x10000000 (GPU GMMADR) is claimed by
                // \_SB.PCI0.GFX0 instead
                Memory32Fixed (ReadWrite, 0xDF800000, 0x00600000) // 0xDFE00000+ produced by PCI0 for GFX0
                Memory32Fixed (ReadWrite, 0xFA000000, 0x04000000)
                Memory32Fixed (ReadWrite, 0xFEC00000, 0x00001000) // IOAPIC 0
                Memory32Fixed (ReadWrite, 0xFEC10000, 0x00001000) // IOAPIC 1
                Memory32Fixed (ReadWrite, 0xFEE00000, 0x00001000) // LAPIC
                Memory32Fixed (ReadWrite, 0xFF000000, 0x0011C000) // SoC MMIO below SCU IPC
                Memory32Fixed (ReadWrite, 0xFF11C400, 0x00943C00) // SoC MMIO above SCU IPC
                Memory32Fixed (ReadWrite, 0xFFA61000, 0x00596FC0) // below IPC mailbox
                Memory32Fixed (ReadWrite, 0xFFFF8000, 0x00008000) // above IPC mailbox
                Memory32Fixed (ReadWrite, 0x7FAFF000, 0x00001000) // firmware console-state page (FrameBufferSerialPortLib)
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
        // PCI root bridge + the two REAL PCI devices on this SoC.
        //
        // iomem.txt shows exactly two genuinely PCI-addressable functions on
        // the A600CG; everything else (I2C/SPI/GPIO/USB/SDHCI/HSU/DMA) is
        // Linux "pci glue" and is declared here as its own ACPI device, not as
        // a PCI function:
        //   0000:00:02.0 - PowerVR SGX544MP GPU, owns 80000000-8fffffff
        //                  (GMMADR region 0), dfec0000-dfefffff and
        //                  dff00000-dfffffff (pvrsrvkm)
        //   0000:00:03.0 - Intel ISP 2300 camera, owns df800000-dfbfffff
        // Exposing these under a PNP0A03/08 root lets Windows' pci.sys
        // enumerate them by PCI VID/DID, matching Graphics/49889.inf
        // (PCI\VEN_8086&DEV_08C7..) and Camera/camera.inf
        // (PCI\VEN_8086&DEV_08D0). The stock Z2760 DSDT and the working DSDT2
        // declare the GPU the same way (_ADR 0x00020000 under PCI0).
        //
        // Config space is reached through the legacy CF8/CFC ports - the same
        // path the firmware PCICFG probe uses successfully on this board.
        // MCFG is intentionally NOT published: A600CG's iomem maps 0x7FB00000
        // as a single-bus MMCONFIG, and an earlier experiment publishing it over
        // bus 0-255 reset the board exactly when setup.exe's full device-stack
        // phase made pci.sys issue MMCONFIG reads. CF8/CFC avoids that.
        //
        // The ISP's _DEP on \_SB.PEP and _PR3 to \_SB.PEP.ID3C mirror the
        // stock Z2760 ISP0; GFX0 additionally matches the existing DSDT2
        // declaration. The _CRS memory producer windows cover both devices'
        // real BAR ranges from iomem.txt (GPU at 0x7FC00000-0x8FFFFFFF and
        // 0xDFE00000-0xDFFFFFFF, ISP at 0xDF800000-0xDFBFFFFF).
        //
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_CID, EisaId ("PNP0A03"))
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
            // iomem.txt. The lower window starts at 0x7FC00000 so the
            // framebuffer steal aperture sits inside the producer range and
            // can be handed to the GPU.
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
                    0x00000000, 0x7FC00000, 0x8FFFFFFF, 0x00000000, 0x10400000,,, )
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0xDF800000, 0xDFBFFFFF, 0x00000000, 0x00400000,,, )
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x00000000, 0xDFE00000, 0xDFFFFFFF, 0x00000000, 0x00200000,,, )
            })
            //
            // Give the OS control over PCIe features (native hotplug/PME/AER),
            // matching the native U-Boot southcluster.asl implementation.
            //
            Method (_OSC, 4, NotSerialized)
            {
                If (LEqual (Arg0, ToUUID ("33db4d5b-1ff7-401c-9657-7441c03dd766")))
                {
                    Return (Arg3)
                }
                Else
                {
                    CreateDWordField (Arg3, 0, CDW1)
                    Or (CDW1, 4, CDW1)
                    Return (Arg3)
                }
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Device (GFX0)
            {
                Name (_ADR, 0x00020000)
                Name (_DDN, "PowerVR SGX544MP Graphics")
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
        // IOSF mailbox window claimed by the PEP's PUNIT/MBI mailbox traffic.
        // clvpep.sys MmMapIoSpace()'s 0xE00000D0 (size 8) and performs a
        // write-then-readback command/data exchange at +0/+4; the stock Z2760
        // DSDT exposes exactly this device (MBID / INT33BD, 0xE00000D0 length
        // 8, _UID 1) so the MMIO is claimed in the namespace and stays out of
        // pci.sys' producer pool (our PCI0 _CRS windows end at 0xDFFFFFFF, so
        // this region is otherwise unclaimed on T00G). No _STA needed: like
        // the stock w511 MBID, the device is always present.
        //
        Device (MBID)
        {
            Name (_HID, "INT33BD")
            Name (_CID, "INT33BD")
            Name (_UID, One)
            Method (_CRS, 0, Serialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite,
                        0xE00000D0,
                        0x00000008)
                })
                Return (RBUF)
            }
        }

        //
        // Power button. The power key is serviced by the MSIC/PMIC (this
        // board's SFI DEVS lists msic_power_btn; the SFI GPIO table has no
        // power-key pin and the board has no I2C/EC chain), so it is a plain
        // PNP0C0C device that Windows' ACPI button driver claims. There is no
        // GPIO _AEI/_E39 delivery like the Z2760's I2C2 GCMD loop: the key
        // interrupt is the MSIC line over the SCU (SFI irq 0x43 / GSI 67), and
        // assuming the PEP loads, clvpep.sys itself virtualizes the MSIC power
        // button and performs reset/S5 through the SCU + NC PM unit (both
        // reserved by SYSR above). The GPIO volume-key array (TBAD) is
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
        //   GPO0 = AON  bank, 0xFF119000, _UID 1, _HRV 0 (REV_0000)
        //   GPO1 = CORE bank, 0xFF13F000, _UID 2, _HRV 1 (REV_0001)
        // The STEP-gated _HRV methods below mirror the stock w511 DSDT; with
        // STEP = One (this board's B0 silicon) GPO0 returns 0 -> GpioClv binds
        // it as the A0_B0_AON controller and GPO1 returns One -> A0_B0_CORE,
        // matching the kernel's PCI IDs 0x08eb (AON) / 0x08f7 (CORE).
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

            Name (AVBL, Zero)
            Method (_REG, 2, NotSerialized)
            {
                If ((Arg0 == 0x08))
                {
                    AVBL = Arg1
                }
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
        // Declaring them provides the FixedDMA request lines used by SPI1/SPI2
        // (and on the Z2760 reference by the I2C/UART nodes) with a real
        // provider, matching the ship BIOS exactly. STEP=One -> GDMS returns
        // RBUF (GSI 0x71 variant is only for STEP == 0x02). Resource templates
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
        // (Uart16550pc.sys, SerCx-based) and uses PIO - FixedDMA was tried
        // (channels 0-5 on UDMS, serviced by the "Intel(R) UART DMA"
        // HalExtIntcUartDMA ext) but every UART failed to start with Code 10
        // "The requested operation was unsuccessful". This matches the HSU
        // DMA data path being unreliable on Clovertrail: mainline Linux
        // deliberately disables it ("RX data is never delivered", 8250_mid.c
        // clv_board) and falls back to PIO (dma_index 0/1/2 confirms our
        // Z2760 channel numbers were right - the silicon DMA path itself is
        // what is broken). No _DEP on the DMA controllers - SPI regression
        // lesson (dependent devices stopped enumerating).
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
                })
                Return (RBUF) /* \_SB_.URT2._CRS.RBUF */
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // SPI controllers (DesignWare SSC) - PCI 00:00.2 (SPI1) and
        // 00:02.4 (SPI2). Windows binds the SPB Framework "spi.sys"
        // (SpbCx-based) on ACPI\INT33B0. Bases/GSIs follow the stock SFI
        // this board: dw_spi0@0xFF135000/GSI 9 and dw_spi1@0xFF136000/GSI 38
        // (dmesg-stock.txt, iomem.txt:44-47).
        //
        // DMAS experiment (Z2760 FixedDMA SBUF, _DEP on GDMS) is REVERTED:
        // enabling it made SPI1/SPI2 vanish from Device Manager entirely even
        // though GDMS/UDMS started OK. The Z2760 numbers are retained in git
        // history; do not re-add _DEP (the dependent devices stop enumerating)
        // and only retry FixedDMA once the enumeration regression is diagnosed.
        // SPI must stay on the known-good PIO RBUF path.
        //
        Device (SPI1)
        {
            Name (_HID, "INT33B0")
            Name (_CID, "INT33B0")
            Name (_DDN, "Cloverview SPI Controller")
            Name (_UID, One)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF135000, 0x00000400)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000009,
                    }
                })
                Return (RBUF) /* \_SB_.SPI1._CRS.RBUF */
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        Device (SPI2)
        {
            Name (_HID, "INT33B0")
            Name (_CID, "INT33B0")
            Name (_DDN, "Cloverview SPI Controller")
            Name (_UID, 0x02)
            Method (_CRS, 0, NotSerialized)
            {
                Name (RBUF, ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0xFF136000, 0x00000400)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                    {
                        0x00000026,
                    }
                })
                Return (RBUF) /* \_SB_.SPI2._CRS.RBUF */
            }
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        //
        // SD card host controller (mmc1 at 0xFFA58000 per iomem_A600CG.txt,
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
        //  - BRCM child is minimal (_ADR + _RMV only, no GpioInt, no _PRW)
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
                Method (_RMV, 0, NotSerialized)
                {
                    Return (Zero)
                }
            }
        }

        //
        // No \_SB.I2Cn controllers are declared in this revision - the six
        // Designware I2C buses and their board children (codec/charger/
        // fuel-gauge/touch/accelerometer) were dropped as device-specific.
        // Their MMIO windows remain covered by the SYSR reservation above.
        //
        //
        // Chipidea/ARC dual-role OTG controller. Two variants, selected by the
        // KDNET_USB build flag (see Platforms/t00gPkg/t00gPkg.dsc):
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
