/* Rave-OS's bash-like shell: a second, fully separate command language
 * from forth.c (deliberately -- forth.c stays generic and filesystem-
 * free; this module's whole point is filesystem interaction, so
 * depending directly on fs.h is correct here, not a layering
 * violation). Same "no GUI dependencies" isolation forth.c already
 * practices -- kernel.c owns the window, the console widgets, and
 * rendering shell_eval_line()'s output; this file only ever touches
 * fs.h. */

#include "shell.h"

/* Bounded append with a running cursor, same shape as kernel.c's own
 * str_append() -- duplicated here rather than shared (see this
 * codebase's "each file owns its small string primitives" convention:
 * taskbar.c, startmenu.c, fs.c's own path_append() all do the same).
 * Truncates rather than overflows dst if src would run past cap. */
static void shell_append(char *dst, int *pos, int cap, const char *src) {
    if (cap <= 0) {
        return;
    }
    while (*src && *pos < cap - 1) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = 0;
}

static void shell_format_uint(unsigned int v, char *out) {
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v > 0) {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = 0;
}

/* True if line starts with word (case-insensitively -- matching
 * Forth's/RUN's own case-insensitive word lookup, see kernel.c's
 * match_run_command()), followed by a space or the end of the string --
 * a whole-token match, so "cat" doesn't match a line starting "catfoo".
 * No copying, unlike a fixed-size token buffer would need.
 *
 * Case-insensitive here, unlike shell_resolve()'s path arguments below
 * (which must match a real on-disk name exactly): this console's font
 * renders every character in uppercase-style glyphs, so "cd" and "CD"
 * are visually indistinguishable in the scrollback -- a command word
 * being case-sensitive here isn't discoverable, it's just confusing (a
 * real live-testing mistake, not a hypothetical). A path argument's
 * true casing is at least visible via `ls`, so it stays exact-match. */
static int shell_token_is(const char *line, const char *word) {
    int i;
    for (i = 0; word[i]; i++) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + 32);
        }
        if (c != word[i]) {
            return 0;
        }
    }
    return line[i] == 0 || line[i] == ' ';
}

/* The trimmed argument remainder after a command word already
 * confirmed by shell_token_is() -- borrowed pointer into line, not
 * copied, same shape kernel.c's match_run_command() already uses for
 * "RUN <arg>". Points at line's own terminating nul (an empty string)
 * if nothing follows the command word. */
static const char *shell_arg(const char *line, const char *cmd_word) {
    int i = 0;
    while (cmd_word[i]) {
        i++;
    }
    while (line[i] == ' ') {
        i++;
    }
    return &line[i];
}

/* Copies line's first whitespace-delimited token into out (bounded) --
 * only used for the "<word>: command not found" error message, since
 * shell_token_is()/shell_arg() never need to materialize the command
 * word as its own buffer for dispatch. */
static void shell_first_word(const char *line, char *out, int cap) {
    int i = 0, pos = 0;
    while (line[i] && line[i] != ' ' && pos < cap - 1) {
        out[pos++] = line[i++];
    }
    out[pos] = 0;
}

/* Resolves arg against sh->cwd into resolved (cap should be
 * FS_PATH_MAX): a leading '/' is used as-is (absolute); the bare token
 * ".." copies cwd and walks up via fs_path_parent(); the bare token "."
 * copies cwd unchanged (real bash's "current directory, no-op" -- with
 * no `.` entry ever on disk, this needs its own branch the same way
 * ".." does, or it would fall through to fs_path_join() and look for a
 * real entry literally named "."); anything else joins onto cwd via
 * fs_path_join(). Deliberately does not support "." or ".." embedded
 * partway through a longer relative path (e.g. "../foo", "./foo") --
 * the FILES window's own ".." affordance is exactly this same
 * one-level granularity, and the shell reaches the same set of
 * destinations FILES already does, not a superset. */
