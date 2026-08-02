; Rave-OS stage-2 loader.
; Loaded by stage1 at 0x0000:0x8000, still in 16-bit real mode.
; Job: load the C kernel off disk, enable the A20 line, build a flat GDT,
; switch to 32-bit protected mode, and hand off control to the kernel.
BITS 16
ORG 0x8000

; Disk layout (CHS sector numbers, 1-indexed): sector 1 = stage1,
; sectors 2-3 = stage2 (STAGE2_SECTORS=2 in stage1.asm), so the kernel
; starts at sector 4. KERNEL_SECTORS must match KERNEL_SECTORS in
; ../kernel/Makefile. Both of these are manually-synced constants for now --
; a rough edge to revisit once kernel size stops being an afterthought.
KERNEL_START_SECTOR equ 4
KERNEL_SECTORS       equ 8
KERNEL_SEGMENT       equ 0x1000   ; 0x1000:0x0000 = physical 0x10000
KERNEL_LOAD_ADDR     equ 0x10000

start:
    mov [boot_drive], dl   ; stage1 leaves the BIOS boot-drive number in dl; save it again here

    mov si, msg_stage2
    call print_string

    mov ax, KERNEL_SEGMENT
    mov es, ax
    mov ah, 0x02              ; BIOS function: read sectors (CHS addressing)
    mov al, KERNEL_SECTORS
    mov ch, 0                  ; cylinder 0
    mov cl, KERNEL_START_SECTOR
    mov dh, 0                  ; head 0
    mov dl, [boot_drive]
    xor bx, bx                 ; ES:BX destination, ES = KERNEL_SEGMENT
    int 0x13
    jc disk_error

    call enable_a20

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1              ; set CR0.PE (Protection Enable)
    mov cr0, eax

    jmp CODE_SEG:protected_mode_entry   ; far jump: flushes the prefetch queue
                                         ; and loads CS with our 32-bit code selector

disk_error:
    mov si, msg_disk_error
    call print_string
.hang16:
    cli
    hlt
    jmp .hang16

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

boot_drive db 0

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

    jmp KERNEL_LOAD_ADDR    ; hand off to the C kernel's _start

msg_stage2     db 'Rave-OS: stage2 loaded, loading kernel...', 13, 10, 0
msg_disk_error db 'Disk read error!', 0

times 1024 - ($ - $$) db 0   ; pad to exactly STAGE2_SECTORS * 512 bytes
