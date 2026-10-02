/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/SfiTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#define SFI_SIG_SYST  "SYST"
#define SFI_SIG_XSDT  "XSDT"

// SYST pointer entries bounded by the search area.
#define SFI_SYST_MAX_POINTERS  (SFI_SEARCH_SIZE / sizeof (UINT64))

// Offset from the end of the XSDT header to the UINT32 holding the address of the
// table XSDT points at. XSDT is 44 bytes on every board measured, so it carries
// exactly one such entry; see SfiFindXsdtChild().
#define SFI_XSDT_CHILD_OFFSET  12

/**
  Is a table address inside the SFI search area, with room for a header?

  @param[in] Addr  Candidate table address.

  @retval TRUE   The address is inside the area and a header fits.
  @retval FALSE  Otherwise; the candidate cannot be dereferenced.
**/
STATIC
BOOLEAN
SfiAddrInWindow (
  IN UINTN  Addr
  )
{
  return (Addr >= SFI_SEARCH_BASE) &&
         (Addr <= (SFI_SEARCH_END - sizeof (SFI_TABLE_HEADER)));
}

/**
  Validate an SFI table header.

  @param[in] Header  Table header to validate.

  @retval TRUE   Length within the search area and correct checksum.
  @retval FALSE  Length, bounds or checksum failed.
**/
STATIC
BOOLEAN
SfiTableIsValid (
  IN CONST SFI_TABLE_HEADER  *Header
  )
{
  UINTN       TableAddr;
  UINTN       Index;
  CONST UINT8 *Bytes;
  UINT8       Sum;

  TableAddr = (UINTN)Header;

  // Table must fit inside the SFI area
  if ((Header->Len < sizeof (*Header)) ||
      (Header->Len > (SFI_SEARCH_END - TableAddr)))
  {
    return FALSE;
  }

  // SFI checksum: all bytes must sum to zero. The accumulator is deliberately
  // UINT8 so the addition wraps; accumulating in a wider type looks like it
  // works and silently rejects every real table.
  Bytes = (CONST UINT8 *)Header;
  Sum   = 0;
  for (Index = 0; Index < Header->Len; Index++) {
    Sum += Bytes[Index];
  }

  return (Sum == 0);
}

/**
  Check that a header carries the given signature.

  @param[in] Header    Table header to check.
  @param[in] Signature Four-character table signature.

  @retval TRUE   The header matches the signature.
  @retval FALSE  Otherwise.
**/
STATIC
BOOLEAN
SfiTableIs (
  IN CONST SFI_TABLE_HEADER  *Header,
  IN CONST CHAR8             *Signature
  )
{
  return CompareMem (Header->Sig, Signature, 4) == 0;
}

/**
  Find SYST, the one table that has to be looked for by scanning.

  Everything else is reachable through SYST's pointer array, so this is the only
  place the walk step matters. SYST is the first table written and its payload is
  aligned, so it is 16-byte aligned on every board measured; nothing else reliably
  is, which is why the other tables are reached by pointer.

  @return  Pointer to the validated SYST table, or NULL when not found.
**/
STATIC
CONST SFI_TABLE_HEADER *
SfiFindSyst (
  VOID
  )
{
  UINTN                  Address;
  CONST SFI_TABLE_HEADER  *Header;

  for (Address = SFI_SEARCH_BASE;
       Address < SFI_SEARCH_END;
       Address += SFI_SEARCH_STRIDE)
  {
    Header = (CONST SFI_TABLE_HEADER *)Address;

    if (SfiTableIs (Header, SFI_SIG_SYST) && SfiTableIsValid (Header)) {
      return Header;
    }
  }

  return NULL;
}

/**
  Return the single table XSDT points at.

  MCFG is the one table SYST does not list on any board measured, so the bootloader
  puts it in XSDT instead. XSDT's payload is 20 bytes and that address is the
  UINT32 at offset 12 from the end of the header; the surrounding fields are
  unidentified and no board publishes a second entry, so only the first is read.

  @param[in] Xsdt  Validated XSDT table.

  @return  Pointer to the validated table XSDT points at, or NULL.
**/
STATIC
CONST SFI_TABLE_HEADER *
SfiFindXsdtChild (
  IN CONST SFI_TABLE_HEADER  *Xsdt
  )
{
  CONST SFI_TABLE_HEADER  *Child;
  UINTN                   Address;

  if (Xsdt->Len < (sizeof (*Xsdt) + SFI_XSDT_CHILD_OFFSET + sizeof (UINT32))) {
    return NULL;
  }

  Address = *(CONST UINT32 *)((CONST UINT8 *)Xsdt + sizeof (*Xsdt) +
                              SFI_XSDT_CHILD_OFFSET);
  if (!SfiAddrInWindow (Address)) {
    return NULL;
  }

  Child = (CONST SFI_TABLE_HEADER *)Address;

  return SfiTableIsValid (Child) ? Child : NULL;
}

CONST SFI_TABLE_HEADER *
EFIAPI
SfiFindTable (
  IN CONST CHAR8  *Signature
  )
{
  CONST SFI_TABLE_HEADER  *Syst;
  CONST SFI_TABLE_HEADER  *Xsdt;
  CONST SFI_TABLE_HEADER  *Header;
  CONST UINT64            *Pointer;
  UINTN                   Count;
  UINTN                   Index;

  //
  // Only the four signature bytes are ever compared, so a longer name would
  // match a table it does not name.
  //
  if ((Signature == NULL) || (AsciiStrLen (Signature) != 4)) {
    return NULL;
  }

  Syst = SfiFindSyst ();
  if (Syst == NULL) {
    DEBUG ((
      DEBUG_WARN,
      "SfiTableLib: no SFI SYST table in 0x%llx-0x%llx\n",
      (UINT64)SFI_SEARCH_BASE,
      (UINT64)SFI_SEARCH_END
      ));
    return NULL;
  }

  Xsdt  = NULL;
  Count = (Syst->Len - sizeof (*Syst)) / sizeof (UINT64);
  if (Count > SFI_SYST_MAX_POINTERS) {
    Count = SFI_SYST_MAX_POINTERS;
  }

  for (Index = 0; Index < Count; Index++) {
    Pointer = (CONST UINT64 *)((CONST UINT8 *)Syst + sizeof (*Syst) +
                               (Index * sizeof (UINT64)));

    // Only follow pointers inside the SFI area; others could be garbage.
    if (!SfiAddrInWindow ((UINTN)*Pointer)) {
      continue;
    }

    Header = (CONST SFI_TABLE_HEADER *)(UINTN)*Pointer;
    if (!SfiTableIsValid (Header)) {
      continue;
    }

    if (SfiTableIs (Header, Signature)) {
      DEBUG ((DEBUG_VERBOSE, "SfiTableLib: %a found at 0x%llx\n", Signature, (UINT64)(UINTN)Header));
      return Header;
    }

    if (SfiTableIs (Header, SFI_SIG_XSDT)) {
      Xsdt = Header;
    }
  }

  if (Xsdt != NULL) {
    Header = SfiFindXsdtChild (Xsdt);
    if ((Header != NULL) && SfiTableIs (Header, Signature)) {
      DEBUG ((DEBUG_VERBOSE, "SfiTableLib: %a found at 0x%llx via XSDT\n", Signature, (UINT64)(UINTN)Header));
      return Header;
    }
  }

  DEBUG ((DEBUG_WARN, "SfiTableLib: this boot publishes no SFI %a table\n", Signature));
  return NULL;
}