static void shell_resolve(const struct shell *sh, const char *arg, char *resolved, int cap) {
    if (arg[0] == '/') {
        int pos = 0;
        shell_append(resolved, &pos, cap, arg);
    } else if (arg[0] == '.' && arg[1] == '.' && arg[2] == 0) {
        int pos = 0;
        shell_append(resolved, &pos, cap, sh->cwd);
        fs_path_parent(resolved);
    } else if (arg[0] == '.' && arg[1] == 0) {
        int pos = 0;
        shell_append(resolved, &pos, cap, sh->cwd);
    } else {
        fs_path_join(resolved, cap, sh->cwd, arg);
    }
}

static int shell_name_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int shell_name_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 32);
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 32);
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/* Walks `path` (absolute, e.g. "/etc/config") one component at a time,
 * replacing each with its real on-disk casing found by scanning the
 * parent's actual listing -- so "cd home", "cd /home", "cat config" all
 * resolve to this OS's real, uppercase names, without fs.c's own
 * lookups (or FILES/RUN, which call them directly) becoming any less
 * case-sensitive. Each component tries an exact match first and only
 * falls back to a case-insensitive one if that fails, so a real
 * lowercase entry sharing a name with a differently-cased one (e.g.
 * this filesystem's own reserved-vs-real /DEV precedent) still resolves
 * to itself rather than being silently redirected to its sibling.
 *
 * If `correct_last` is 0, the final path component is left exactly as
 * typed and never looked up at all -- mkdir's own target is a name
 * being created, not one to match against something that (by
 * definition) doesn't exist yet; only the parent portion of its path
 * gets corrected.
 *
 * Leaves `path` completely untouched -- not partially rewritten -- if
 * any component that needed correcting has no match at all, exact or
 * case-insensitive: the caller's own fs_ call then fails with its
 * usual, correct error against the as-typed path, same as before this
 * function existed. */
static void shell_case_correct(char *path, int cap, int correct_last) {
    char resolved[FS_PATH_MAX];
    char component[FS_NAME_MAX];
    char tail[FS_NAME_MAX];
    int rpos = 1;
    int i = 1; /* path[0] is always '/' */
    int has_tail = 0;

    resolved[0] = '/';
    resolved[1] = 0;
    tail[0] = 0;

    while (path[i]) {
        int clen = 0;
        int is_last;

        while (path[i] && path[i] != '/' && clen < FS_NAME_MAX - 1) {
            component[clen++] = path[i++];
        }
        component[clen] = 0;
        is_last = (path[i] == 0);
        if (path[i] == '/') {
            i++;
        }
        if (clen == 0) {
            continue;
        }

        if (is_last && !correct_last) {
            int pos = 0;
            shell_append(tail, &pos, (int)sizeof(tail), component);
            has_tail = 1;
            break;
        }

        {
            struct fs_dirent entries[FS_LIST_MAX];
            unsigned int count, j;
            int matched = 0;
            int pass;

            if (fs_list_dir(resolved, entries, FS_LIST_MAX, &count) != 0) {
                return;
            }
            for (pass = 0; pass < 2 && !matched; pass++) {
                for (j = 0; j < count; j++) {
                    int eq = (pass == 0) ? shell_name_eq(entries[j].name, component)
                                         : shell_name_eq_ci(entries[j].name, component);
                    if (eq) {
                        int k;
                        if (rpos > 1) {
                            resolved[rpos++] = '/';
                        }
                        for (k = 0; entries[j].name[k] && rpos < (int)sizeof(resolved) - 1; k++) {
                            resolved[rpos++] = entries[j].name[k];
                        }
                        resolved[rpos] = 0;
                        matched = 1;
                        break;
                    }
                }
            }
            if (!matched) {
                return;
            }
        }
    }

    {
        int pos = 0;
        shell_append(path, &pos, cap, resolved);
        if (has_tail) {
            /* resolved is never empty (always at least "/"), so this
             * only skips the separator for the root case -- the same
             * "/" special-case fs_path_join() already needs, avoiding a
             * doubled "//tail" that would fail to resolve. */
            if (!(resolved[0] == '/' && resolved[1] == 0)) {
                shell_append(path, &pos, cap, "/");
            }
            shell_append(path, &pos, cap, tail);
        }
    }
}

