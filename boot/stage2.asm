; Rave-OS stage-2 loader.
; Loaded by stage1 at 0x0000:0x8000, still in 16-bit real mode.
; Job: enable the A20 line, build a flat GDT, switch to 32-bit protected mode,
; and prove it worked by writing straight to the VGA text buffer (BIOS
; teletype calls no longer work once we're out of real mode).
BITS 16
ORG 0x8000

start:
    mov si, msg_stage2
    call print_string

    call enable_a20

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1              ; set CR0.PE (Protection Enable)
    mov cr0, eax

    jmp CODE_SEG:protected_mode_entry   ; far jump: flushes the prefetch queue
                                         ; and loads CS with our 32-bit code selector

; si -> null-terminated string; prints via BIOS teletype (still real mode here)
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp print_string
.done:
    ret

; Fast A20 gate: bit 1 of I/O port 0x92 (System Control Port A) enables the
; A20 address line when set. This is the simplest of several historical
; methods (others go through the keyboard controller or BIOS int 0x15) and
; is supported by QEMU.
enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; --- Global Descriptor Table: flat model, one code + one data segment,
; both base 0 / limit 4GB, so segmentation is effectively a no-op and all
; addressing is just linear addresses (paging isn't set up yet either, so
; those are also physical addresses for now).
gdt_start:
    dq 0x0000000000000000      ; entry 0 must be a null descriptor

gdt_code:
    dw 0xFFFF       ; limit bits 0-15
    dw 0x0000       ; base bits 0-15
    db 0x00         ; base bits 16-23
    db 10011010b    ; access: present, ring 0, code segment, executable, readable
    db 11001111b    ; flags: 4KB granularity, 32-bit segment | limit bits 16-19 = 0xF
    db 0x00         ; base bits 24-31

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b    ; access: present, ring 0, data segment, writable
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; GDT size in bytes, minus 1
    dd gdt_start                  ; linear address of the GDT (ORG makes this absolute)

CODE_SEG equ gdt_code - gdt_start   ; selector 0x08
DATA_SEG equ gdt_data - gdt_start   ; selector 0x10

BITS 32
protected_mode_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000        ; stack in low memory, well clear of our own code

    mov esi, msg_pmode
    mov edi, 0xB8000         ; VGA text-mode framebuffer, physical address
.print32:
    lodsb
    or al, al
    jz .hang
    mov [edi], al
    mov byte [edi + 1], 0x0F   ; attribute byte: white on black
    add edi, 2
    jmp .print32
.hang:
    hlt
    jmp .hang

msg_stage2 db 'Rave-OS: stage2 loaded, entering protected mode...', 13, 10, 0
msg_pmode  db 'Rave-OS: 32-bit protected mode OK.', 0

times 1024 - ($ - $$) db 0   ; pad to exactly STAGE2_SECTORS * 512 bytes
