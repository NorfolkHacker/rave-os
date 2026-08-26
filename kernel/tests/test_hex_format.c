/* kernel/tests/test_hex_format.c -- host-side only, never linked into
 * kernel.bin. hex32_to_str() has no freestanding-only dependencies,
 * so it compiles and runs natively here -- same convention as
 * test_font.c/test_paging.c. */
#include <stdio.h>
#include <string.h>
#include "../arch/hexfmt.h"

static int failures = 0;

static void check_str(const char *got, const char *expected, const char *msg) {
    if (strcmp(got, expected) != 0) {
        printf("FAIL: %s -- got \"%s\", expected \"%s\"\n", msg, got, expected);
        failures++;
    }
}

int main(void) {
    char buf[9];

    hex32_to_str(0x00000000u, buf);
    check_str(buf, "00000000", "zero");

    hex32_to_str(0xFFFFFFFFu, buf);
    check_str(buf, "FFFFFFFF", "all-ones");

    hex32_to_str(0xFD000000u, buf);
    check_str(buf, "FD000000", "the real framebuffer address, for a concrete regression anchor");

    hex32_to_str(0x0000000Au, buf);
    check_str(buf, "0000000A", "single low nibble, leading zeros padded");

    hex32_to_str(0x12345678u, buf);
    check_str(buf, "12345678", "mixed digits");

    if (failures == 0) {
        printf("PASS: all hex_format tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
