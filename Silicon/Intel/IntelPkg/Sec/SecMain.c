/** @file
  Generic Intel MID SEC phase.

  SoC-specific work that must run before the PEI Core dispatches lives in the platform PEIM.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "SecMain.h"

typedef
VOID
(EFIAPI *PEI_CORE_ENTRY_POINT)(
  IN CONST EFI_SEC_PEI_HAND_OFF  *SecCoreData,
  IN CONST EFI_PEI_PPI_DESCRIPTOR *PpiList
  );

STATIC
EFI_STATUS
EFIAPI
SecTemporaryRamSupport (
  IN CONST EFI_PEI_SERVICES  **PeiServices,
  IN EFI_PHYSICAL_ADDRESS    TemporaryMemoryBase,
  IN EFI_PHYSICAL_ADDRESS    PermanentMemoryBase,
  IN UINTN                   CopySize
  );

STATIC EFI_PEI_TEMPORARY_RAM_SUPPORT_PPI  mTemporaryRamSupportPpi = {
  SecTemporaryRamSupport
};

STATIC EFI_PEI_PPI_DESCRIPTOR  mPeiSecPpiList[] = {
  {
    EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
    &gEfiTemporaryRamSupportPpiGuid,
    &mTemporaryRamSupportPpi
  }
};

/**
  Migrate the temporary RAM contents into permanent memory and switch the
  stack over. Called by the PEI Core once real memory is installed.
**/
STATIC
EFI_STATUS
EFIAPI
SecTemporaryRamSupport (
  IN CONST EFI_PEI_SERVICES  **PeiServices,
  IN EFI_PHYSICAL_ADDRESS    TemporaryMemoryBase,
  IN EFI_PHYSICAL_ADDRESS    PermanentMemoryBase,
  IN UINTN                   CopySize
  )
{
  VOID                      *OldHeap;
  VOID                      *NewHeap;
  VOID                      *OldStack;
  VOID                      *NewStack;
  UINTN                     HeapSize;
  UINTN                     StackSize;
  INTN                      StackDelta;
  BASE_LIBRARY_JUMP_BUFFER  JumpBuffer;

  //
  // Lower half of temporary RAM is the PEI heap, upper half is the stack.
  //
  HeapSize  = CopySize >> 1;
  StackSize = CopySize - HeapSize;

  OldHeap = (VOID *)(UINTN)TemporaryMemoryBase;
  NewHeap = (VOID *)((UINTN)PermanentMemoryBase + StackSize);

  OldStack = (VOID *)((UINTN)TemporaryMemoryBase + HeapSize);
  NewStack = (VOID *)(UINTN)PermanentMemoryBase;

  CopyMem (NewHeap, OldHeap, HeapSize);
  CopyMem (NewStack, OldStack, StackSize);

  //
  // Do NOT use SwitchStack() here: it ASSERTs (EntryPoint != NULL) in
  // MdePkg/Library/BaseLib/SwitchStack.c line 52 and it never returns, while
  // the Temporary RAM Support PPI must return to the PEI Core with the very
  // same call frame, only relocated to permanent memory.
  //
  // Instead relocate the current stack frame exactly like
  // OvmfPkg/Sec/SecMain.c does: capture the context, add the
  // (new stack - old stack) delta to ESP/EBP and long-jump, so execution
  // resumes right here but running on the copied stack in permanent memory.
  //
  StackDelta = (INTN)((UINTN)NewStack - (UINTN)OldStack);

  if (SetJump (&JumpBuffer) == 0) {
    JumpBuffer.Esp = (UINT32)((INTN)JumpBuffer.Esp + StackDelta);
    JumpBuffer.Ebp = (UINT32)((INTN)JumpBuffer.Ebp + StackDelta);
    LongJump (&JumpBuffer, (UINTN)-1);
  }

  return EFI_SUCCESS;
}

