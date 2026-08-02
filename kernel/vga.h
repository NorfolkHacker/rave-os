#ifndef RAVEOS_VGA_H
#define RAVEOS_VGA_H

void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_set_color(unsigned char fg, unsigned char bg);

#endif
