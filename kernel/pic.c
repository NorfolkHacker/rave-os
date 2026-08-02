/* Driver for the two 8259 PICs (Programmable Interrupt Controllers) that
 * route hardware IRQ lines to the CPU -- IRQ1 (keyboard) and IRQ12
 * (mouse, on the second/"slave" PIC, cascaded through the first PIC's
 * IRQ2 line) are the ones this kernel currently cares about. */

#include "pic.h"
#include "io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI 0x20 /* "end of interrupt" command */

#define ICW1_ICW4 0x01 /* ICW4 (below) will be sent */
#define ICW1_INIT 0x10 /* begin PIC initialization sequence */
#define ICW4_8086 0x01 /* operate in 8086/88 mode, not obsolete 8080 mode */

/* Writing to port 0x80 (an unused POST-diagnostic port) is a standard
 * trick for burning a few microseconds -- real 8259 hardware needs a
 * short delay between initialization command words. QEMU's emulated PIC
 * doesn't strictly need it, but it's harmless and correct on real
 * hardware too. */
static void io_wait(void) {
    outb(0x80, 0);
}

void pic_remap(void) {
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    outb(PIC1_DATA, 4); /* tell the master PIC a slave sits on its IRQ2 line */
    io_wait();
    outb(PIC2_DATA, 2); /* tell the slave PIC its own cascade identity */
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, 0xFF); /* mask every IRQ line; callers unmask what they handle */
    outb(PIC2_DATA, 0xFF);
}

void pic_set_mask(int irq) {
    unsigned short port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    unsigned char bit = (unsigned char)((irq < 8) ? irq : irq - 8);
    outb(port, (unsigned char)(inb(port) | (1 << bit)));
}

void pic_clear_mask(int irq) {
    unsigned short port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    unsigned char bit = (unsigned char)((irq < 8) ? irq : irq - 8);
    outb(port, (unsigned char)(inb(port) & (unsigned char)~(1 << bit)));
}

void pic_send_eoi_master(void) {
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_send_eoi_slave(void) {
    /* Slave-originated IRQs need EOI sent to both PICs -- the master
     * doesn't know the slave already handled its own line. */
    outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}
