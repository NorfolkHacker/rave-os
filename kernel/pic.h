#ifndef RAVEOS_PIC_H
#define RAVEOS_PIC_H

#define PIC1_OFFSET 0x20 /* IRQ0-7  -> interrupt vectors 0x20-0x27 */
#define PIC2_OFFSET 0x28 /* IRQ8-15 -> interrupt vectors 0x28-0x2F */

/* Reprograms both 8259 PICs to deliver IRQ0-15 at PIC1_OFFSET/PIC2_OFFSET
 * instead of their power-on default (0x08-0x0F, which collides with CPU
 * exception vectors), then masks every IRQ line. Callers unmask
 * individually via pic_clear_mask() once they have a handler ready. */
void pic_remap(void);

void pic_set_mask(int irq);
void pic_clear_mask(int irq);

void pic_send_eoi_master(void);
void pic_send_eoi_slave(void);

#endif
