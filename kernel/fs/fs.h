#ifndef RAVEOS_FS_H
#define RAVEOS_FS_H

/* Public contract for anyone sizing a buffer for fs_list_dir(): the most
 * entries any one directory's table can hold, and the max length
 * (including the terminating nul) of one entry's name. */
#define FS_MAX_FILES 20
#define FS_NAME_MAX 16

/* fs_list_dir()'s true worst case: FS_MAX_FILES real entries plus one
 * synthetic "DEV" row when listing root (see fs_list_dir()'s own
 * comment below) -- callers must size their listing buffer to this,
 * not FS_MAX_FILES, or the synthetic row can be silently dropped. */
#define FS_LIST_MAX (FS_MAX_FILES + 1)

/* Longest absolute path this filesystem's own path-walking logic can
 * ever produce or accept: 4 components at up to FS_NAME_MAX - 1 (15)
 * characters each, plus a separating '/' before each and a terminating
 * nul -- generous, not a tight bound. Callers building a path buffer
 * (e.g. via fs_path_join()) should size it to this. */
#define FS_PATH_MAX 80

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

/* Idempotent: creates /BIN /ETC /HOME /USR /VAR /TMP if they don't
 * already exist (fs_create_dir()'s "already exists" failure is treated
 * as success), the same shape fs_selftest() already relies on for
 * /TESTDIR. Safe -- expected -- to call every boot, not just on a fresh
 * format. Deliberately does NOT create /DEV -- that name is reserved
 * for fs_list_dir()'s synthetic listing, see its own comment below. */
void fs_bootstrap_dirs(void);

/* Paths are absolute and '/'-separated, e.g. "/TESTDIR/NESTED.TXT" -- no
 * relative paths or a current-directory concept yet. Each component is
 * capped at 15 characters, and a path may have at most FS_MAX_PATH_DEPTH
 * (4) components (see fs.c). */

/* Creates a directory at path. The parent must already exist and be a
 * directory; fails (-1) if the path itself already exists, the parent's
 * table is full, there isn't room left before the reserved tail sector
 * (see fs.c), or the path is exactly "/DEV" (reserved -- see
 * fs_list_dir()'s comment). Returns 0 on success. */
int fs_create_dir(const char *path);

/* Write-once: fails (-1) if a file at this path already exists, if the
 * parent directory doesn't exist, if the parent's table is full, if
 * there isn't enough room left before the reserved tail sector, or if
 * the path is exactly "/DEV" (reserved -- see fs_list_dir()'s comment).
 * No delete/overwrite yet -- deferred to a later stage. Returns 0 on
 * success. */
int fs_create_file(const char *path, const void *data, unsigned int size);

/* Appends data to an existing file, or creates it (same as
 * fs_create_file()) if it doesn't exist yet. Spare space in the file's
 * own last allocated sector is always used first; growing beyond that
 * only succeeds if this file's data is still the very last thing
 * allocated on disk (nothing else has been created since) -- otherwise
 * fails (-1), since there's no relocate-and-copy support. Also fails if
 * path names a directory, or the total size would overflow. Returns 0
 * on success. */
int fs_append_file(const char *path, const void *data, unsigned int size);

/* Returns 0 and fills *out_size if found and it fits in buf_size, -1 if
 * not found, not a file (e.g. path names a directory), or too big for the
 * caller's buffer. */
int fs_read_file(const char *path, void *buf, unsigned int buf_size, unsigned int *out_size);

/* Removes the entry at path from its parent's table. Fails (-1) if path
 * doesn't exist, or if it names a non-empty directory -- there's no
 * recursive delete yet, so a directory must be emptied first. Note: this
 * only unlinks the directory-table entry; the sectors it (or a deleted
 * file's data) occupied are never reclaimed, since the allocator is a
 * simple one-way bump allocator with no free list yet. Returns 0 on
 * success. */
int fs_delete(const char *path);

/* Renames the entry at path to new_name, in place, within the same parent
 * directory -- new_name is a bare leaf name, not a path, so this can't
 * move an entry to a different directory. Fails (-1) if path doesn't
 * exist, new_name is empty, contains '/', is longer than 15 characters,
 * already names a different entry in the same directory, or new_name is
 * exactly "DEV" and path's parent is root (reserved -- see
 * fs_list_dir()'s comment). Works on files and directories alike -- a
 * directory's contents aren't touched, only its own table entry. Returns
 * 0 on success. */
int fs_rename(const char *path, const char *new_name);

/* Relocates path (must name an existing file -- directories are out of
 * scope for v1) into dest_dir, a metadata-only operation: no file data
 * is read, written, or reallocated, only the directory-table entry moves
 * from its current parent's table into dest_dir's. This matters because
 * the allocator never reclaims sectors -- implementing move as
 * copy-then-delete would permanently leak the original data's space on
 * every call. Fails (-1) if path doesn't exist or isn't a file, dest_dir
 * doesn't exist or isn't a directory, dest_dir's table is full, dest_dir
 * already has an entry named the same as path's leaf name (this also
 * covers "moving" a file into the directory it's already in -- a
 * guaranteed name collision with itself, so it correctly fails without
 * needing a special case), or the destination name is reserved (see
 * fs_list_dir()'s comment). Every one of those checks happens before any
 * write, so a failure there truly leaves both tables untouched -- except
 * one edge case that can't be checked for in advance: the destination
 * table is written first (to avoid ever losing the entry if the *source*
 * write then failed), so if that first write succeeds but the follow-up
 * write to the source table then fails (e.g. an ATA write failure), the
 * entry ends up present in both tables, aliased to the same start_lba,
 * rather than cleanly moved or cleanly unmoved. Returns 0 on success. */
int fs_move(const char *path, const char *dest_dir);

/* Duplicates the file at path into dest_dir under the same leaf name --
 * unlike fs_move(), a real second copy of the data is allocated and
 * written (there's no way around that for a genuine copy). Fails (-1)
 * under the same conditions as fs_move() (path must be an existing file;
 * dest_dir must exist, be a directory, have table space, and not already
 * have an entry with that name), plus running out of disk space for the
 * new copy. Returns 0 on success. */
int fs_copy_file(const char *path, const char *dest_dir);

/* Lists path's direct entries (not recursive) into out[], up to
 * max_entries (callers should size their buffer to FS_LIST_MAX to never
 * truncate). path must name a directory -- "/" for root. Returns 0 and
 * sets *out_count on success, -1 if path doesn't exist or isn't a
 * directory.
 *
 * "/DEV" is special: synthetic, never a real on-disk directory. Listing
 * it probes the two fixed ATA drives live (see fs.c) instead of reading
 * a stored table. Listing "/" always includes a synthetic "DEV" entry
 * alongside whatever real entries exist. Nothing can be created inside
 * "/DEV", and nothing can be created/renamed to shadow the name "DEV" at
 * root -- fs_create_dir()/fs_create_file()/fs_rename() all reject it. */
int fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count);

/* Appends name onto cwd ("/" gets name appended directly, without a
 * doubled leading slash; anything else gets a separating '/' first).
 * cap is dst's real size (callers should pass FS_PATH_MAX, not a guess)
 * -- name may come from an on-disk directory entry (fs_dirent.name),
 * which is only guaranteed to fit FS_NAME_MAX, not that cwd+"/"+name
 * fits in cap; truncates rather than overflows dst if it would run
 * past cap. */
void fs_path_join(char *dst, int cap, const char *cwd, const char *name);

/* In place: truncates cwd at its last '/', or resets it to "/" if that
 * was the leading slash (going up from a top-level directory). */
void fs_path_parent(char *cwd);

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
