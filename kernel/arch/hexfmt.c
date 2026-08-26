#include "hexfmt.h"

void hex32_to_str(uint32_t v, char *out9) {
    static const char digits[] = "0123456789ABCDEF";
    int i;
    for (i = 7; i >= 0; i--) {
        out9[i] = digits[v & 0xF];
        v >>= 4;
    }
    out9[8] = '\0';
}
