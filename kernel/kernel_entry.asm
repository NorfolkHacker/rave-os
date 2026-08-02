; Tiny asm stub that becomes the very first bytes of the kernel binary.
; stage2 far-jumps straight to this physical address after switching to
; protected mode, so this has to be real, valid 32-bit code at offset 0 --
; a bare C file has no guaranteed entry point/address without this.
BITS 32
section .text
global _start
extern kmain

_start:
    call kmain
.hang:
    hlt
    jmp .hang
