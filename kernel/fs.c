/* Rave-OS's filesystem: a superblock, and a tree of fixed-size directory
 * tables (root plus subdirectories, all the same shape) whose entries are
 * either files or more directories. Files and directory tables alike are
 * allocated as contiguous runs of sectors by a simple bump allocator. All
 * of it built on top of kernel/ata.c's read/write-one-sector primitives --
 * this file never touches an I/O port directly. */

#include "fs.h"
#include "ata.h"
#include <stdint.h>

#define FS_MAGIC 0x45564152u /* on-disk bytes read as ASCII "RAVE" */
#define FS_VERSION 2 /* bumped from 1: entries gained a type field (file/dir) */

/* FS_MAX_FILES/FS_NAME_MAX moved to fs.h -- fs_list_dir() made them a
 * public contract (callers need to know them to size a buffer), so a
 * private copy here would just be a second definition to keep in sync. */
#define FS_MAX_PATH_DEPTH 4

#define FS_SUPERBLOCK_LBA 0
#define FS_ROOT_LBA 1 /* root directory's own entry table -- same shape as any other directory's */
#define FS_DATA_START_LBA 2

/* Must match boot/Makefile's `dd ... count=1` (1MiB) for fs.img -- this
 * driver has no IDENTIFY-command drive-size probe (Stage A didn't need
 * one), so the disk's real size is just a fact both sides have to agree
 * on, same as the boot sector layout constants boot/Makefile computes for
 * disk.img. */
#define FS_TOTAL_SECTORS 2048u

/* The very last sector stays permanently reserved for ata_selftest()'s
 * scratch write (kernel/ata.c) -- kept out of the allocator's reach so a
 * real file or directory table can never be silently corrupted by that
 * diagnostic. */
#define FS_RESERVED_TAIL_SECTORS 1u

struct fs_superblock {
    uint32_t magic;
    uint32_t version;
    uint32_t total_sectors;
    uint32_t next_free_lba;
} __attribute__((packed));

struct fs_entry {
    char name[FS_NAME_MAX]; /* name[0] == 0 marks a free slot */
    uint32_t start_lba;     /* file data, or another directory's own table sector */
    uint32_t size_bytes;    /* unused (0) for a directory entry */
    uint8_t type;           /* FS_TYPE_FILE or FS_TYPE_DIR */
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

/* Reads/writes one directory's entry table, at whichever LBA that
 * particular directory (root or a subdirectory) happens to live at --
 * every directory is the same shape, so this one pair of functions serves
 * all of them. */
static void dirtable_read(unsigned int lba, struct fs_entry *entries) {
    unsigned char buf[ATA_SECTOR_SIZE];
    struct fs_entry *on_disk = (struct fs_entry *)buf;
    int i;
    ata_read_sector(ATA_DRIVE_SLAVE, lba, buf);
    for (i = 0; i < FS_MAX_FILES; i++) {
        entries[i] = on_disk[i];
    }
}

static void dirtable_write(unsigned int lba, const struct fs_entry *entries) {
    unsigned char buf[ATA_SECTOR_SIZE];
    struct fs_entry *on_disk = (struct fs_entry *)buf;
    int i;
    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = 0;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        on_disk[i] = entries[i];
    }
    ata_write_sector(ATA_DRIVE_SLAVE, lba, buf);
}

static int dirtable_find(const struct fs_entry *entries, const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entries[i].name[0] != 0 && str_eq(entries[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int dirtable_find_free(const struct fs_entry *entries) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entries[i].name[0] == 0) {
            return i;
        }
    }
    return -1;
}

/* Splits an absolute path ("/a/b/c") into up to FS_MAX_PATH_DEPTH
 * components. Returns the component count, or -1 if the path doesn't
 * start with '/', a component is empty (e.g. "//" or a trailing '/') or
 * longer than FS_NAME_MAX - 1 characters, or there are more than
 * FS_MAX_PATH_DEPTH components. No strtok() -- this kernel is
 * freestanding/libc-less, same reasoning as forth.c's own tokenizer. */
static int path_split(const char *path, char components[][FS_NAME_MAX]) {
    int count = 0;
    int i;

    if (path[0] != '/') {
        return -1;
    }
    i = 1;

    while (path[i]) {
        int j = 0;
        if (count >= FS_MAX_PATH_DEPTH) {
            return -1;
        }
        while (path[i] && path[i] != '/') {
            if (j >= FS_NAME_MAX - 1) {
                return -1;
            }
            components[count][j] = path[i];
            j++;
            i++;
        }
        if (j == 0) {
            return -1; /* "//" or a trailing '/' */
        }
        components[count][j] = 0;
        count++;
        if (path[i] == '/') {
            i++;
        }
    }

    if (count == 0) {
        return -1; /* just "/" -- nothing to operate on */
    }
    return count;
}

