#ifndef RAVEOS_SHELL_H
#define RAVEOS_SHELL_H

#include "fs.h"

/* One shell's state: just its current directory. Everything else a
 * command needs (the typed line, an output buffer) is passed directly
 * to shell_eval_line() per call, same shape forth_eval_line() already
 * uses (forth.h) -- no hidden state beyond cwd. */
struct shell {
    char cwd[FS_PATH_MAX];
};

/* Sets cwd to "/" -- so the first `ls` a user types shows the standard
 * system folders immediately, rather than the FILES window's own
 * /HOME starting point (which is empty by default). `cd` with no
 * argument still goes to /HOME (shell.c), matching bash's own
 * bare-cd-goes-home behavior -- only the boot-time default differs. */
void shell_init(struct shell *sh);

/* Executes one typed line against sh's cwd, writing '\n'-separated
 * output into out (out_cap includes room for a nul terminator). Same
 * calling shape as forth_eval_line() (forth.h) -- kernel.c's existing
 * append_split_lines() renders the result into a console_output the
 * same way it already does for Forth and RUN. An empty line produces
 * empty output (a silent no-op, same as pressing Enter at an empty bash
 * prompt). Commands: pwd, cd [path], ls [path], cat <path>,
 * mkdir <path>, rm <path>, mv <src> <dest_dir>, cp <src> <dest_dir>,
 * echo <text> -- both mv and cp take a bare destination *directory*, not
 * a full destination path (there's no rename-during-move here); the
 * command word itself is
 * case-insensitive (CD/cd/Cd all work, matching Forth's/RUN's own
 * case-insensitive word lookup -- this console's font can't visually
 * distinguish typed case, so a case-sensitive command word wasn't
 * discoverable), no quoting/pipes/redirection. Path arguments are also
 * forgiving about case (`cd home`, `cat config` resolve to the real
 * `HOME`/`CONFIG` entries) for the identical font reason -- but only as
 * a fallback: an exact-case match always wins first (shell.c's
 * shell_case_correct()), so this stays consistent with fs.c's own
 * genuinely case-sensitive storage rather than pretending it isn't
 * case-sensitive. `mkdir`'s own new name is the one exception, created
 * exactly as typed -- there's nothing existing yet to correct it
 * against. An unrecognized command writes "<word>: command not
 * found", matching bash's own wording. */
void shell_eval_line(struct shell *sh, const char *line, char *out, int out_cap);

/* Resolves arg against sh->cwd, case-corrected against real on-disk
 * names (see shell_case_correct()'s own doc comment in shell.c) -- the
 * one public entry point into this file's path resolution, for callers
 * outside shell.c that need to resolve a path themselves rather than
 * going through a full command line (kernel.c's EDIT interception is
 * the only current caller). cap should be FS_PATH_MAX. */
void shell_resolve_path(const struct shell *sh, const char *arg, char *resolved, int cap);

#endif