/* The one public entry point into this file's otherwise-private path
 * resolution -- shell_resolve()/shell_case_correct() themselves stay
 * static, same "one shared boundary, not several newly-exported
 * privates" shape fs_path_join()/fs_path_parent() already established
 * between FILES and SHELL. correct_last is always 1 here: every caller
 * of this function is resolving something that's expected to already
 * exist (SHELL's own commands all use correct_last=1 except mkdir's
 * brand-new name), so a single fixed choice is simpler than exposing
 * the flag. cap should be FS_PATH_MAX. */
void shell_resolve_path(const struct shell *sh, const char *arg, char *resolved, int cap) {
    shell_resolve(sh, arg, resolved, cap);
    shell_case_correct(resolved, cap, 1);
}

/* No argument resets to /HOME (matches bash's own bare-cd-goes-home
 * behavior; /HOME is already this OS's real home directory, guaranteed
 * to exist by fs_bootstrap_dirs() before any window can open). An
 * argument is resolved and validated with fs_list_dir() *before* being
 * committed to sh->cwd -- unlike the FILES window's own click-to-
 * navigate (which commits unconditionally and just shows an empty
 * listing on failure), a shell needs real textual feedback on failure. */
static void shell_cmd_cd(struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char target[FS_PATH_MAX];
    struct fs_dirent tmp[FS_LIST_MAX];
    unsigned int tmp_count;

    if (arg[0] == 0) {
        int p = 0;
        shell_append(sh->cwd, &p, (int)sizeof(sh->cwd), "/HOME");
        return;
    }

    shell_resolve(sh, arg, target, (int)sizeof(target));
    shell_case_correct(target, (int)sizeof(target), 1);
    if (fs_list_dir(target, tmp, FS_LIST_MAX, &tmp_count) != 0) {
        shell_append(out, pos, cap, "cd: no such directory");
        return;
    }
    {
        int p = 0;
        shell_append(sh->cwd, &p, (int)sizeof(sh->cwd), target);
    }
}

/* No argument lists sh->cwd. One line per entry: "NAME/" for a
 * directory, "NAME SIZEB" for a file -- the exact format
 * draw_files_group() (kernel.c) already uses for FILES' own rows, just
 * as text instead of pixels. An empty directory prints nothing (real
 * ls behavior), not FILES' "(EMPTY)" placeholder. */
static void shell_cmd_ls(const struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char target[FS_PATH_MAX];
    struct fs_dirent entries[FS_LIST_MAX];
    unsigned int count;
    unsigned int i;

    if (arg[0] == 0) {
        int p = 0;
        shell_append(target, &p, (int)sizeof(target), sh->cwd);
    } else {
        shell_resolve(sh, arg, target, (int)sizeof(target));
        shell_case_correct(target, (int)sizeof(target), 1);
    }

    if (fs_list_dir(target, entries, FS_LIST_MAX, &count) != 0) {
        shell_append(out, pos, cap, "ls: no such directory");
        return;
    }

    /* Real bash's ls prints nothing at all for an empty directory, but
     * that reads as indistinguishable from a hung or silently-failed
     * command in a brand-new shell nobody has built trust in yet --
     * confirmed by live confusion, not a hypothetical. Says so
     * explicitly instead, same "(EMPTY)" wording the FILES window's
     * own listing already uses for the identical situation
     * (draw_files_group(), kernel.c) -- one consistent way this OS
     * shows "nothing here" everywhere, not a new convention invented
     * just for the shell. */
    if (count == 0) {
        shell_append(out, pos, cap, "(EMPTY)");
        return;
    }

    for (i = 0; i < count; i++) {
        if (i > 0) {
            shell_append(out, pos, cap, "\n");
        }
        shell_append(out, pos, cap, entries[i].name);
        if (entries[i].type == FS_TYPE_DIR) {
            shell_append(out, pos, cap, "/");
        } else {
            char num[12];
            shell_format_uint(entries[i].size_bytes, num);
            shell_append(out, pos, cap, " ");
            shell_append(out, pos, cap, num);
            shell_append(out, pos, cap, "B");
        }
    }
}

