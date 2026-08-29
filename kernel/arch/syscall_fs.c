#include "syscall.h"
#include "fs.h"

int syscall_dispatch(int num, int arg) {
    if (num == SYS_READ_FILE) {
        const struct sys_read_file_args *a = (const struct sys_read_file_args *)arg;
        return fs_read_file(a->path, a->buf, a->buf_size, a->out_size);
    }
    if (num == SYS_CREATE_FILE) {
        const struct sys_create_file_args *a = (const struct sys_create_file_args *)arg;
        return fs_create_file(a->path, a->data, a->size);
    }
    if (num == SYS_LIST_DIR) {
        const struct sys_list_dir_args *a = (const struct sys_list_dir_args *)arg;
        return fs_list_dir(a->path, a->out, a->max_entries, a->out_count);
    }
    if (num == SYS_DELETE) {
        return fs_delete((const char *)arg);
    }
    if (num == SYS_CREATE_DIR) {
        return fs_create_dir((const char *)arg);
    }
    return syscall_dispatch_core(num, arg);
}
