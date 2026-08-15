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
 * mkdir <path>, rm <path>, echo <text> -- lowercase, case-sensitive
 * (unlike Forth's/RUN's case-insensitive matching), no quoting/pipes/
 * redirection. An unrecognized command writes "<word>: command not
 * found", matching bash's own wording. */
void shell_eval_line(struct shell *sh, const char *line, char *out, int out_cap);

#endif