/* 512 -- matches kernel.c's shell_out[] output buffer size (the only
 * caller), so a file that fits this read buffer also fits fully into
 * the caller's out without a second, smaller truncation. Same "one
 * sector's worth" sizing kernel.c's own VIEWER_BUF_SIZE already used
 * for the same purpose before the VIEWER window was removed. */
#define SHELL_CAT_BUF_SIZE 512

/* No argument is treated as a failure -- there's no stdin to read from
 * here, unlike real bash's cat-with-no-args. fs_read_file() doesn't
 * distinguish "not found" from "not a file" from "too big for the
 * buffer", so all three collapse to one generic message, same as
 * kernel.c's RUN command does for the identical underlying failure
 * modes. */
static void shell_cmd_cat(const struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char target[FS_PATH_MAX];
    char buf[SHELL_CAT_BUF_SIZE];
    unsigned int out_size;

    if (arg[0] == 0) {
        shell_append(out, pos, cap, "cat: read failed");
        return;
    }
    shell_resolve(sh, arg, target, (int)sizeof(target));
    shell_case_correct(target, (int)sizeof(target), 1);

    if (fs_read_file(target, buf, SHELL_CAT_BUF_SIZE - 1, &out_size) != 0) {
        shell_append(out, pos, cap, "cat: read failed");
        return;
    }
    /* A genuinely empty (0-byte) file -- e.g. one FILES' own Enter-to-
     * create-a-file flow makes by default -- reads back as silent,
     * blank output, same "looks identical to a hang" ambiguity ls just
     * got fixed for. Says so explicitly for the same reason. */
    if (out_size == 0) {
        shell_append(out, pos, cap, "(EMPTY FILE)");
        return;
    }
    buf[out_size] = 0;
    shell_append(out, pos, cap, buf);
}

static void shell_cmd_mkdir(const struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char target[FS_PATH_MAX];

    if (arg[0] == 0) {
        shell_append(out, pos, cap, "mkdir: failed");
        return;
    }
    shell_resolve(sh, arg, target, (int)sizeof(target));
    /* correct_last = 0: only the parent portion is corrected -- the new
     * directory's own name is created exactly as typed, since it
     * doesn't exist yet to match against. */
    shell_case_correct(target, (int)sizeof(target), 0);
    if (fs_create_dir(target) != 0) {
        shell_append(out, pos, cap, "mkdir: failed");
    }
}

/* fs_delete() still has no recursive delete -- a non-empty directory
 * fails here exactly the same real way the FILES window's own DELETE
 * button already lives with, not a new limitation invented for this
 * command. */
static void shell_cmd_rm(const struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char target[FS_PATH_MAX];

    if (arg[0] == 0) {
        shell_append(out, pos, cap, "rm: failed");
        return;
    }
    shell_resolve(sh, arg, target, (int)sizeof(target));
    shell_case_correct(target, (int)sizeof(target), 1);
    if (fs_delete(target) != 0) {
        shell_append(out, pos, cap, "rm: failed");
    }
}

/* Splits arg (everything after the command word) into its first
 * whitespace-delimited token (copied, bounded, into first) and
 * everything after that token, trimmed of leading spaces (borrowed, not
 * copied -- same shape shell_arg() already uses for the single-argument
 * commands). mv/cp are the only two commands needing a source and a
 * destination rather than one path. */
static void shell_two_args(const char *arg, char *first, int first_cap, const char **second) {
    int i = 0, pos = 0;
    while (arg[i] && arg[i] != ' ' && pos < first_cap - 1) {
        first[pos++] = arg[i++];
    }
    first[pos] = 0;
    while (arg[i] == ' ') {
        i++;
    }
    *second = &arg[i];
}

/* Both the source file and the destination directory must already exist
 * -- unlike mkdir's own new name, mv doesn't invent anything, so both
 * arguments get full case correction (correct_last = 1). fs_move() is
 * itself files-only (directories out of scope for v1), so that
 * restriction applies here for free, no extra check needed. */
