/* Rave-OS's first real filesystem: a superblock, one fixed-size flat file
 * table (no directories yet -- that's its own later stage), and files
 * allocated as contiguous runs of sectors by a simple bump allocator. All
 * of it built on top of kernel/ata.c's read/write-one-sector primitives --
 * this file never touches an I/O port directly. */

#include "fs.h"
#include "ata.h"
#include <stdint.h>

#define FS_MAGIC 0x45564152u /* on-disk bytes read as ASCII "RAVE" */
#define FS_VERSION 1

#define FS_MAX_FILES 20
#define FS_NAME_MAX 16 /* includes the terminating nul, so 15 usable chars */

#define FS_SUPERBLOCK_LBA 0
#define FS_FILETABLE_LBA 1
#define FS_DATA_START_LBA 2

/* Must match boot/Makefile's `dd ... count=1` (1MiB) for fs.img -- this
 * driver has no IDENTIFY-command drive-size probe (Stage A didn't need
 * one), so the disk's real size is just a fact both sides have to agree
 * on, same as the boot sector layout constants boot/Makefile computes for
 * disk.img. */
#define FS_TOTAL_SECTORS 2048u

/* The very last sector stays permanently reserved for ata_selftest()'s
 * scratch write (kernel/ata.c) -- kept out of the allocator's reach so a
 * real file can never be silently corrupted by that diagnostic. */
#define FS_RESERVED_TAIL_SECTORS 1u

struct fs_superblock {
    uint32_t magic;
    uint32_t version;
    uint32_t total_sectors;
    uint32_t next_free_lba;
} __attribute__((packed));

struct fs_file_entry {
    char name[FS_NAME_MAX]; /* name[0] == 0 marks a free slot */
    uint32_t start_lba;
    uint32_t size_bytes;
} __attribute__((packed));

static struct fs_superblock sb;
static int mounted = 0;

static int str_eq(const char *a, const char *b) {
    int i;
    for (i = 0; i < FS_NAME_MAX; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == 0) {
            return 1;
        }
    }
    return 1;
}

static void name_copy(char *dst, const char *src) {
    int i;
    for (i = 0; i < FS_NAME_MAX - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    for (; i < FS_NAME_MAX; i++) {
        dst[i] = 0;
    }
}

static void sb_write(void) {
    unsigned char buf[ATA_SECTOR_SIZE];
    int i;
    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = 0;
    }
    *(struct fs_superblock *)buf = sb;
    ata_write_sector(ATA_DRIVE_SLAVE, FS_SUPERBLOCK_LBA, buf);
}

static void ft_read(struct fs_file_entry *entries) {
    unsigned char buf[ATA_SECTOR_SIZE];
    struct fs_file_entry *on_disk = (struct fs_file_entry *)buf;
    int i;
    ata_read_sector(ATA_DRIVE_SLAVE, FS_FILETABLE_LBA, buf);
    for (i = 0; i < FS_MAX_FILES; i++) {
        entries[i] = on_disk[i];
    }
}

static void ft_write(const struct fs_file_entry *entries) {
    unsigned char buf[ATA_SECTOR_SIZE];
    struct fs_file_entry *on_disk = (struct fs_file_entry *)buf;
    int i;
    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = 0;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        on_disk[i] = entries[i];
    }
    ata_write_sector(ATA_DRIVE_SLAVE, FS_FILETABLE_LBA, buf);
}

static int ft_find(const struct fs_file_entry *entries, const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entries[i].name[0] != 0 && str_eq(entries[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int ft_find_free(const struct fs_file_entry *entries) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entries[i].name[0] == 0) {
            return i;
        }
    }
    return -1;
}

void fs_init(void) {
    unsigned char buf[ATA_SECTOR_SIZE];

    ata_read_sector(ATA_DRIVE_SLAVE, FS_SUPERBLOCK_LBA, buf);
    sb = *(struct fs_superblock *)buf;

    if (sb.magic != FS_MAGIC) {
        unsigned char ft_buf[ATA_SECTOR_SIZE];
        int i;

        sb.magic = FS_MAGIC;
        sb.version = FS_VERSION;
        sb.total_sectors = FS_TOTAL_SECTORS;
        sb.next_free_lba = FS_DATA_START_LBA;
        sb_write();

        for (i = 0; i < (int)sizeof(ft_buf); i++) {
            ft_buf[i] = 0;
        }
        ata_write_sector(ATA_DRIVE_SLAVE, FS_FILETABLE_LBA, ft_buf);
    }

    mounted = 1;
}

