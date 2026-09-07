/** @file
  Definitions for the generic Intel MID SEC phase.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef INTEL_MID_SEC_MAIN_H_
#define INTEL_MID_SEC_MAIN_H_

#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PeCoffGetEntryPointLib.h>
#include <Ppi/TemporaryRamSupport.h>
#include <Ppi/SecPlatformInformation.h>

/**
  C entry point of the SEC phase.

  Called from Ia32/SecEntry.nasm with a temporary stack already in place.

  @param[in] SizeOfRam      Size of the temporary RAM region.
  @param[in] TempRamBase    Base of the temporary RAM region.
  @param[in] BootFirmwareVolume  Base of the boot firmware volume that carries
                                 the PEI Core.
**/
VOID
EFIAPI
SecStartup (
  IN UINT32  SizeOfRam,
  IN UINT32  TempRamBase,
  IN VOID    *BootFirmwareVolume
  );

#endif // INTEL_MID_SEC_MAIN_H_