/**
  Walk the FFS files in the boot firmware volume and return the entry point of
  the PEI Core.
**/
STATIC
EFI_STATUS
FindPeiCoreEntryPoint (
  IN  EFI_FIRMWARE_VOLUME_HEADER  *Fv,
  OUT VOID                        **EntryPoint
  )
{
  EFI_FFS_FILE_HEADER        *File;
  EFI_COMMON_SECTION_HEADER  *Section;
  UINTN                      FileOffset;
  UINTN                      FileSize;
  UINTN                      SectionOffset;
  UINTN                      SectionSize;
  UINT8                      *FvBase;

  if ((Fv == NULL) || (Fv->Signature != EFI_FVH_SIGNATURE)) {
    return EFI_NOT_FOUND;
  }

  FvBase     = (UINT8 *)Fv;
  FileOffset = Fv->HeaderLength;
  FileOffset = ALIGN_VALUE (FileOffset, 8);

  while (FileOffset < (UINTN)Fv->FvLength) {
    File = (EFI_FFS_FILE_HEADER *)(FvBase + FileOffset);

    FileSize = (UINTN)File->Size[0] |
               ((UINTN)File->Size[1] << 8) |
               ((UINTN)File->Size[2] << 16);

    if ((FileSize == 0) || (FileSize == 0xFFFFFF)) {
      break;
    }

    if (File->Type == EFI_FV_FILETYPE_PEI_CORE) {
      SectionOffset = sizeof (EFI_FFS_FILE_HEADER);

      while (SectionOffset < FileSize) {
        Section = (EFI_COMMON_SECTION_HEADER *)((UINT8 *)File + SectionOffset);

        SectionSize = (UINTN)Section->Size[0] |
                      ((UINTN)Section->Size[1] << 8) |
                      ((UINTN)Section->Size[2] << 16);

        if (SectionSize < sizeof (EFI_COMMON_SECTION_HEADER)) {
          break;
        }

        if ((Section->Type == EFI_SECTION_PE32) ||
            (Section->Type == EFI_SECTION_TE))
        {
          return PeCoffLoaderGetEntryPoint (
                   (VOID *)((UINT8 *)Section + sizeof (EFI_COMMON_SECTION_HEADER)),
                   EntryPoint
                   );
        }

        SectionOffset = ALIGN_VALUE (SectionOffset + SectionSize, 4);
      }
    }

    FileOffset = ALIGN_VALUE (FileOffset + FileSize, 8);
  }

  return EFI_NOT_FOUND;
}

/**
  C entry point of the SEC phase.
**/
VOID
EFIAPI
SecStartup (
  IN UINT32  SizeOfRam,
  IN UINT32  TempRamBase,
  IN VOID    *BootFirmwareVolume
  )
{
  EFI_SEC_PEI_HAND_OFF  SecCoreData;
  PEI_CORE_ENTRY_POINT  PeiCoreEntryPoint;
  EFI_STATUS            Status;
  VOID                  *EntryPoint;

  SerialPortInitialize ();

  // Print UEFI version message
  DEBUG ((EFI_D_WARN, "\n"));
  DEBUG ((EFI_D_WARN, "%s for %a %a\n", PcdGetPtr (PcdFirmwareVersionString), FixedPcdGetPtr (PcdSmbiosSystemManufacturer), FixedPcdGetPtr (PcdSmbiosSystemModel)));
  DEBUG ((EFI_D_WARN, "Built at %a on %a\n",  __TIME__, __DATE__));
  DEBUG ((EFI_D_WARN, "\n"));

  DEBUG ((
    DEBUG_INFO,
    "  Image base 0x%08x  TempRam 0x%08x (0x%x bytes)  BFV 0x%08x\n",
    FixedPcdGet32 (PcdFdBaseAddress),
    TempRamBase,
    SizeOfRam,
    (UINT32)(UINTN)BootFirmwareVolume
    ));

  Status = FindPeiCoreEntryPoint (
             (EFI_FIRMWARE_VOLUME_HEADER *)BootFirmwareVolume,
             &EntryPoint
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SEC: PEI Core not found in BFV! %r\n", Status));
    CpuDeadLoop ();
  }

  ZeroMem (&SecCoreData, sizeof (SecCoreData));
  SecCoreData.DataSize               = (UINT16)sizeof (EFI_SEC_PEI_HAND_OFF);
  SecCoreData.BootFirmwareVolumeBase = BootFirmwareVolume;
  SecCoreData.BootFirmwareVolumeSize =
    (UINTN)((EFI_FIRMWARE_VOLUME_HEADER *)BootFirmwareVolume)->FvLength;
  SecCoreData.TemporaryRamBase = (VOID *)(UINTN)TempRamBase;
  SecCoreData.TemporaryRamSize = SizeOfRam;
  SecCoreData.PeiTemporaryRamBase = SecCoreData.TemporaryRamBase;
  SecCoreData.PeiTemporaryRamSize = SizeOfRam >> 1;
  SecCoreData.StackBase = (VOID *)((UINTN)TempRamBase + (SizeOfRam >> 1));
  SecCoreData.StackSize = SizeOfRam - (SizeOfRam >> 1);

  DEBUG ((DEBUG_INFO, "SEC: entering PEI Core at 0x%08x\n", (UINT32)(UINTN)EntryPoint));

  PeiCoreEntryPoint = (PEI_CORE_ENTRY_POINT)EntryPoint;
  PeiCoreEntryPoint (&SecCoreData, mPeiSecPpiList);

  CpuDeadLoop ();
}
