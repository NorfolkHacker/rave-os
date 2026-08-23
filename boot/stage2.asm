; Rave-OS stage-2 loader.
; Loaded by stage1 at 0x0000:0x8000, still in 16-bit real mode.
; Job: load the C kernel off disk, switch the display into a VESA (VBE)
; linear-framebuffer graphics mode, enable the A20 line, build a flat GDT,
; switch to 32-bit protected mode, and hand off control to the kernel.
BITS 16
ORG 0x8000

; Disk layout (CHS sector numbers, 1-indexed): sector 1 = stage1, then
; STAGE2_SECTORS sectors of stage2, then the kernel starting at
; KERNEL_START_SECTOR for KERNEL_SECTORS sectors. All three are normally
; passed in by boot/Makefile via `nasm -D`, computed from the actual
; measured sizes of stage2.bin and kernel.bin -- this used to be
; hand-copied constants kept in sync across this file, stage1.asm, and
; kernel/Makefile by hand (a real, repeatedly-hit source of bugs; see
; docs/BUILD_LOG.md's consolidation entry). The fallbacks below only
; matter if this file is ever assembled directly, outside the Makefile.
%ifndef KERNEL_START_SECTOR
KERNEL_START_SECTOR equ 4
%endif
%ifndef KERNEL_SECTORS
KERNEL_SECTORS equ 20
%endif
KERNEL_SEGMENT   equ 0x1000   ; 0x1000:0x0000 = physical 0x10000
KERNEL_LOAD_ADDR equ 0x10000
SEGMENT_CHUNK_SECTORS equ 128 ; 128 * 512 = 65536 = exactly one 64KB real-mode segment

; VBE mode 0x112 = 640x480, 32 bits/pixel, linear framebuffer. Bit 14
; (0x4000) of the mode number tells VBE function 4F02h to use the linear
; framebuffer addressing model instead of legacy bank-switched addressing.
VBE_MODE       equ 0x112
VBE_MODE_LFB   equ VBE_MODE | 0x4000

; Scratch buffer VBE fills in with a 256-byte ModeInfoBlock, and the small
; struct we distill out of it for the kernel -- both addresses are free
; real-mode memory below the kernel's 0x10000 load point and above stage2
; itself (which ends at 0x8000+1024=0x8400).
VBE_INFO_ADDR  equ 0x9000
BOOT_INFO_ADDR equ 0x9500   ; must match BOOT_INFO_ADDR in ../kernel/boot_info.h

start:
    mov [boot_drive], dl   ; stage1 leaves the BIOS boot-drive number in dl; save it again here

    mov si, msg_stage2
    call print_string

    ; Load the kernel across as many 64KB real-mode segments as it takes.
    ; Uses INT 13h AH=42h ("extended read", LBA addressing via a Disk
    ; Address Packet) rather than AH=02h's legacy CHS addressing: CHS's
    ; sector field is only 6 bits (max 63), and a real/emulated BIOS
    ; refuses a single CHS read that would cross a track boundary --
    ; harmless while the kernel was small enough that
    ; KERNEL_START_SECTOR + KERNEL_SECTORS stayed under ~63, but this
    ; kernel's on-disk footprint (no relation to RAM/.bss size -- see
    ; kernel/Makefile's own comment on that) grew past that with this
    ; project's accumulated feature set, and CHS has no way to express a
    ; multi-track transfer in one call at all (unlike the ES:BX
    ; wraparound below, chunking alone can't fix a hard 63-sector
    ; ceiling on the chunk size itself). AH=42h addresses purely by
    ; linear sector number instead, sidestepping the whole CHS geometry
    ; question -- universally supported by SeaBIOS (and every real BIOS
    ; since the mid-1990s) for hard-disk boot drives.
    ; A single INT 13h read call still only advances the 16-bit BX/DAP
    ; offset as it fills the buffer -- it never carries into ES -- so one
    ; call asking for more than SEGMENT_CHUNK_SECTORS (64KB / 512 = 128,
    ; one full segment) would silently wrap the offset back to 0 partway
    ; through and overwrite the start of the buffer instead of extending
    ; it. Splitting into chunks that each start at offset 0 of their own
    ; segment (ES bumped by 0x1000 = 64KB between chunks) keeps every
    ; individual read inside one segment no matter how large
    ; KERNEL_SECTORS grows as the kernel itself grows.
    mov ax, KERNEL_SEGMENT
    mov es, ax
    mov eax, KERNEL_START_SECTOR - 1   ; DAP's LBA is 0-indexed; disk sector numbers here are 1-indexed
    mov [dap_lba_lo], eax
    mov word [sectors_left], KERNEL_SECTORS

.load_chunk:
    mov ax, [sectors_left]
    or ax, ax
    jz .load_done
    cmp ax, SEGMENT_CHUNK_SECTORS
    jbe .chunk_size_ok
    mov ax, SEGMENT_CHUNK_SECTORS
.chunk_size_ok:
    mov [this_chunk], ax

    mov byte [dap_size], 0x10
    mov byte [dap_reserved], 0
    mov ax, [this_chunk]
    mov [dap_count], ax
    mov word [dap_offset], 0    ; buffer offset, always 0 -- start of this chunk's segment
    mov ax, es
    mov [dap_segment], ax
    mov dword [dap_lba_hi], 0

    mov ah, 0x42               ; BIOS function: extended read (LBA addressing)
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc disk_error

    mov ax, [sectors_left]
    sub ax, [this_chunk]
    mov [sectors_left], ax

    movzx eax, word [this_chunk]
    add [dap_lba_lo], eax       ; next chunk's starting LBA

    mov ax, es
    add ax, 0x1000               ; next chunk's segment, 64KB further up
    mov es, ax

    jmp .load_chunk
.load_done:

    xor ax, ax
    mov es, ax                 ; restore ES=0 now that the disk read is done

    mov si, msg_video
    call print_string
    call setup_video

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

; Query the VBE ModeInfoBlock for VBE_MODE, pull out the fields the kernel
; needs (framebuffer physical address, scanline pitch, resolution, bit
; depth) into a compact boot_info struct, then actually switch the display
; into that mode.
setup_video:
    mov ax, 0x4F01          ; VBE function 01h: get mode info
    mov cx, VBE_MODE
    mov di, VBE_INFO_ADDR
    int 0x10
    cmp ax, 0x004F           ; AL=4Fh means "function supported", AH=0 means "success"
    jne vbe_error

    mov eax, [VBE_INFO_ADDR + 0x28]   ; PhysBasePtr: linear framebuffer physical address
    mov [BOOT_INFO_ADDR + 0], eax
    mov ax, [VBE_INFO_ADDR + 0x10]     ; BytesPerScanLine
    mov [BOOT_INFO_ADDR + 4], ax
    mov ax, [VBE_INFO_ADDR + 0x12]     ; XResolution
    mov [BOOT_INFO_ADDR + 6], ax
    mov ax, [VBE_INFO_ADDR + 0x14]     ; YResolution
    mov [BOOT_INFO_ADDR + 8], ax
    mov al, [VBE_INFO_ADDR + 0x19]     ; BitsPerPixel
    mov [BOOT_INFO_ADDR + 10], al

    mov ax, 0x4F02           ; VBE function 02h: set mode
    mov bx, VBE_MODE_LFB
    int 0x10
    cmp ax, 0x004F
    jne vbe_error
    ret

vbe_error:
    mov si, msg_vbe_error
    call print_string
.hang16:
    cli
    hlt
    jmp .hang16

boot_drive db 0
sectors_left dw 0   ; sectors still to load, decremented as load_chunk consumes them
this_chunk   dw 0    ; size of the chunk load_chunk is currently reading/just read

; INT 13h AH=42h's "Disk Address Packet" -- the fixed 16-byte structure
; that call reads its arguments from (SI must point here), rather than
; register arguments the way AH=02h's CHS read used. Fields refilled
; before every .load_chunk iteration; dap_lba_hi always stays 0 (this
; kernel is always well under the 4-billion-sector reach of the low
; 32 bits alone).
dap:
dap_size     db 0   ; packet size, always 0x10
dap_reserved db 0   ; always 0
dap_count    dw 0   ; sectors to transfer this call
dap_offset   dw 0   ; destination buffer offset
dap_segment  dw 0   ; destination buffer segment
dap_lba_lo   dd 0   ; starting LBA (0-indexed), low 32 bits
dap_lba_hi   dd 0   ; starting LBA, high 32 bits -- always 0

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
msg_video      db 'Rave-OS: setting 640x480x32 video mode...', 13, 10, 0
msg_disk_error db 'Disk read error!', 0
msg_vbe_error  db 'VBE video mode not supported!', 0

times 1024 - ($ - $$) db 0   ; pad to exactly STAGE2_SECTORS * 512 bytes
