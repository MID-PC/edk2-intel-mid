//
// z2580 CPU performance/idle table (\_PR.P000..P003).
//
// STUB - the z2580 _PSS/_CST values have not been captured yet (no stock SFI
// FREQ data or validated DSDT fragment for the 2.0 GHz part exists in this
// tree; only z2520 in Dsdt2.asl and z2560 in Dsdt.asl were available).
//
// The z2580 is a 2.0 GHz upper-bin Z25xx part but shares the Cloverview
// silicon, so this file should mirror PrZ2560.asl's structure with the real
// SFI FREQ P-states (expected ratios on the ~133 MHz interface clock) filled
// in. Until then, selecting SOC_VARIANT_Z2580 deliberately fails the build:
// compiling an empty \_PR with the four enabled MADT LAPICs would hand the OS
// processors it can never power-manage.
//
#error "PrZ2580.asl: z2580 \_PSS data not captured yet - fill this file or use SOC_VARIANT_Z2560/Z2520"