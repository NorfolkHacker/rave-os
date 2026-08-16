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

#define FS_DEV_NAME "DEV" /* the bare (not "/DEV" path) reserved name -- kept in one place so the write-guards and fs_list_dir()'s synthesis can't drift apart */

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

/* Caps its comparison at FS_NAME_MAX (16) bytes and reports equal if
 * both strings match up to that point with no terminator seen yet --
 * correct for the FS_NAME_MAX-bounded entry names this was written for,
 * but a silent prefix-equality trap if ever used to compare two longer
 * strings (e.g. full paths) that happen to share their first 16 bytes.
 * Every current caller passes a short literal ("/DEV", "/", or an
 * entry name already known to fit) that terminates the loop well before
 * byte 16, so this is a latent risk, not a live bug. */
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
 * all of them. Both return 0 on success, -1 if the underlying ATA
 * operation failed -- callers must check this rather than assume it
 * always succeeds: on a read failure, buf (and thus entries[]) would
 * otherwise be whatever was already on the stack, misread as real
 * on-disk file/directory records; on a write failure, the caller would
 * otherwise believe a change was persisted when it wasn't. */
static int dirtable_read(unsigned int lba, struct fs_entry *entries) {
    unsigned char buf[ATA_SECTOR_SIZE];
    struct fs_entry *on_disk = (struct fs_entry *)buf;
    int i;
    if (ata_read_sector(ATA_DRIVE_SLAVE, lba, buf) != 0) {
        return -1;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        entries[i] = on_disk[i];
    }
    return 0;
}

static int dirtable_write(unsigned int lba, const struct fs_entry *entries) {
    unsigned char buf[ATA_SECTOR_SIZE];
    struct fs_entry *on_disk = (struct fs_entry *)buf;
    int i;
    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = 0;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        on_disk[i] = entries[i];
    }
    return ata_write_sector(ATA_DRIVE_SLAVE, lba, buf);
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

/* True only if [lba, lba+span) is entirely within the allocator's valid
 * data range -- never the superblock (LBA 0), the root table (LBA 1), or
 * anything at/past the reserved tail. Every start_lba this file uses as a
 * sector address comes straight off disk (a directory entry, or the
 * superblock's next_free_lba) with no other guarantee it wasn't
 * corrupted; callers check this before dereferencing one, instead of
 * trusting it to have been written by this same code. Written to avoid
 * overflow: the `lba >= limit` check runs before `limit - lba`, so that
 * subtraction can never wrap. */