/* Walks the first n components as directories, starting from the root
 * table and following each one's start_lba into the next table, updating
 * *table_lba as it goes. -1 if any component along the way is missing or
 * isn't a directory. Shared by walk_to_parent() (n = count - 1, stopping
 * short of the final component) and resolve_dir_lba() (n = count, since
 * every component of a directory path is itself a directory). */
static int walk_components(char components[][FS_NAME_MAX], int n, unsigned int *table_lba) {
    int i;
    for (i = 0; i < n; i++) {
        struct fs_entry entries[FS_MAX_FILES];
        int slot;
        dirtable_read(*table_lba, entries);
        slot = dirtable_find(entries, components[i]);
        if (slot < 0 || entries[slot].type != FS_TYPE_DIR) {
            return -1;
        }
        *table_lba = entries[slot].start_lba;
    }
    return 0;
}

/* Walks all but the last component of path. Hands back the LBA of the
 * table that should hold the final component (*out_table_lba) and that
 * component's own name (leaf_name). -1 if any component along the way is
 * missing or isn't a directory. */
static int walk_to_parent(const char *path, char *leaf_name, unsigned int *out_table_lba) {
    char components[FS_MAX_PATH_DEPTH][FS_NAME_MAX];
    unsigned int table_lba = FS_ROOT_LBA;
    int count = path_split(path, components);

    if (count < 0) {
        return -1;
    }
    if (walk_components(components, count - 1, &table_lba) != 0) {
        return -1;
    }

    name_copy(leaf_name, components[count - 1]);
    *out_table_lba = table_lba;
    return 0;
}

/* Resolves a directory path (including "/" for root, which has no parent
 * to look itself up in -- a fixed special case) to that directory's own
 * entry-table LBA. -1 if any component is missing or isn't a directory. */
static int resolve_dir_lba(const char *path, unsigned int *out_table_lba) {
    char components[FS_MAX_PATH_DEPTH][FS_NAME_MAX];
    unsigned int table_lba = FS_ROOT_LBA;
    int count;

    if (path[0] == '/' && path[1] == 0) {
        *out_table_lba = FS_ROOT_LBA;
        return 0;
    }

    count = path_split(path, components);
    if (count < 0) {
        return -1;
    }
    if (walk_components(components, count, &table_lba) != 0) {
        return -1;
    }

    *out_table_lba = table_lba;
    return 0;
}

void fs_init(void) {
    unsigned char buf[ATA_SECTOR_SIZE];

    ata_read_sector(ATA_DRIVE_SLAVE, FS_SUPERBLOCK_LBA, buf);
    sb = *(struct fs_superblock *)buf;

    if (sb.magic != FS_MAGIC || sb.version != FS_VERSION) {
        unsigned char root_buf[ATA_SECTOR_SIZE];
        int i;

        sb.magic = FS_MAGIC;
        sb.version = FS_VERSION;
        sb.total_sectors = FS_TOTAL_SECTORS;
        sb.next_free_lba = FS_DATA_START_LBA;
        sb_write();

        for (i = 0; i < (int)sizeof(root_buf); i++) {
            root_buf[i] = 0;
        }
        ata_write_sector(ATA_DRIVE_SLAVE, FS_ROOT_LBA, root_buf);
    }

    mounted = 1;
}

/* Allocates one fresh sector from the bump allocator, past the reserved
 * tail boundary check shared by both files and directory tables. Returns
 * the LBA, or 0xFFFFFFFF (never a valid LBA -- sector 0 is the
 * superblock) on failure. */
static unsigned int alloc_sectors(unsigned int count) {
    unsigned int lba;
    if (sb.next_free_lba + count > FS_TOTAL_SECTORS - FS_RESERVED_TAIL_SECTORS) {
        return 0xFFFFFFFFu;
    }
    lba = sb.next_free_lba;
    sb.next_free_lba = lba + count;
    sb_write();
    return lba;
}

