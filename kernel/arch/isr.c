/* Interrupt handlers, and the wiring that connects them to the IDT/PIC.
 *
 * Handlers use GCC's __attribute__((interrupt)) instead of hand-written
 * assembly stubs: the compiler generates the register save/restore and
 * `iret` a real interrupt handler needs, given a function with the right
 * shape (a struct interrupt_frame * parameter, plus a leading
 * unsigned int error_code parameter for the handful of CPU exceptions
 * that push one). The one thing this can't do is tell a handler which
 * vector invoked it -- the CPU doesn't pass that in, only the address of
 * whichever function the IDT entry points to runs. That's why the
 * generic catch-all below can't report which of the many exceptions
 * sharing it actually fired; a real per-vector stub (in asm) that pushes
 * its own vector number would be needed for that. Four of the most likely
 * exceptions to actually hit during development get named, distinct
 * handlers instead (see panic() below) so at least those show up as more
 * than a silent freeze. */

#include "idt.h"
#include "pic.h"
#include "io.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mouse.h"
#include "sb16.h"
#include "graphics.h"
#include "text.h"

struct interrupt_frame;

/* Last resort for anything the kernel can't recover from: paint a red
 * banner with the given message and halt for good. Safe to call from an
 * interrupt handler even though it never returns -- the special part of
 * __attribute__((interrupt)) is only the return path (iret), which a
 * panic never takes. */
static void panic(const char *msg) {
    gfx_fill_rect(0, 0, gfx_width(), 30, 0xCC0000);
    text_puts(10, 8, msg, 0xFFFFFF, 2);
    gfx_present();
    for (;;) {
        __asm__ volatile("cli\n\thlt");
    }
}

__attribute__((interrupt)) static void isr_exception_no_err(struct interrupt_frame *frame) {
    (void)frame;
    panic("PANIC: UNHANDLED CPU EXCEPTION");
}

__attribute__((interrupt)) static void isr_exception_err(struct interrupt_frame *frame, unsigned int error_code) {
    (void)frame;
    (void)error_code;
    panic("PANIC: UNHANDLED CPU EXCEPTION");
}

__attribute__((interrupt)) static void isr_divide_error(struct interrupt_frame *frame) {
    (void)frame;
    panic("PANIC: DIVIDE ERROR");
}

__attribute__((interrupt)) static void isr_invalid_opcode(struct interrupt_frame *frame) {
    (void)frame;
    panic("PANIC: INVALID OPCODE");
}

__attribute__((interrupt)) static void isr_general_protection(struct interrupt_frame *frame, unsigned int error_code) {
    (void)frame;
    (void)error_code;
    panic("PANIC: GENERAL PROTECTION FAULT");
}

__attribute__((interrupt)) static void isr_page_fault(struct interrupt_frame *frame, unsigned int error_code) {
    (void)frame;
    (void)error_code;
    panic("PANIC: PAGE FAULT");
}

__attribute__((interrupt)) static void irq_master_default(struct interrupt_frame *frame) {
    (void)frame;
    pic_send_eoi_master();
}

__attribute__((interrupt)) static void irq_slave_default(struct interrupt_frame *frame) {
    (void)frame;
    pic_send_eoi_slave();
}

/* 8259 PICs latch an IRQ line's edge into their Interrupt Request
 * Register even while that line is masked -- so a byte polled directly
 * (bypassing the handler, as mouse_init()'s handshake does while IRQ12 is
 * still masked) can leave a stale pending request that fires the instant
 * the line is unmasked, well after the byte it corresponded to was
 * already consumed. Checking the status port's "output buffer full" bit
 * before trusting inb(0x60) turns that spurious fire into a harmless
 * no-op instead of re-reading (or reading garbage) into the buffer.
 * Found by tracing exactly this: a mouse packet decoding to nonsense
 * turned out to be offset by one stale 0xFA (ACK) byte. */
#define PS2_STATUS_PORT 0x64
#define PS2_STATUS_OUTPUT_FULL 0x01

__attribute__((interrupt)) static void irq1_keyboard(struct interrupt_frame *frame) {
    (void)frame;
    if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        keyboard_irq_push_scancode(inb(0x60));
    }
    pic_send_eoi_master();
}

__attribute__((interrupt)) static void irq12_mouse(struct interrupt_frame *frame) {
    (void)frame;
    if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        mouse_irq_push_byte(inb(0x60));
    }
    pic_send_eoi_slave();
}

__attribute__((interrupt)) static void irq5_sb16(struct interrupt_frame *frame) {
    (void)frame;
    sb16_irq_ack();
    pic_send_eoi_master();
}

/* Which of the 32 CPU exception vectors push a hardware error code onto
 * the stack before invoking the handler -- fixed by the x86 architecture
 * itself, not a software choice. Getting one of these wrong desyncs the
 * stack for that vector's handler. */
static int exception_has_error_code(int vector) {
    switch (vector) {
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 17:
        case 21:
        case 29:
        case 30:
            return 1;
        default:
            return 0;
    }
}

void interrupts_init(void) {
    int vector;

    pic_remap();

    for (vector = 0; vector < 32; vector++) {
        void *handler = exception_has_error_code(vector) ? (void *)isr_exception_err : (void *)isr_exception_no_err;
        idt_set_gate(vector, handler, IDT_TYPE_INTERRUPT_GATE_32);
    }
    /* Named handlers for the exceptions most likely to actually fire
     * during kernel development, overriding the generic catch-all above
     * for just these four vectors. */
    idt_set_gate(0, (void *)isr_divide_error, IDT_TYPE_INTERRUPT_GATE_32);
    idt_set_gate(6, (void *)isr_invalid_opcode, IDT_TYPE_INTERRUPT_GATE_32);
    idt_set_gate(13, (void *)isr_general_protection, IDT_TYPE_INTERRUPT_GATE_32);
    idt_set_gate(14, (void *)isr_page_fault, IDT_TYPE_INTERRUPT_GATE_32);

    for (vector = 0; vector < 16; vector++) {
        void *handler;
        if (vector == 1) {
            handler = (void *)irq1_keyboard;
        } else if (vector == 5) {
            handler = (void *)irq5_sb16;
        } else if (vector == 12) {
            handler = (void *)irq12_mouse;
        } else if (vector < 8) {
            handler = (void *)irq_master_default;
        } else {
            handler = (void *)irq_slave_default;
        }
        idt_set_gate(0x20 + vector, handler, IDT_TYPE_INTERRUPT_GATE_32);
    }

    idt_init();
}

void interrupts_enable(void) {
    pic_clear_mask(1);  /* keyboard */
    pic_clear_mask(2);  /* cascade line -- required for IRQ8-15 to reach the CPU */
    pic_clear_mask(5);  /* sb16 */
    pic_clear_mask(12); /* mouse */
    __asm__ volatile("sti");
}
