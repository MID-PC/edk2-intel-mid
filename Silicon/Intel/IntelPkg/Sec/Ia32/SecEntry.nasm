;------------------------------------------------------------------------------
; @file
;   Generic Intel MID SEC entry point (IA32)
;
;   The primary bootloader loads this image at PcdFdBaseAddress (e.g. 0x01101000 on
;   Cloverview) and jumps to its first byte in 32-bit flat protected mode,
;   interrupts disabled, paging off - exactly the state the stock Intel
;   bootstub was entered in. This file MUST therefore be linked at offset 0 of
;   the final binary (which is what patch_sec_entry.py does)
;
;   No hardware initialization is performed here; we only build a known-good flat GDT, set up a
;   temporary stack in DRAM and call into C.
;
; SPDX-License-Identifier: BSD-2-Clause-Patent
;------------------------------------------------------------------------------

    SECTION .text

BITS 32

extern ASM_PFX(SecStartup)

%define FD_BASE          FixedPcdGet32 (PcdFdBaseAddress)
%define TEMP_RAM_BASE    FixedPcdGet32 (PcdSecPeiTemporaryRamBase)
%define TEMP_RAM_SIZE    FixedPcdGet32 (PcdSecPeiTemporaryRamSize)

;
; Entry point - offset 0 of the firmware image.
;
global ASM_PFX(_ModuleEntryPoint)
ASM_PFX(_ModuleEntryPoint):
    cli
    cld

    ;
    ; Load our own flat GDT; do not trust whatever the loader left behind.
    ;
    lgdt    [cs:GdtDescriptor]

    ;
    ; Reload CS with a far jump to our 32-bit flat code selector (0x08).
    ;
    jmp     0x08:FlatMode

FlatMode:
    mov     eax, 0x10                   ; flat data selector
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ;
    ; Temporary stack lives at the top of the temporary RAM window.
    ;
    mov     esp, TEMP_RAM_BASE + TEMP_RAM_SIZE
    and     esp, ~0xF

    ;
    ; Clear the temporary RAM so PEI starts from a known state.
    ;
    mov     edi, TEMP_RAM_BASE
    mov     ecx, TEMP_RAM_SIZE / 4
    xor     eax, eax
    rep     stosd

    ;
    ; SecStartup (SizeOfRam, TempRamBase, BootFirmwareVolume)
    ;
    ; The boot firmware volume immediately follows the SEC region inside the
    ; same image; its address is patched in by the FDF layout below.
    ;
    mov     eax, ASM_PFX(gBootFirmwareVolumeBase)
    push    dword [eax]
    push    dword TEMP_RAM_BASE
    push    dword TEMP_RAM_SIZE
    call    ASM_PFX(SecStartup)

    ;
    ; SecStartup never returns.
    ;
DeadLoop:
    hlt
    jmp     DeadLoop

;------------------------------------------------------------------------------
; Flat 32-bit GDT
;------------------------------------------------------------------------------
align 16
GdtBase:
    ; 0x00 - null
    dq  0
    ; 0x08 - flat code, base 0, limit 4G, ring 0
    dw  0xFFFF
    dw  0x0000
    db  0x00
    db  0x9B
    db  0xCF
    db  0x00
    ; 0x10 - flat data, base 0, limit 4G, ring 0
    dw  0xFFFF
    dw  0x0000
    db  0x00
    db  0x93
    db  0xCF
    db  0x00
GdtEnd:

align 16
GdtDescriptor:
    dw  GdtEnd - GdtBase - 1
    dd  GdtBase

;
; Base of the FV that carries the PEI Core. Patched by GenFv through the
; FDF "gIntelMidTokenSpaceGuid.PcdFdBaseAddress" arithmetic below; we simply
; keep it as a relocatable dword the C code can read.
;
global ASM_PFX(gBootFirmwareVolumeBase)
ASM_PFX(gBootFirmwareVolumeBase):
    dd  FD_BASE + 0x10000