int fs_create_dir(const char *path) {
    struct fs_entry entries[FS_MAX_FILES];
    char leaf[FS_NAME_MAX];
    unsigned char zero_buf[ATA_SECTOR_SIZE];
    unsigned int table_lba;
    unsigned int new_lba;
    int slot;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    dirtable_read(table_lba, entries);
    if (dirtable_find(entries, leaf) >= 0) {
        return -1; /* already exists */
    }
    slot = dirtable_find_free(entries);
    if (slot < 0) {
        return -1; /* parent's table full */
    }

    new_lba = alloc_sectors(1);
    if (new_lba == 0xFFFFFFFFu) {
        return -1;
    }

    for (i = 0; i < (int)sizeof(zero_buf); i++) {
        zero_buf[i] = 0;
    }
    ata_write_sector(ATA_DRIVE_SLAVE, new_lba, zero_buf);

    name_copy(entries[slot].name, leaf);
    entries[slot].start_lba = new_lba;
    entries[slot].size_bytes = 0;
    entries[slot].type = FS_TYPE_DIR;
    dirtable_write(table_lba, entries);

    return 0;
}

int fs_create_file(const char *path, const void *data, unsigned int size) {
    struct fs_entry entries[FS_MAX_FILES];
    const unsigned char *src = (const unsigned char *)data;
    unsigned char buf[ATA_SECTOR_SIZE];
    char leaf[FS_NAME_MAX];
    unsigned int table_lba;
    unsigned int sectors_needed;
    unsigned int lba;
    unsigned int remaining;
    unsigned int s;
    int slot;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    dirtable_read(table_lba, entries);
    if (dirtable_find(entries, leaf) >= 0) {
        return -1; /* write-once: already exists */
    }
    slot = dirtable_find_free(entries);
    if (slot < 0) {
        return -1; /* parent's table full */
    }

    sectors_needed = (size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (sectors_needed == 0) {
        sectors_needed = 1; /* even a zero-byte file still owns one sector */
    }
    lba = alloc_sectors(sectors_needed);
    if (lba == 0xFFFFFFFFu) {
        return -1;
    }

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

    name_copy(entries[slot].name, leaf);
    entries[slot].start_lba = lba;
    entries[slot].size_bytes = size;
    entries[slot].type = FS_TYPE_FILE;
    dirtable_write(table_lba, entries);

    return 0;
}

int fs_read_file(const char *path, void *buf, unsigned int buf_size, unsigned int *out_size) {
    struct fs_entry entries[FS_MAX_FILES];
    unsigned char *dst = (unsigned char *)buf;
    unsigned char sector_buf[ATA_SECTOR_SIZE];
    char leaf[FS_NAME_MAX];
    unsigned int table_lba;
    unsigned int sectors;
    unsigned int s;
    int slot;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    dirtable_read(table_lba, entries);
    slot = dirtable_find(entries, leaf);
    if (slot < 0 || entries[slot].type != FS_TYPE_FILE) {
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

int fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count) {
    struct fs_entry entries[FS_MAX_FILES];
    unsigned int table_lba;
    unsigned int count = 0;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (resolve_dir_lba(path, &table_lba) != 0) {
        return -1;
    }

    dirtable_read(table_lba, entries);
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entries[i].name[0] == 0) {
            continue;
        }
        if (count >= max_entries) {
            break; /* truncate rather than overflow the caller's buffer */
        }
        name_copy(out[count].name, entries[i].name);
        out[count].type = entries[i].type;
        out[count].size_bytes = entries[i].size_bytes;
        count++;
    }

    *out_count = count;
    return 0;
}

#define FS_SELFTEST_DIR "/TESTDIR"
#define FS_SELFTEST_PATH "/TESTDIR/NESTED.TXT"
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

    /* Found on this boot -- every boot after the first, once the nested
     * test file already exists -- means comparing it here *is* the proof
     * a real directory and the file inside it both survived a full
     * reboot, not just a raw sector. */
    if (fs_read_file(FS_SELFTEST_PATH, buf, sizeof(buf), &out_size) == 0) {
        return content_matches(buf, out_size) ? "FS: OK" : "FS: MISMATCH";
    }

    if (fs_create_dir(FS_SELFTEST_DIR) != 0) {
        return "FS: MKDIR FAIL";
    }
    if (fs_create_file(FS_SELFTEST_PATH, fs_selftest_content, sizeof(fs_selftest_content) - 1) != 0) {
        return "FS: WRITE FAIL";
    }
    if (fs_read_file(FS_SELFTEST_PATH, buf, sizeof(buf), &out_size) != 0) {
        return "FS: READ FAIL";
    }
    return content_matches(buf, out_size) ? "FS: OK" : "FS: MISMATCH";
}
