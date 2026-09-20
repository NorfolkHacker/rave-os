#ifndef ACID_LIB_PATHS_H
#define ACID_LIB_PATHS_H

/* Validates one entry of a manifest's `libs` field before vm_host is
 * allowed to load and execute it. A .app.toml is app-controlled data
 * naming files this process will run as code, so the check lives here in
 * C rather than in desktop.rb's own manifest parser -- the same reasoning
 * as the Terminal sandbox escape fixed in 6cd6279: a Ruby-side check
 * protects nothing once anything else can reach the same C entry point.
 *
 * Safe means: non-empty, relative (no leading '/'), no empty path
 * component, no component that is exactly "..", no backslash, and ending
 * in ".rb". Everything safe is then resolved under v2/apps/ by the
 * caller, so a safe path can only ever name a file inside the apps
 * directory tree.
 *
 * Deliberately a pure function over a string -- it touches no filesystem,
 * so it is fully testable on its own (v2/tools/test_lib_paths.c) and
 * behaves identically on both targets. Returns 1 if safe, 0 otherwise. */
int lib_path_is_safe( const char * path );

#endif
