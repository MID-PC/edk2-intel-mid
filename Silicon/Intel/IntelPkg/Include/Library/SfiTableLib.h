/** @file
  Locate tables published by a Simple Firmware Interface bootloader. SFI 1.0
  deposits them in the legacy BIOS area between 0x000E0000 and 0x00100000; this
  library owns the shared 24-byte header and the rules for finding one by signature.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SFI_TABLE_LIB_H_
#define SFI_TABLE_LIB_H_

#include <Uefi/UefiBaseType.h>

//
// The area the bootloader publishes SFI tables into.
//
#define SFI_SEARCH_BASE   0x000E0000ULL
#define SFI_SEARCH_SIZE   0x00020000ULL
#define SFI_SEARCH_END    (SFI_SEARCH_BASE + SFI_SEARCH_SIZE)

//
// Step the bootstrap scan takes over the search area. This is only sound for
// SYST; see the comment on SfiFindSyst().
//
#define SFI_SEARCH_STRIDE  16

//
// The 24-byte header common to every SFI table.
//
#pragma pack(1)

typedef struct {
  CHAR8   Sig[4];
  UINT32  Len;
  UINT8   Rev;
  UINT8   Csum;
  CHAR8   OemId[6];
  CHAR8   OemTableId[8];
} SFI_TABLE_HEADER;

#pragma pack()

/**
  Locate a published SFI table by its four-character signature.
  This walks SYST's pointer array rather than scanning: only SYST and XSDT are
  16-byte aligned on the boards in dumps/, and MMAP never is.

  @param[in] Signature  Four-character table signature, NUL terminated ("MMAP").
  @return  Pointer to the validated table, or NULL when this boot does not publish one.
**/
CONST SFI_TABLE_HEADER *
EFIAPI
SfiFindTable (
  IN CONST CHAR8  *Signature
  );

#endif // SFI_TABLE_LIB_H_
