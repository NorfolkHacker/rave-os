#ifndef RAVEOS_FS_H
#define RAVEOS_FS_H

/* Public contract for anyone sizing a buffer for fs_list_dir(): the most
 * entries any one directory's table can hold, and the max length
 * (including the terminating nul) of one entry's name. */
#define FS_MAX_FILES 20
#define FS_NAME_MAX 16

#define FS_TYPE_FILE 0
#define FS_TYPE_DIR 1

struct fs_dirent {
    char name[FS_NAME_MAX];
    int type; /* FS_TYPE_FILE or FS_TYPE_DIR */
    unsigned int size_bytes; /* unused (0) for a directory */
};

/* Mounts the filesystem on the ATA slave drive (kernel/ata.h), formatting
 * it fresh if the superblock's magic/version don't match -- so a blank (or
 * old-format) fs.img just works, no separate host-side mkfs tool needed.
 * Safe to call more than once (re-reads the superblock each time). */
void fs_init(void);

/* Paths are absolute and '/'-separated, e.g. "/TESTDIR/NESTED.TXT" -- no
 * relative paths or a current-directory concept yet. Each component is
 * capped at 15 characters, and a path may have at most FS_MAX_PATH_DEPTH
 * (4) components (see fs.c). */

/* Creates a directory at path. The parent must already exist and be a
 * directory; fails (-1) if the path itself already exists, the parent's
 * table is full, or there isn't room left before the reserved tail
 * sector (see fs.c). Returns 0 on success. */
int fs_create_dir(const char *path);

/* Write-once: fails (-1) if a file at this path already exists, if the
 * parent directory doesn't exist, if the parent's table is full, or if
 * there isn't enough room left before the reserved tail sector. No
 * delete/overwrite yet -- deferred to a later stage. Returns 0 on
 * success. */
int fs_create_file(const char *path, const void *data, unsigned int size);

/* Returns 0 and fills *out_size if found and it fits in buf_size, -1 if
 * not found, not a file (e.g. path names a directory), or too big for the
 * caller's buffer. */
int fs_read_file(const char *path, void *buf, unsigned int buf_size, unsigned int *out_size);

/* Lists path's direct entries (not recursive) into out[], up to
 * max_entries (callers should size their buffer to FS_MAX_FILES to never
 * truncate). path must name a directory -- "/" for root. Returns 0 and
 * sets *out_count on success, -1 if path doesn't exist or isn't a
 * directory. */
int fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count);

/* Mounts (formatting if needed), then proves it end-to-end against a
 * fixed nested path: reads it first -- if it's already there (every boot
 * after the first), comparing its content *is* the cross-reboot
 * persistence proof for both the directory and the file inside it; if not
 * (first boot on a fresh/reformatted image), creates the directory and
 * file and reads it back. Returns a static, human-readable status string
 * ("FS: OK", ...) meant to be shown directly in the UI, same convention
 * as ata_selftest(). */
const char *fs_selftest(void);

#endif
