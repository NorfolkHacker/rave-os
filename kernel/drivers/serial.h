#ifndef RAVEOS_SERIAL_H
#define RAVEOS_SERIAL_H

/* Temporary debug-only serial (COM1) logger -- for tracing the real-hardware
 * PAINT left-click bug (docs/IDEAS.md). Not part of the OS's normal design;
 * remove once the bug is root-caused and fixed. */

void serial_init(void);
void serial_write_str(const char *s);
void serial_write_int(int v);

#endif
