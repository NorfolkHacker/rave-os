#ifndef RAVEOS_HEXFMT_H
#define RAVEOS_HEXFMT_H

#include <stdint.h>

/* Writes exactly 8 uppercase hex digits (no "0x" prefix, no sign) plus
 * a null terminator into out[0..8] -- out must point to at least 9
 * bytes. No snprintf in this freestanding kernel; this is the first
 * number-to-text conversion this kernel has needed (confirmed: no
 * itoa/snprintf/hex-to-string helper exists anywhere in kernel/
 * today). */
void hex32_to_str(uint32_t v, char *out9);

#endif