static void shell_cmd_mv(const struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char src_arg[FS_PATH_MAX];
    const char *dest_arg;
    char src[FS_PATH_MAX];
    char dest[FS_PATH_MAX];

    shell_two_args(arg, src_arg, (int)sizeof(src_arg), &dest_arg);
    if (src_arg[0] == 0 || dest_arg[0] == 0) {
        shell_append(out, pos, cap, "mv: failed");
        return;
    }
    shell_resolve(sh, src_arg, src, (int)sizeof(src));
    shell_case_correct(src, (int)sizeof(src), 1);
    shell_resolve(sh, dest_arg, dest, (int)sizeof(dest));
    shell_case_correct(dest, (int)sizeof(dest), 1);
    if (fs_move(src, dest) != 0) {
        shell_append(out, pos, cap, "mv: failed");
    }
}

/* Same argument handling as mv -- see its comment. */
static void shell_cmd_cp(const struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char src_arg[FS_PATH_MAX];
    const char *dest_arg;
    char src[FS_PATH_MAX];
    char dest[FS_PATH_MAX];

    shell_two_args(arg, src_arg, (int)sizeof(src_arg), &dest_arg);
    if (src_arg[0] == 0 || dest_arg[0] == 0) {
        shell_append(out, pos, cap, "cp: failed");
        return;
    }
    shell_resolve(sh, src_arg, src, (int)sizeof(src));
    shell_case_correct(src, (int)sizeof(src), 1);
    shell_resolve(sh, dest_arg, dest, (int)sizeof(dest));
    shell_case_correct(dest, (int)sizeof(dest), 1);
    if (fs_copy_file(src, dest) != 0) {
        shell_append(out, pos, cap, "cp: failed");
    }
}

/* Starts at root, not /HOME (unlike the FILES window's own cwd, and
 * unlike shell_cmd_cd()'s own bare-cd-goes-home behavior below) -- on
 * request, so the very first `ls` a user types shows the standard
 * system folders immediately rather than landing on an empty directory
 * first. */
void shell_init(struct shell *sh) {
    int pos = 0;
    shell_append(sh->cwd, &pos, (int)sizeof(sh->cwd), "/");
}

void shell_eval_line(struct shell *sh, const char *line, char *out, int out_cap) {
    int pos = 0;

    if (line[0] != 0) {
        if (shell_token_is(line, "pwd")) {
            shell_append(out, &pos, out_cap, sh->cwd);
        } else if (shell_token_is(line, "cd")) {
            shell_cmd_cd(sh, shell_arg(line, "cd"), out, &pos, out_cap);
        } else if (shell_token_is(line, "ls")) {
            shell_cmd_ls(sh, shell_arg(line, "ls"), out, &pos, out_cap);
        } else if (shell_token_is(line, "cat")) {
            shell_cmd_cat(sh, shell_arg(line, "cat"), out, &pos, out_cap);
        } else if (shell_token_is(line, "mkdir")) {
            shell_cmd_mkdir(sh, shell_arg(line, "mkdir"), out, &pos, out_cap);
        } else if (shell_token_is(line, "rm")) {
            shell_cmd_rm(sh, shell_arg(line, "rm"), out, &pos, out_cap);
        } else if (shell_token_is(line, "mv")) {
            shell_cmd_mv(sh, shell_arg(line, "mv"), out, &pos, out_cap);
        } else if (shell_token_is(line, "cp")) {
            shell_cmd_cp(sh, shell_arg(line, "cp"), out, &pos, out_cap);
        } else if (shell_token_is(line, "echo")) {
            shell_append(out, &pos, out_cap, shell_arg(line, "echo"));
        } else {
            char word[16];
            shell_first_word(line, word, (int)sizeof(word));
            shell_append(out, &pos, out_cap, word);
            shell_append(out, &pos, out_cap, ": command not found");
        }
    }

    out[pos] = 0;
}
