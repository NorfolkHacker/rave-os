#include "syscall.h"

int syscall_dispatch(int num, int arg) {
    (void)arg;
    switch (num) {
        case SYS_TEST:
            return 0x1234;
        case SYS_EXIT:
            return 0;
        default:
            return -1;
    }
}
