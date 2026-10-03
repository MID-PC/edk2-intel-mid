;------------------------------------------------------------------------------
; @file
;   Generic Intel MID SEC entry point (X64 / long-mode capable SoCs)
;
;   On 64-bit Moorefield style parts (e.g. Asus Zenfone Zoom / ZX551ML) the
;   primary bootloader loads this image at PcdFdBaseAddress (0x10F00000, the
;   slot that normally holds the boot image "second bootloader") and jumps to
;   its first byte in 32-bit flat protected mode, interrupts disabled and
;   paging off - exactly the state the stock bootstub was entered in.
;
;   Unlike the bootstub, which handed a 32-bit kernel a ready made environment,
;   we have to reach long mode ourselves. The stock bootstub relied on the
;   Linux kernel doing the protected->long-mode switch; here nothing does it
;   for us so SEC performs the transition:
;
;     1. build a known-good flat 32-bit GDT and reload the segment registers
;     2. build identity-mapped 4-level page tables covering the low 4 GiB
;        using 2 MiB pages (PML4 -> one PDPTE -> 4 PDs of 512 * 2 MiB)
;     3. enable PAE, set LME in EFER, turn on paging -> the CPU is now in
;        IA-32e compatibility mode
;     4. load a GDT with a 64-bit code descriptor and far-jump into it, which
;        activates 64-bit long mode
;     5. set up a temporary stack in DRAM and call the C entry (SecStartup)
;
;   The page tables are placed at the top of the temporary RAM window so they
;   do not collide with the stack/heap that PEI later relocates. DxeIpl keeps
;   running in long mode from here (PcdDxeIplSwitchToLongMode|TRUE) so nothing
;   ever drops back to 32-bit.
;
;   This file MUST be linked at offset 0 of the final binary (patch_sec_entry.py
;   makes offset 0 a direct jmp to the real entry).
;
; SPDX-License-Identifier: BSD-2-Clause-Patent
;------------------------------------------------------------------------------

    SECTION .text

extern ASM_PFX(SecStartup)

%define FD_BASE          FixedPcdGet32 (PcdFdBaseAddress)
%define TEMP_RAM_BASE    FixedPcdGet32 (PcdSecPeiTemporaryRamBase)
%define TEMP_RAM_SIZE    FixedPcdGet32 (PcdSecPeiTemporaryRamSize)

;
; Page tables live in the top 24 KiB of the temporary RAM window:
;   PML4  (4 KiB)  - 1 present entry -> PDPT
;   PDPT  (4 KiB)  - 4 present entries -> 4 page directories (4 GiB total)
;   PD0..PD3 (16 KiB) - 512 * 4 = 2048 2 MiB pages = 4 GiB identity map
; That is 6 pages = 24 KiB. Reserve the top 32 KiB to be safe.
;
%define PT_TOTAL_SIZE    0x8000
%define PT_BASE          (TEMP_RAM_BASE + TEMP_RAM_SIZE - PT_TOTAL_SIZE)
%define PML4_BASE        (PT_BASE + 0x0000)
%define PDPT_BASE        (PT_BASE + 0x1000)
%define PD_BASE          (PT_BASE + 0x2000)   ; PD0..PD3 span 0x2000-0x5FFF

; Paging entry flags: Present | ReadWrite (| PageSize for 2 MiB leaves)
%define PG_P             0x1
%define PG_RW            0x2
%define PG_PS            0x80

BITS 32

;
; Entry point - offset 0 of the firmware image.
;
global ASM_PFX(_ModuleEntryPoint)
ASM_PFX(_ModuleEntryPoint):
    cli
    cld

    ;
    ; Load our own flat 32-bit GDT; do not trust whatever the loader left behind.
    ;
    lgdt    [cs:Gdt32Descriptor]

    ;
    ; Reload CS with a far jump to our 32-bit flat code selector (0x08).
    ;
    jmp     0x08:FlatMode32

FlatMode32:
    mov     eax, 0x10                   ; flat 32-bit data selector
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ;
    ; Temporary stack lives just below the page tables at the top of temp RAM.
    ;
    mov     esp, PT_BASE
    and     esp, ~0xF

    ;----------------------------------------------------------------------
    ; Build identity-mapped page tables for the low 4 GiB.
    ;----------------------------------------------------------------------

    ; Zero the whole page-table region first.
    mov     edi, PT_BASE
    mov     ecx, PT_TOTAL_SIZE / 4
    xor     eax, eax
    rep     stosd

    ; PML4[0] -> PDPT
    mov     edi, PML4_BASE
    mov     eax, PDPT_BASE | PG_P | PG_RW
    mov     [edi], eax
    ; upper dword already zero

    ; PDPT[0..3] -> PD0..PD3
    mov     edi, PDPT_BASE
    mov     eax, PD_BASE | PG_P | PG_RW
    mov     ecx, 4
.fill_pdpt:
    mov     [edi], eax
    ; upper dword already zero
    add     eax, 0x1000                 ; next PD
    add     edi, 8
    loop    .fill_pdpt

    ; PD0..PD3: 2048 entries, each a 2 MiB identity leaf.
    ; Entry value = (index << 21) | flags.
    mov     edi, PD_BASE
    mov     eax, PG_P | PG_RW | PG_PS   ; phys base 0, leaf flags
    mov     edx, 0                      ; upper dword of physical address
    mov     ecx, 2048
