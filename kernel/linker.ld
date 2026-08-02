ENTRY(_start)

SECTIONS
{
    /* Must match KERNEL_LOAD_ADDR in boot/stage2.asm -- that's the physical
       address stage2 far-jumps to after enabling protected mode. */
    . = 0x10000;

    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }

    /* __bss_start/__bss_end mark the range kernel_entry.asm must zero
       before calling kmain -- objcopy -O binary drops .bss entirely from
       the flat kernel.bin (confirmed empirically; see docs/BUILD_LOG.md),
       so nothing else zeroes this memory. Before this, the kernel worked
       only because unwritten RAM happened to read as zero -- true for the
       small amount of .bss used so far, but not something to keep relying
       on as it grows. */
    __bss_start = .;
    .bss : { *(.bss) *(COMMON) }
    __bss_end = .;

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
