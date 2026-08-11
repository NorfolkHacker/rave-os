#ifndef RAVEOS_FS_H
#define RAVEOS_FS_H

/* Mounts the filesystem on the ATA slave drive (kernel/ata.h), formatting
 * it fresh if the superblock's magic number doesn't match -- so a blank
 * fs.img just works, no separate host-side mkfs tool needed. Safe to call
 * more than once (re-reads the superblock each time). */
void fs_init(void);

/* Write-once: fails (-1) if a file by this name already exists, if the
 * file table is full, or if there isn't enough room left before the
 * reserved tail sector (see fs.c). No delete/overwrite yet -- deferred to
 * a later stage. Returns 0 on success. */
int fs_create_file(const char *name, const void *data, unsigned int size);

/* Returns 0 and fills *out_size if found and it fits in buf_size, -1 if
 * not found or too big for the caller's buffer. */
int fs_read_file(const char *name, void *buf, unsigned int buf_size, unsigned int *out_size);

/* Mounts (formatting if needed), then proves it end-to-end: reads a fixed
 * test filename first -- if it's already there (every boot after the
 * first), comparing its content *is* the cross-reboot persistence proof;
 * if not (first boot on a fresh image), creates it and reads it back.
 * Returns a static, human-readable status string ("FS: OK", ...) meant to
 * be shown directly in the UI, same convention as ata_selftest(). */
const char *fs_selftest(void);

#endif