static int lba_span_valid(unsigned int lba, unsigned int span) {
    unsigned int limit = FS_TOTAL_SECTORS - FS_RESERVED_TAIL_SECTORS;
    if (lba < FS_DATA_START_LBA || lba >= limit) {
        return 0;
    }
    return span <= limit - lba;
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
        if (dirtable_read(*table_lba, entries) != 0) {
            return -1;
        }
        slot = dirtable_find(entries, components[i]);
        if (slot < 0 || entries[slot].type != FS_TYPE_DIR || !lba_span_valid(entries[slot].start_lba, 1)) {
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
    int read_ok = ata_read_sector(ATA_DRIVE_SLAVE, FS_SUPERBLOCK_LBA, buf) == 0;

    if (read_ok) {
        sb = *(struct fs_superblock *)buf;
    }

    /* Reformat whenever the superblock can't be trusted: the read itself
     * failed (buf is whatever was on the stack), the magic/version don't
     * match, total_sectors doesn't match what this build actually uses,
     * or next_free_lba is out of the allocator's valid range (e.g. bit
     * rot, or a previous run that crashed mid-write). Every field the
     * rest of this file trusts unconditionally is validated right here,
     * once, rather than by every caller that touches sb later. */
    if (!read_ok || sb.magic != FS_MAGIC || sb.version != FS_VERSION || sb.total_sectors != FS_TOTAL_SECTORS ||
        sb.next_free_lba < FS_DATA_START_LBA || sb.next_free_lba > FS_TOTAL_SECTORS - FS_RESERVED_TAIL_SECTORS) {
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

/* Allocates count fresh sectors from the bump allocator. Returns the
 * starting LBA, or 0xFFFFFFFF (never a valid LBA -- sector 0 is the
 * superblock) on failure. Delegates the bounds check to lba_span_valid()
 * so a huge count (e.g. from an overflowed sectors_needed computation
 * upstream) can't wrap sb.next_free_lba + count back into range the way
 * a direct `next_free_lba + count > limit` comparison could. */
static unsigned int alloc_sectors(unsigned int count) {
    unsigned int lba;
    if (!lba_span_valid(sb.next_free_lba, count)) {
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

    if (table_lba == FS_ROOT_LBA && str_eq(leaf, FS_DEV_NAME)) {
        return -1; /* reserved: /DEV is synthetic (see fs_list_dir()), never a real directory */
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
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
    if (ata_write_sector(ATA_DRIVE_SLAVE, new_lba, zero_buf) != 0) {
        return -1;
    }

    name_copy(entries[slot].name, leaf);
    entries[slot].start_lba = new_lba;
    entries[slot].size_bytes = 0;
    entries[slot].type = FS_TYPE_DIR;
    return dirtable_write(table_lba, entries);
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

    if (table_lba == FS_ROOT_LBA && str_eq(leaf, FS_DEV_NAME)) {
        return -1; /* reserved: /DEV is synthetic (see fs_list_dir()), never a real directory */
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    if (dirtable_find(entries, leaf) >= 0) {
        return -1; /* write-once: already exists */
    }
    slot = dirtable_find_free(entries);
    if (slot < 0) {
        return -1; /* parent's table full */
    }

    /* Deliberately not `(size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE` --
     * that addition overflows uint32_t for size close to UINT32_MAX,
     * wrapping sectors_needed down to a tiny number while size_bytes
     * below still records the full (huge) size, desyncing the directory
     * entry's metadata from the sectors actually allocated. This form
     * can't overflow: division and modulo never wrap. */
    sectors_needed = size / ATA_SECTOR_SIZE + (size % ATA_SECTOR_SIZE != 0 ? 1 : 0);
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
    return dirtable_write(table_lba, entries);
}

/* Appends data to an existing file, or creates it (via fs_create_file())
 * if it doesn't exist yet -- a logger shouldn't need two separate code
 * paths for "first write" vs. "later writes". Every other write in this
 * file is write-once; this is the first genuine append, and the bump
 * allocator (no free list, no reclaim) shapes what it can safely do:
 * spare space in the file's own already-allocated last sector is always
 * safe to fill in place, but growing onto MORE sectors is only safe
 * when this file's data is still the very last thing the allocator has
 * handed out (start_lba + old_sectors == sb.next_free_lba) -- otherwise
 * something else already occupies the sectors right after it, and
 * there's no relocate-and-copy support here. Refuses (-1) rather than
 * attempt that, the same "real gap, not solved before it has to be"
 * reasoning fs_delete()'s never-reclaims-sectors limitation already
 * uses. If a failure happens after the tail sector was already written
 * but before size_bytes is updated below, those extra bytes are
 * harmlessly orphaned (physically on disk, but past what size_bytes
 * claims, so fs_read_file() never sees them) rather than corrupting
 * anything previously readable. */
int fs_append_file(const char *path, const void *data, unsigned int size) {
    struct fs_entry entries[FS_MAX_FILES];
    const unsigned char *src = (const unsigned char *)data;
    unsigned char buf[ATA_SECTOR_SIZE];
    char leaf[FS_NAME_MAX];
    unsigned int table_lba;
    int slot;
    unsigned int old_size, new_size;
    unsigned int old_sectors, last_sector_offset, tail_space, into_tail;
    unsigned int remaining, extra_sectors, extra_lba;
    unsigned int s, i;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    slot = dirtable_find(entries, leaf);
    if (slot < 0) {
        return fs_create_file(path, data, size);
    }
    if (entries[slot].type != FS_TYPE_FILE) {
        return -1;
    }

    if (size == 0) {
        return 0;
    }

    old_size = entries[slot].size_bytes;
    new_size = old_size + size;
    if (new_size < old_size) {
        return -1; /* overflow */
    }

    old_sectors = old_size / ATA_SECTOR_SIZE + (old_size % ATA_SECTOR_SIZE != 0 ? 1 : 0);
    if (old_sectors == 0) {
        old_sectors = 1; /* even a zero-byte file owns one sector, matching fs_create_file() */
    }
    if (!lba_span_valid(entries[slot].start_lba, old_sectors)) {
        return -1;
    }
    last_sector_offset = old_size - (old_sectors - 1) * ATA_SECTOR_SIZE;
    tail_space = ATA_SECTOR_SIZE - last_sector_offset;

    into_tail = size < tail_space ? size : tail_space;
    if (into_tail > 0) {
        if (ata_read_sector(ATA_DRIVE_SLAVE, entries[slot].start_lba + old_sectors - 1, buf) != 0) {
            return -1;
        }
        for (i = 0; i < into_tail; i++) {
            buf[last_sector_offset + i] = src[i];
        }
        if (ata_write_sector(ATA_DRIVE_SLAVE, entries[slot].start_lba + old_sectors - 1, buf) != 0) {
            return -1;
        }
    }

    remaining = size - into_tail;
    if (remaining > 0) {
        if (entries[slot].start_lba + old_sectors != sb.next_free_lba) {
            return -1; /* something else follows this file on disk -- can't extend safely */
        }
        extra_sectors = remaining / ATA_SECTOR_SIZE + (remaining % ATA_SECTOR_SIZE != 0 ? 1 : 0);
        extra_lba = alloc_sectors(extra_sectors);
        if (extra_lba == 0xFFFFFFFFu) {
            return -1;
        }
        for (s = 0; s < extra_sectors; s++) {
            unsigned int chunk = remaining - s * ATA_SECTOR_SIZE;
            if (chunk > ATA_SECTOR_SIZE) {
                chunk = ATA_SECTOR_SIZE;
            }
            for (i = 0; i < ATA_SECTOR_SIZE; i++) {
                buf[i] = 0;
            }
            for (i = 0; i < chunk; i++) {
                buf[i] = src[into_tail + s * ATA_SECTOR_SIZE + i];
            }
            if (ata_write_sector(ATA_DRIVE_SLAVE, extra_lba + s, buf) != 0) {
                return -1;
            }
        }
    }

    entries[slot].size_bytes = new_size;
    return dirtable_write(table_lba, entries);
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

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    slot = dirtable_find(entries, leaf);
    if (slot < 0 || entries[slot].type != FS_TYPE_FILE) {
        return -1;
    }
    if (entries[slot].size_bytes > buf_size) {
        return -1;
    }

    sectors = entries[slot].size_bytes / ATA_SECTOR_SIZE + (entries[slot].size_bytes % ATA_SECTOR_SIZE != 0 ? 1 : 0);
    if (sectors == 0) {
        sectors = 1;
    }

    if (!lba_span_valid(entries[slot].start_lba, sectors)) {
        return -1;
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

int fs_delete(const char *path) {
    struct fs_entry entries[FS_MAX_FILES];
    char leaf[FS_NAME_MAX];
    unsigned int table_lba;
    int slot;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    slot = dirtable_find(entries, leaf);
    if (slot < 0) {
        return -1; /* not found */
    }

    if (entries[slot].type == FS_TYPE_DIR) {
        struct fs_entry sub_entries[FS_MAX_FILES];
        int j;
        if (!lba_span_valid(entries[slot].start_lba, 1)) {
            return -1;
        }
        if (dirtable_read(entries[slot].start_lba, sub_entries) != 0) {
            return -1;
        }
        for (j = 0; j < FS_MAX_FILES; j++) {
            if (sub_entries[j].name[0] != 0) {
                return -1; /* refuse: not empty, no recursive delete */
            }
        }
    }

    for (i = 0; i < FS_NAME_MAX; i++) {
        entries[slot].name[i] = 0;
    }
    entries[slot].start_lba = 0;
    entries[slot].size_bytes = 0;
    entries[slot].type = 0;
    return dirtable_write(table_lba, entries);
}

int fs_rename(const char *path, const char *new_name) {
    struct fs_entry entries[FS_MAX_FILES];
    char leaf[FS_NAME_MAX];
    unsigned int table_lba;
    int slot;
    int existing;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    slot = dirtable_find(entries, leaf);
    if (slot < 0) {
        return -1; /* not found */
    }

    if (new_name[0] == 0) {
        return -1;
    }
    for (i = 0; new_name[i]; i++) {
        if (new_name[i] == '/' || i >= FS_NAME_MAX - 1) {
            return -1;
        }
    }

    if (table_lba == FS_ROOT_LBA && str_eq(new_name, FS_DEV_NAME)) {
        return -1; /* reserved: renaming something to shadow synthetic /DEV */
    }

    existing = dirtable_find(entries, new_name);
    if (existing >= 0 && existing != slot) {
        return -1; /* taken by a different entry */
    }

    name_copy(entries[slot].name, new_name);
    return dirtable_write(table_lba, entries);
}

int fs_move(const char *path, const char *dest_dir) {
    struct fs_entry src_entries[FS_MAX_FILES];
    struct fs_entry dest_entries[FS_MAX_FILES];
    struct fs_entry moved;
    char leaf[FS_NAME_MAX];
    unsigned int src_table_lba;
    unsigned int dest_table_lba;
    int src_slot;
    int dest_slot;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &src_table_lba) != 0) {
        return -1;
    }
    if (dirtable_read(src_table_lba, src_entries) != 0) {
        return -1;
    }
    src_slot = dirtable_find(src_entries, leaf);
    if (src_slot < 0 || src_entries[src_slot].type != FS_TYPE_FILE) {
        return -1; /* not found, or a directory (out of scope for v1) */
    }

    if (resolve_dir_lba(dest_dir, &dest_table_lba) != 0) {
        return -1;
    }
    if (dirtable_read(dest_table_lba, dest_entries) != 0) {
        return -1;
    }
    if (dirtable_find(dest_entries, leaf) >= 0) {
        return -1; /* name already taken at destination (self-move included) */
    }
    dest_slot = dirtable_find_free(dest_entries);
    if (dest_slot < 0) {
        return -1; /* destination table full */
    }

    moved = src_entries[src_slot];
    dest_entries[dest_slot] = moved;
    if (dirtable_write(dest_table_lba, dest_entries) != 0) {
        return -1;
    }

    for (i = 0; i < FS_NAME_MAX; i++) {
        src_entries[src_slot].name[i] = 0;
    }
    src_entries[src_slot].start_lba = 0;
    src_entries[src_slot].size_bytes = 0;
    src_entries[src_slot].type = 0;
    return dirtable_write(src_table_lba, src_entries);
}

int fs_copy_file(const char *path, const char *dest_dir) {
    struct fs_entry src_entries[FS_MAX_FILES];
    struct fs_entry dest_entries[FS_MAX_FILES];
    unsigned char sector_buf[ATA_SECTOR_SIZE];
    char leaf[FS_NAME_MAX];
    unsigned int src_table_lba;
    unsigned int dest_table_lba;
    unsigned int sectors;
    unsigned int new_lba;
    unsigned int s;
    int src_slot;
    int dest_slot;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &src_table_lba) != 0) {
        return -1;
    }
    if (dirtable_read(src_table_lba, src_entries) != 0) {
        return -1;
    }
    src_slot = dirtable_find(src_entries, leaf);
    if (src_slot < 0 || src_entries[src_slot].type != FS_TYPE_FILE) {
        return -1; /* not found, or a directory (out of scope for v1) */
    }

    if (resolve_dir_lba(dest_dir, &dest_table_lba) != 0) {
        return -1;
    }
    if (dirtable_read(dest_table_lba, dest_entries) != 0) {
        return -1;
    }
    if (dirtable_find(dest_entries, leaf) >= 0) {
        return -1; /* name already taken at destination */
    }
    dest_slot = dirtable_find_free(dest_entries);
    if (dest_slot < 0) {
        return -1; /* destination table full */
    }

    sectors = src_entries[src_slot].size_bytes / ATA_SECTOR_SIZE +
              (src_entries[src_slot].size_bytes % ATA_SECTOR_SIZE != 0 ? 1 : 0);
    if (sectors == 0) {
        sectors = 1;
    }
    if (!lba_span_valid(src_entries[src_slot].start_lba, sectors)) {
        return -1;
    }

    new_lba = alloc_sectors(sectors);
    if (new_lba == 0xFFFFFFFFu) {
        return -1;
    }

    for (s = 0; s < sectors; s++) {
        if (ata_read_sector(ATA_DRIVE_SLAVE, src_entries[src_slot].start_lba + s, sector_buf) != 0) {
            return -1;
        }
        if (ata_write_sector(ATA_DRIVE_SLAVE, new_lba + s, sector_buf) != 0) {
            return -1;
        }
    }

    name_copy(dest_entries[dest_slot].name, leaf);
    dest_entries[dest_slot].start_lba = new_lba;
    dest_entries[dest_slot].size_bytes = src_entries[src_slot].size_bytes;
    dest_entries[dest_slot].type = FS_TYPE_FILE;
    return dirtable_write(dest_table_lba, dest_entries);
}

/* /DEV is synthetic -- never a real directory-table entry. HDA is always
 * listed (this kernel booted from the ATA master, so it's present by
 * construction whenever code is running to ask -- no probe needed). HDB
 * is listed only if the filesystem disk is actually readable right now,
 * reusing the exact live check fs_init() already trusts to mount,
 * instead of new ATA-protocol presence-detection code. */
static void fs_list_dev(struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count) {
    unsigned char probe[ATA_SECTOR_SIZE];
    unsigned int count = 0;

    if (count < max_entries) {
        name_copy(out[count].name, "HDA");
        out[count].type = FS_TYPE_FILE;
        out[count].size_bytes = 0;
        count++;
    }

    if (count < max_entries && ata_read_sector(ATA_DRIVE_SLAVE, FS_SUPERBLOCK_LBA, probe) == 0) {
        name_copy(out[count].name, "HDB");
        out[count].type = FS_TYPE_FILE;
        out[count].size_bytes = 0;
        count++;
    }

    *out_count = count;
}

int fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count) {
    struct fs_entry entries[FS_MAX_FILES];
    unsigned int table_lba;
    unsigned int count = 0;
    int is_root;
    int dev_shadowed = 0;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (str_eq(path, "/DEV")) {
        fs_list_dev(out, max_entries, out_count);
        return 0;
    }

    if (resolve_dir_lba(path, &table_lba) != 0) {
        return -1;
    }
    is_root = str_eq(path, "/");

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entries[i].name[0] == 0) {
            continue;
        }
        if (count >= max_entries) {
            break; /* truncate rather than overflow the caller's buffer */
        }
        if (is_root && str_eq(entries[i].name, FS_DEV_NAME)) {
            dev_shadowed = 1; /* a real entry already claims the name; don't double-list it */
        }
        name_copy(out[count].name, entries[i].name);
        out[count].type = entries[i].type;
        out[count].size_bytes = entries[i].size_bytes;
        count++;
    }

    if (is_root && !dev_shadowed && count < max_entries) {
        name_copy(out[count].name, FS_DEV_NAME);
        out[count].type = FS_TYPE_DIR;
        out[count].size_bytes = 0;
        count++;
    }

    *out_count = count;
    return 0;
}

/* Bounded append with a running cursor, same shape as kernel.c's own
 * str_append() -- duplicated here rather than shared (see this file's
 * "each file owns its small string primitives" convention). Truncates
 * rather than overflows dst if src would run past cap. */
static void path_append(char *dst, int *pos, int cap, const char *src) {
    if (cap <= 0) {
        return;
    }
    while (*src && *pos < cap - 1) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = 0;
}

void fs_path_join(char *dst, int cap, const char *cwd, const char *name) {
    int pos = 0;
    /* Deliberately not this file's own str_eq(): that comparison is
     * bounded to FS_NAME_MAX (16) and meant for directory-entry names,
     * not arbitrary-length paths -- comparing a long cwd against the
     * short literal "/" happens to be safe with it too (the short
     * operand's own nul always ends the loop first), but a direct
     * check here avoids needing that reasoning at all. */
    int cwd_is_root = (cwd[0] == '/' && cwd[1] == 0);
    if (!cwd_is_root) {
        path_append(dst, &pos, cap, cwd);
    }
    path_append(dst, &pos, cap, "/");
    path_append(dst, &pos, cap, name);
}

void fs_path_parent(char *cwd) {
    int i;
    int last_slash = -1;
    for (i = 0; cwd[i]; i++) {
        if (cwd[i] == '/') {
            last_slash = i;
        }
    }
    if (last_slash <= 0) {
        cwd[0] = '/';
        cwd[1] = 0;
    } else {
        cwd[last_slash] = 0;
    }
}

#define FS_NUM_STANDARD_DIRS 7
static const char *const fs_standard_dirs[FS_NUM_STANDARD_DIRS] = {
    "/BIN", "/ETC", "/HOME", "/USR", "/VAR", "/TMP", "/GAMES",
};

void fs_bootstrap_dirs(void) {
    int i;
    if (!mounted) {
        fs_init();
    }
    for (i = 0; i < FS_NUM_STANDARD_DIRS; i++) {
        fs_create_dir(fs_standard_dirs[i]); /* -1 ("already exists", or a full parent table) is fine to ignore here -- boot-time plumbing, not a user action */
    }
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