int fs_create_file(const char *name, const void *data, unsigned int size) {
    struct fs_file_entry entries[FS_MAX_FILES];
    const unsigned char *src = (const unsigned char *)data;
    unsigned char buf[ATA_SECTOR_SIZE];
    unsigned int sectors_needed;
    unsigned int lba;
    unsigned int remaining;
    unsigned int s;
    int slot;

    if (!mounted) {
        fs_init();
    }

    ft_read(entries);
    if (ft_find(entries, name) >= 0) {
        return -1; /* write-once: already exists */
    }
    slot = ft_find_free(entries);
    if (slot < 0) {
        return -1; /* file table full */
    }

    sectors_needed = (size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (sectors_needed == 0) {
        sectors_needed = 1; /* even a zero-byte file still owns one sector */
    }
    if (sb.next_free_lba + sectors_needed > FS_TOTAL_SECTORS - FS_RESERVED_TAIL_SECTORS) {
        return -1; /* would run into the reserved tail sector */
    }

    lba = sb.next_free_lba;
    remaining = size;
    for (s = 0; s < sectors_needed; s++) {
        unsigned int chunk = remaining < ATA_SECTOR_SIZE ? remaining : ATA_SECTOR_SIZE;
        unsigned int i;
        for (i = 0; i < ATA_SECTOR_SIZE; i++) {
            buf[i] = 0;
        }
        for (i = 0; i < chunk; i++) {
            buf[i] = src[s * ATA_SECTOR_SIZE + i];
        }
        if (ata_write_sector(ATA_DRIVE_SLAVE, lba + s, buf) != 0) {
            return -1;
        }
        remaining -= chunk;
    }

    name_copy(entries[slot].name, name);
    entries[slot].start_lba = lba;
    entries[slot].size_bytes = size;
    ft_write(entries);

    sb.next_free_lba = lba + sectors_needed;
    sb_write();

    return 0;
}

int fs_read_file(const char *name, void *buf, unsigned int buf_size, unsigned int *out_size) {
    struct fs_file_entry entries[FS_MAX_FILES];
    unsigned char *dst = (unsigned char *)buf;
    unsigned char sector_buf[ATA_SECTOR_SIZE];
    unsigned int sectors;
    unsigned int s;
    int slot;

    if (!mounted) {
        fs_init();
    }

    ft_read(entries);
    slot = ft_find(entries, name);
    if (slot < 0) {
        return -1;
    }
    if (entries[slot].size_bytes > buf_size) {
        return -1;
    }

    sectors = (entries[slot].size_bytes + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (sectors == 0) {
        sectors = 1;
    }

    for (s = 0; s < sectors; s++) {
        unsigned int chunk;
        unsigned int i;
        if (ata_read_sector(ATA_DRIVE_SLAVE, entries[slot].start_lba + s, sector_buf) != 0) {
            return -1;
        }
        chunk = entries[slot].size_bytes - s * ATA_SECTOR_SIZE;
        if (chunk > ATA_SECTOR_SIZE) {
            chunk = ATA_SECTOR_SIZE;
        }
        for (i = 0; i < chunk; i++) {
            dst[s * ATA_SECTOR_SIZE + i] = sector_buf[i];
        }
    }

    *out_size = entries[slot].size_bytes;
    return 0;
}

#define FS_SELFTEST_NAME "SELFTEST.TXT"
static const char fs_selftest_content[] = "RAVE-OS FS OK";

static int content_matches(const unsigned char *buf, unsigned int out_size) {
    unsigned int i;
    if (out_size != sizeof(fs_selftest_content) - 1) {
        return 0;
    }
    for (i = 0; i < out_size; i++) {
        if (buf[i] != (unsigned char)fs_selftest_content[i]) {
            return 0;
        }
    }
    return 1;
}

const char *fs_selftest(void) {
    unsigned char buf[64];
    unsigned int out_size;

    fs_init();

    /* Found on this boot -- every boot after the first, once the test
     * file already exists -- means comparing it here *is* the proof a
     * named file survived a full reboot, not just a raw sector. */
    if (fs_read_file(FS_SELFTEST_NAME, buf, sizeof(buf), &out_size) == 0) {
        return content_matches(buf, out_size) ? "FS: OK" : "FS: MISMATCH";
    }

    if (fs_create_file(FS_SELFTEST_NAME, fs_selftest_content, sizeof(fs_selftest_content) - 1) != 0) {
        return "FS: WRITE FAIL";
    }
    if (fs_read_file(FS_SELFTEST_NAME, buf, sizeof(buf), &out_size) != 0) {
        return "FS: READ FAIL";
    }
    return content_matches(buf, out_size) ? "FS: OK" : "FS: MISMATCH";
}