.fill_pd:
    mov     [edi], eax                  ; low dword: phys | flags
    mov     [edi + 4], edx              ; high dword: phys[63:32]
    add     eax, 0x200000               ; +2 MiB
    adc     edx, 0                      ; carry into high dword at 4 GiB
    add     edi, 8
    loop    .fill_pd

    ;----------------------------------------------------------------------
    ; Enter long mode.
    ;----------------------------------------------------------------------

    ; CR3 = PML4
    mov     eax, PML4_BASE
    mov     cr3, eax

    ; CR4.PAE = 1
    mov     eax, cr4
    or      eax, 0x20                   ; BIT5 PAE
    mov     cr4, eax

    ; EFER.LME = 1
    mov     ecx, 0xC0000080             ; IA32_EFER
    rdmsr
    or      eax, 0x100                  ; BIT8 LME
    wrmsr

    ; CR0.PG = 1 (paging on) -> compatibility mode
    mov     eax, cr0
    or      eax, 0x80000000             ; BIT31 PG
    mov     cr0, eax

    ; Load a GDT that has a 64-bit code descriptor and far-jump into it.
    lgdt    [cs:Gdt64Descriptor]
    jmp     0x08:LongMode64

BITS 64

LongMode64:
    mov     ax, 0x10                    ; flat 64-bit data selector
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ;
    ; Enable SSE/SSE2. The X64 EDK2 toolchain generates SSE code and, critically,
    ; BaseLib SetJump/LongJump (used by SecTemporaryRamSupport) save/restore
    ; XMM6-XMM15 with movdqu/stmxcsr. Executing those before SSE is enabled
    ; raises #UD/#NM, which - with no exception handler this early - looks like a
    ; dead hang right at the stack switch. Clear CR0.EM, set CR0.MP, and set
    ; CR4.OSFXSR | CR4.OSXMMEXCPT.
    ;
    mov     rax, cr0
    and     ax, ~0x0004                 ; clear CR0.EM (BIT2)
    or      ax, 0x0002                  ; set   CR0.MP (BIT1)
    mov     cr0, rax

    mov     rax, cr4
    or      ax, 0x0600                  ; set CR4.OSFXSR (BIT9) | OSXMMEXCPT (BIT10)
    mov     cr4, rax

    ;
    ; Temporary stack at the top of temporary RAM, below the page tables.
    ;
    mov     rsp, PT_BASE
    and     rsp, ~0xF

    ;
    ; Clear the lower temporary RAM so PEI starts from a known state.
    ; (Do not clobber the page tables we just built at the top.)
    ;
    mov     rdi, TEMP_RAM_BASE
    mov     ecx, (TEMP_RAM_SIZE - PT_TOTAL_SIZE) / 8
    xor     rax, rax
    rep     stosq

    ;
    ; SecStartup (SizeOfRam, TempRamBase, BootFirmwareVolume)
    ;   RCX, RDX, R8 per the MS x64 ABI used by edk2.
    ;
    mov     rcx, TEMP_RAM_SIZE - PT_TOTAL_SIZE
    mov     rdx, TEMP_RAM_BASE
    mov     r8,  [rel gBootFirmwareVolumeBase]

    ; Reserve 32 bytes of shadow space required by the MS x64 ABI.
    sub     rsp, 0x20
    call    ASM_PFX(SecStartup)

    ;
    ; SecStartup never returns.
    ;
DeadLoop:
    hlt
    jmp     DeadLoop

;------------------------------------------------------------------------------
; Flat 32-bit GDT (used before the long-mode switch)
;------------------------------------------------------------------------------
align 16
Gdt32Base:
    ; 0x00 - null
    dq  0
    ; 0x08 - flat 32-bit code, base 0, limit 4G, ring 0
    dw  0xFFFF
    dw  0x0000
    db  0x00
    db  0x9B
    db  0xCF
    db  0x00
    ; 0x10 - flat 32-bit data, base 0, limit 4G, ring 0
    dw  0xFFFF
    dw  0x0000
    db  0x00
    db  0x93
    db  0xCF
    db  0x00
Gdt32End:

align 16
Gdt32Descriptor:
    dw  Gdt32End - Gdt32Base - 1
    dd  Gdt32Base

;------------------------------------------------------------------------------
; 64-bit GDT (long mode)
;   The L bit (bit 21 of the code descriptor's flags byte) selects 64-bit mode.
;------------------------------------------------------------------------------
align 16
Gdt64Base:
    ; 0x00 - null
    dq  0
    ; 0x08 - 64-bit code, L=1, D=0, ring 0
    dw  0x0000
    dw  0x0000
    db  0x00
    db  0x9A                            ; P=1 DPL=0 S=1 Type=code exec/read
    db  0xA0                            ; G=1 L=1 (0xA0 = 1010_0000b)
    db  0x00
    ; 0x10 - 64-bit data, ring 0
    dw  0x0000
    dw  0x0000
    db  0x00
    db  0x92                            ; P=1 DPL=0 S=1 Type=data read/write
    db  0xA0
    db  0x00
Gdt64End:

align 16
Gdt64Descriptor:
    dw  Gdt64End - Gdt64Base - 1
    dq  Gdt64Base

;
; Base of the FV that carries the PEI Core. The boot firmware volume
; immediately follows the SEC region inside the same image.
;
align 8
global ASM_PFX(gBootFirmwareVolumeBase)
ASM_PFX(gBootFirmwareVolumeBase):
    dq  FD_BASE + 0x10000
