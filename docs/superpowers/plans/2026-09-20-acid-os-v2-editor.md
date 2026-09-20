# acid OS v2 Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Grow `v2/apps/editor.rb` from a 246-line minimal slice into a real editor — undo/redo, mark-based selection and clipboard, find, go-to-line, save-as, Ruby syntax highlighting, touch editing, and a Run command — split across per-app library files loaded through a new manifest field.

**Architecture:** A new `libs` field in `.app.toml` lets one app load its own Ruby modules into its VM without every other app paying for them; the editor uses it to split into `editor/buffer.rb` (pure data: lines, cursor, undo, selection), `editor/hl.rb` (pure: Ruby tokenizer), `editor/cmdbar.rb` and `editor/touch.rb` (UI mixins), leaving `editor.rb` as the shell. The two pure files call no `acid_*` binding, so they run headless under the vendored host mruby and carry the test suite.

**Tech Stack:** mruby 4.0 (vendored, `v2/components/mruby/build/host/bin/mruby`), C99 against FreeRTOS, LovyanGFX via the project's own `gfx`/`hal_display` layers, CMake for `v2/sim`.

**Spec:** `docs/superpowers/specs/2026-09-20-acid-os-v2-editor-design.md`

## Global Constraints

- **No `require` exists in this runtime.** `vm_host.c` loads a fixed list of `v2/apps/lib/*.rb` into every VM, then the app script. Ruby files cannot load each other.
- **The only keys an app ever receives** are printable ASCII 32–126 (Shift already resolved into the character) plus `AcidKeys::ENTER`, `BACKSPACE`, `ESCAPE`, `TAB`, `DELETE`, `UP`, `DOWN`, `LEFT`, `RIGHT`. No Ctrl, no Alt, no function keys. Never write a keybinding that needs one.
- **mruby, not CRuby.** No `require`, no `Struct`, no keyword arguments, no `String#each_char`, no `Array#sum`. Verified available and used by this plan: `String#index(str, offset)`, `String#split(sep, -1)`, `String#start_with?`/`end_with?`, `str[i, len]`, `Array#insert`/`delete_at`/`include?`/`join`, destructuring assignment from an array, `=~` with a literal regex, `i += 1 while cond`.
- **Window is 420×280** for the editor after Task 5. Screen is 640×360 (`KERNEL_SCREEN_W`/`H`).
- **Theme colours** come from `v2/core/kernel/kernel_theme.h`; app Ruby hardcodes the hex with a `# THEME_x` comment, the established convention.
- **Run the sim** with `cmake --build v2/sim/build && ./v2/sim/build/acidos_sim`. When driving it with `xdotool`, activate the window **once** and then only move/click — repeated `windowactivate` calls desync Panel_sdl's touch scaling.
- **Every commit message** ends with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.

---

### Task 1: `lib_path_is_safe` — validating manifest-supplied library paths

A manifest is app-controlled data, and `libs` names files the VM will execute as code. The check belongs in C, at the point of use, for the same reason the Terminal sandbox escape fixed in `6cd6279` did: a check in `desktop.rb`'s parser protects nothing once anything else can reach the same C entry point.

**Files:**
- Create: `v2/core/vm_host/lib_paths.h`
- Create: `v2/core/vm_host/lib_paths.c`
- Test: `v2/tools/test_lib_paths.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `int lib_path_is_safe( const char * path )` — returns 1 for a relative `.rb` path under `v2/apps` with no `..` component, 0 otherwise.

- [ ] **Step 1: Write the failing test**

Create `v2/tools/test_lib_paths.c`:

```c
/* Standalone test for lib_path_is_safe (v2/core/vm_host/lib_paths.c).
 * Not part of either CMake build -- that function deliberately depends on
 * nothing but libc, so it compiles and runs on its own in one cc command
 * (see the header of this file's own run instructions in the plan). */
#include <stdio.h>
#include <string.h>

#include "../core/vm_host/lib_paths.h"

static int g_fails = 0;

static void
check( const char * path, int expected )
{
    int got = lib_path_is_safe( path );
    if( got == expected )
    {
        printf( "  ok  %-28s -> %d\n", path ? path : "(null)", got );
    }
    else
    {
        printf( "FAIL  %-28s -> %d, expected %d\n", path ? path : "(null)", got, expected );
        g_fails++;
    }
}

int
main( void )
{
    check( "editor/buffer.rb", 1 );
    check( "hl.rb", 1 );
    check( "a/b/c/deep.rb", 1 );
    check( "weird..name.rb", 1 );   /* ".." as substring, not as a component */

    check( NULL, 0 );
    check( "", 0 );
    check( "../secret.rb", 0 );
    check( "editor/../../secret.rb", 0 );
    check( "..", 0 );
    check( "/etc/passwd", 0 );
    check( "/abs/path.rb", 0 );
    check( "editor//buffer.rb", 0 );
    check( "editor\\buffer.rb", 0 );
    check( "editor/buffer.txt", 0 );
    check( "buffer", 0 );
    check( ".rb", 1 );              /* odd but harmless: a file literally named ".rb" */

    if( g_fails > 0 )
    {
        printf( "%d failure(s)\n", g_fails );
        return 1;
    }
    printf( "all passed\n" );
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /home/norfolkh/os
cc -Wall -Wextra -o v2/sim/build/test_lib_paths \
   v2/tools/test_lib_paths.c v2/core/vm_host/lib_paths.c && \
   ./v2/sim/build/test_lib_paths
```

Expected: FAIL — `v2/core/vm_host/lib_paths.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `v2/core/vm_host/lib_paths.h`:

```c
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
```

- [ ] **Step 4: Write the implementation**

Create `v2/core/vm_host/lib_paths.c`:

```c
#include <stddef.h>
#include <string.h>

#include "lib_paths.h"

int
lib_path_is_safe( const char * path )
{
    if( path == NULL || path[ 0 ] == '\0' )
    {
        return 0;
    }
    if( path[ 0 ] == '/' )
    {
        return 0;
    }
    if( strchr( path, '\\' ) != NULL )
    {
        return 0;
    }

    size_t len = strlen( path );
    if( len < 3 || strcmp( path + len - 3, ".rb" ) != 0 )
    {
        return 0;
    }

    /* Walk components rather than searching for ".." as a substring: a
     * file called "weird..name.rb" is perfectly fine, and "a/../../x.rb"
     * is not -- only a component that IS ".." escapes the directory. An
     * empty component (from "a//b.rb" or a trailing '/') is rejected too,
     * since it means the path wasn't written the way it appears. */
    const char * p = path;
    while( *p != '\0' )
    {
        const char * seg = p;
        while( *p != '\0' && *p != '/' )
        {
            p++;
        }
        size_t seg_len = ( size_t ) ( p - seg );
        if( seg_len == 0 )
        {
            return 0;
        }
        if( seg_len == 2 && seg[ 0 ] == '.' && seg[ 1 ] == '.' )
        {
            return 0;
        }
        if( *p == '/' )
        {
            p++;
        }
    }
    return 1;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
cd /home/norfolkh/os
cc -Wall -Wextra -o v2/sim/build/test_lib_paths \
   v2/tools/test_lib_paths.c v2/core/vm_host/lib_paths.c && \
   ./v2/sim/build/test_lib_paths
```

Expected: PASS — every line `ok`, final line `all passed`, exit code 0.

- [ ] **Step 6: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/vm_host/lib_paths.h v2/core/vm_host/lib_paths.c v2/tools/test_lib_paths.c
git commit -m "$(cat <<'EOF'
v2: validate manifest-supplied library paths

A .app.toml's coming `libs` field names files the VM will execute, and
a manifest is app-controlled data. lib_path_is_safe rejects absolute
paths, backslashes, empty and ".." components, and anything not ending
in .rb, so a surviving path can only name a file under v2/apps.

Pure function over a string, so it tests standalone with no FreeRTOS
or filesystem underneath it.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Plumb `libs` from manifest to VM

**Files:**
- Modify: `v2/core/vm_host/vm_host.h` (add `libs` to `struct vm_host_params`)
- Modify: `v2/core/vm_host/vm_host.c` (`load_libs_into_vm`, call it in `vm_host_task`)
- Modify: `v2/core/kernel/kernel_spawn.h`, `v2/core/kernel/kernel_spawn.c` (new `libs` parameter)
- Modify: `v2/core/bindings/window_binding.c` (`struct launchable_app`, `acid_launcher_register`, `libs_by_path`, both spawn sites)
- Modify: `v2/apps/desktop.rb` (`register_launchable` passes `libs`)
- Modify: `v2/sim/CMakeLists.txt`, `v2/hw/main/CMakeLists.txt` (compile `lib_paths.c`)
- Modify: `v2/sim/sim_main.c`, `v2/hw/main/app_main.c` (boot-time `kernel_spawn_app` calls gain the new argument)

**Interfaces:**
- Consumes: `lib_path_is_safe(const char *)` from Task 1.
- Produces:
  - `void * kernel_spawn_app( const char * script_path, int x, int y, int w, int h, int closable, const char * arg, const char * libs )`
  - `acid_launcher_register(path, name, w, h, multi, libs)` in Ruby — six arguments, `libs` a `String` (`""` when the manifest has none).
  - `struct vm_host_params` field `const char * libs;`

- [ ] **Step 1: Write the failing test**

There is no C test harness for the spawn path, and there cannot usefully be one — it needs a live FreeRTOS scheduler. The test is a real app that only works if its `libs` file loaded. Create `v2/apps/editor/probe.rb`:

```ruby
# Temporary Task 2 probe: proves a manifest's `libs` file really is loaded
# into the app's VM before the app script runs. Deleted at the end of
# Task 2 -- the editor's real modules replace it from Task 3 on.
LIBS_PROBE = "libs loaded"
```

Add to `v2/apps/editor.app.toml`:

```toml
libs = editor/probe.rb
```

Add as the first line of `EditorApp#on_create` in `v2/apps/editor.rb`:

```ruby
    puts "EDITOR PROBE: #{defined?(LIBS_PROBE) ? LIBS_PROBE : 'NOT LOADED'}"
```

- [ ] **Step 2: Run the sim to verify it fails**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build && ./v2/sim/build/acidos_sim 2>&1 | grep "EDITOR PROBE"
```

Open Menu → Editor. Expected: `EDITOR PROBE: NOT LOADED` — nothing loads `libs` yet. (Close the sim window to end the run.)

- [ ] **Step 3: Add `libs` to the VM params and load them**

In `v2/core/vm_host/vm_host.h`, add to `struct vm_host_params` after `arg`:

```c
    /* Comma-separated list of this app's own Ruby modules, relative to
     * v2/apps (e.g. "editor/buffer.rb, editor/hl.rb"), or NULL for an app
     * that has none. Loaded into this VM after the shared apps/lib/*.rb
     * set and before the app's own script, so a module can define classes
     * the script then uses at its top level. Same ownership contract as
     * script_path and arg: not copied here, the caller must keep it
     * alive for the task's lifetime. */
    const char * libs;
```

In `v2/core/vm_host/vm_host.c`, add the include beside the others:

```c
#include "lib_paths.h"
```

Add beside the other path defines near line 33:

```c
/* Every per-app module path from a manifest resolves under here, and
 * lib_path_is_safe (lib_paths.h) has already guaranteed the path can't
 * climb out of it. */
#define ACID_APPS_DIR "v2/apps/"
#define ACID_LIB_ENTRY_MAX 128
```

Add above `vm_host_task`:

```c
/* Splits a manifest's comma-separated `libs` value and loads each entry,
 * skipping (with a message, not a crash) anything unsafe or overlong --
 * one bad entry in a manifest shouldn't stop the app from starting, the
 * same tolerance desktop.rb's own manifest scan already has for a
 * malformed file. */
static void
load_libs_into_vm( mrb_state * mrb, mrb_ccontext * cxt, const char * libs )
{
    if( libs == NULL )
    {
        return;
    }

    const char * p = libs;
    while( *p != '\0' )
    {
        while( *p == ' ' || *p == ',' )
        {
            p++;
        }
        const char * start = p;
        while( *p != '\0' && *p != ',' )
        {
            p++;
        }
        const char * end = p;
        while( end > start && end[ -1 ] == ' ' )
        {
            end--;
        }

        size_t len = ( size_t ) ( end - start );
        if( len == 0 )
        {
            continue;
        }
        if( len >= ACID_LIB_ENTRY_MAX )
        {
            fprintf( stderr, "acid OS v2: lib path too long, skipped\n" );
            continue;
        }

        char entry[ ACID_LIB_ENTRY_MAX ];
        memcpy( entry, start, len );
        entry[ len ] = '\0';

        if( !lib_path_is_safe( entry ) )
        {
            fprintf( stderr, "acid OS v2: rejected unsafe lib path %s\n", entry );
            continue;
        }

        char full[ sizeof( ACID_APPS_DIR ) + ACID_LIB_ENTRY_MAX ];
        snprintf( full, sizeof( full ), ACID_APPS_DIR "%s", entry );
        load_file_into_vm( mrb, cxt, full );
    }
}
```

In `vm_host_task`, add the copy beside the other `ctx`/params reads is not needed (nothing in `ctx` uses it); add the load between the fixed libs and the app script:

```c
    load_file_into_vm( mrb, cxt, ACID_GAME_LIB_PATH );
    load_libs_into_vm( mrb, cxt, params->libs );
    load_file_into_vm( mrb, cxt, params->script_path );
```

- [ ] **Step 4: Thread `libs` through the spawn path**

In `v2/core/kernel/kernel_spawn.h`, extend the signature and its doc comment:

```c
/* ... existing comment ... `libs` is this app's own comma-separated module
 * list from its manifest (NULL for none) -- see vm_host.h's own field
 * comment, and the same ownership contract as `arg`. */
void * kernel_spawn_app( const char * script_path, int x, int y, int w, int h, int closable,
                          const char * arg, const char * libs );
```

In `v2/core/kernel/kernel_spawn.c`, add the parameter to the definition and set it where the other params are filled:

```c
    params->libs = libs;
```

In `v2/core/bindings/window_binding.c`:

Add to `struct launchable_app`, after `multi`:

```c
    /* This app's own Ruby modules, from its manifest's `libs` field --
     * see vm_host.h. NULL for the great majority of apps, which have
     * none. Held here rather than passed to acid_spawn_app because the
     * registry is already the one place both launch paths (the Menu's
     * acid_launcher_spawn and File Manager's acid_spawn_app) agree on. */
    char * libs;
```

Extend `acid_launcher_register` — the format string gains one `s`:

```c
    char * libs;
    mrb_int libs_len;
    mrb_get_args( mrb, "ssiibs", &path, &path_len, &name, &name_len, &w, &h, &multi,
                  &libs, &libs_len );
    ( void ) libs_len;
```

and, after `slot->multi = multi ? 1 : 0;`:

```c
    /* An empty manifest field and a missing one are the same thing to
     * vm_host, which takes NULL to mean "this app has no modules". */
    slot->libs = ( libs[ 0 ] == '\0' ) ? NULL : dup_cstr( libs );
```

Add beside `is_multi_by_path`:

```c
/* The registered `libs` for a script path -- the acid_spawn_app half of
 * the same lookup is_multi_by_path does, so File Manager launching an app
 * by path gets that app's modules exactly as the Menu does. NULL for an
 * unregistered path, which is also the right answer: no manifest, no
 * modules. */
static const char *
libs_by_path( const char * path )
{
    int i;
    for( i = 0; i < g_registered_count; i++ )
    {
        if( strcmp( g_registered[ i ].path, path ) == 0 )
        {
            return g_registered[ i ].libs;
        }
    }
    return NULL;
}
```

Update both `kernel_spawn_app` call sites in this file. In `acid_launcher_spawn` (around line 213):

```c
    void * task = kernel_spawn_app( g_registered[ index ].path, x, y,
                                     g_registered[ index ].w, g_registered[ index ].h, 1, NULL,
                                     g_registered[ index ].libs );
```

In `acid_spawn_app`, pass `libs_by_path( path )` as the new final argument.

- [ ] **Step 5: Update the remaining call sites and both builds**

Every other `kernel_spawn_app` call gains a trailing `NULL`. Find them all:

```bash
cd /home/norfolkh/os
grep -rn "kernel_spawn_app(" v2/ --include=*.c --include=*.h | grep -v components/
```

Expected sites: `v2/sim/sim_main.c` and `v2/hw/main/app_main.c` (the boot set — desktop and any fixed apps), plus the two in `window_binding.c` already done in Step 4.

In `v2/apps/desktop.rb`'s `register_launchable`, replace the register call:

```ruby
    # An app with no `libs` line passes "", which window_binding.c reads as
    # "no modules" -- see its own comment on slot->libs.
    libs = fields["libs"] || ""
    return unless acid_launcher_register(rb_path, fields["name"], fields["w"].to_i,
                                         fields["h"].to_i, multi, libs)
```

Add `lib_paths.c` to both builds. In `v2/sim/CMakeLists.txt`, beside the other `core/vm_host` source:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/lib_paths.c
```

In `v2/hw/main/CMakeLists.txt`, add `../../core/vm_host/lib_paths.c` to `SRCS` alongside the existing `vm_host.c` entry.

- [ ] **Step 6: Run the sim to verify the probe now loads**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build && ./v2/sim/build/acidos_sim 2>&1 | grep "EDITOR PROBE"
```

Open Menu → Editor. Expected: `EDITOR PROBE: libs loaded`.

Then check the reject path still lets the app start — temporarily set `libs = ../../etc/passwd.rb, editor/probe.rb` in `editor.app.toml`, rebuild, launch Editor:

Expected: `acid OS v2: rejected unsafe lib path ../../etc/passwd.rb` on stderr, `EDITOR PROBE: libs loaded` still printed, Editor window opens normally. Restore `libs = editor/probe.rb` afterwards.

Also confirm an app with no `libs` is unaffected — open Menu → Config and Menu → Terminal and check both still work.

- [ ] **Step 7: Remove the probe and commit**

```bash
cd /home/norfolkh/os
rm v2/apps/editor/probe.rb
```

Remove the `puts "EDITOR PROBE: ..."` line from `editor.rb` and set `editor.app.toml`'s field to the modules the following tasks create:

```toml
libs = editor/buffer.rb, editor/hl.rb, editor/cmdbar.rb, editor/touch.rb
```

`vm_host`'s `load_file_into_vm` already prints `could not open` and carries on for a missing file, so the editor keeps working while those files don't exist yet.

```bash
git add -A v2/core v2/sim v2/hw v2/apps
git commit -m "$(cat <<'EOF'
v2: per-app Ruby modules via a manifest `libs` field

This runtime has no require: vm_host loads a fixed apps/lib/*.rb set
into every VM, then the app script. An app large enough to want its own
modules had nowhere to put them except that global list, which every
other app then pays to parse.

A manifest can now name its own, loaded after the shared set and before
the app script. The value rides in the launcher registry, so both launch
paths -- the Menu and File Manager -- pick it up from one lookup.
Entries run through lib_path_is_safe first.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: `Buffer` — lines, cursor, edits, undo/redo

Two primitives carry every mutation: insert text at a position, delete a range. A newline is just text, so splitting and joining lines need no separate operation, and undo has exactly two record types to invert.

**Files:**
- Create: `v2/apps/editor/buffer.rb`
- Create: `v2/tools/test_editor.rb`

**Interfaces:**
- Consumes: nothing (calls no `acid_*` binding, by design).
- Produces: `class Buffer` with
  `initialize(lines)`, `lines`, `line_count`, `line(i)`, `current_line`, `cx`, `cy`,
  `modified?`, `mark_saved`, `set_cursor(x, y)`, `move(dx, dy)`,
  `insert_char(ch)`, `insert_text(text)`, `split_line`, `backspace`, `delete_forward`,
  `delete_range(sx, sy, ex, ey)`, `undo`, `redo`, `end_group`, `take_dirty`.
  `undo`/`redo` return `true` when something happened, `false` when the stack was empty.
  `take_dirty` returns `:all` or an `Array` of line indexes, and clears.

- [ ] **Step 1: Write the failing test**

Create `v2/tools/test_editor.rb`:

```ruby
# Headless tests for the editor's pure modules -- Buffer (editor/buffer.rb)
# and Hl (editor/hl.rb). Neither calls an acid_* binding, so both run under
# the vendored host mruby with no OS underneath them.
#
# Run (this runtime has no require, so the sources are concatenated in,
# exactly the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/editor/buffer.rb v2/apps/editor/hl.rb \
#     v2/tools/test_editor.rb | ./v2/components/mruby/build/host/bin/mruby -

$fails = 0

def eq(actual, expected, what)
  if actual == expected
    puts "  ok  #{what}"
  else
    $fails += 1
    puts "FAIL  #{what}"
    puts "      expected #{expected.inspect}"
    puts "      got      #{actual.inspect}"
  end
end

def group(name)
  puts name
end

# ---------------------------------------------------------------- Buffer

group("Buffer: editing")

b = Buffer.new(["hello"])
b.set_cursor(5, 0)
b.insert_text(" world")
eq(b.lines, ["hello world"], "insert_text appends")
eq([b.cx, b.cy], [11, 0], "cursor lands past inserted text")
eq(b.modified?, true, "insert marks modified")

b = Buffer.new(["hello"])
b.set_cursor(2, 0)
b.insert_text("\n")
eq(b.lines, ["he", "llo"], "newline splits the line")
eq([b.cx, b.cy], [0, 1], "cursor moves to start of new line")

b = Buffer.new(["one", "two"])
b.set_cursor(3, 0)
b.delete_forward
eq(b.lines, ["onetwo"], "delete at end of line joins the next")

b = Buffer.new(["one", "two"])
b.set_cursor(0, 1)
b.backspace
eq(b.lines, ["onetwo"], "backspace at start of line joins the previous")
eq([b.cx, b.cy], [3, 0], "cursor sits at the join")

b = Buffer.new(["abc", "def", "ghi"])
b.delete_range(1, 0, 2, 2)
eq(b.lines, ["ai"], "delete_range spanning lines collapses them")
eq([b.cx, b.cy], [1, 0], "cursor lands at the range start")

b = Buffer.new(["ab"])
b.set_cursor(1, 0)
b.insert_text("X\nY")
eq(b.lines, ["aX", "Yb"], "multi-line insert splits around the cursor")
eq([b.cx, b.cy], [1, 1], "cursor lands past multi-line insert")

group("Buffer: cursor")

b = Buffer.new(["abc", "de"])
b.set_cursor(3, 0)
b.move(1, 0)
eq([b.cx, b.cy], [0, 1], "right at end of line wraps to the next")
b.move(-1, 0)
eq([b.cx, b.cy], [3, 0], "left at start of line wraps to the previous")
b.set_cursor(0, 0)
b.move(-1, 0)
eq([b.cx, b.cy], [0, 0], "left at start of buffer stays put")
b.set_cursor(3, 0)
b.move(0, 1)
eq([b.cx, b.cy], [2, 1], "down onto a shorter line clamps the column")
b.set_cursor(0, 99)
eq(b.cy, 1, "set_cursor clamps past the last line")

group("Buffer: undo/redo")

b = Buffer.new([""])
"word".split("").each { |c| b.insert_char(c) }
eq(b.lines, ["word"], "typed characters land")
eq(b.undo, true, "undo reports it did something")
eq(b.lines, [""], "a typed run undoes as one step")

b = Buffer.new([""])
"ab cd".split("").each { |c| b.insert_char(c) }
eq(b.lines, ["ab cd"], "typed run with a space lands")
b.undo
eq(b.lines, ["ab "], "undo steps back one word, not the whole line")
b.undo
eq(b.lines, ["ab"], "the space is its own step")
b.undo
eq(b.lines, [""], "and the first word is another")

b = Buffer.new([""])
"hi".split("").each { |c| b.insert_char(c) }
b.undo
eq(b.redo, true, "redo reports it did something")
eq(b.lines, ["hi"], "redo reapplies the undone run")
b.undo
b.insert_char("x")
eq(b.redo, false, "a new edit invalidates redo")

b = Buffer.new(["abc"])
eq(b.undo, false, "undo on an untouched buffer is a no-op")

b = Buffer.new(["one", "two"])
b.set_cursor(3, 0)
b.delete_forward
b.undo
eq(b.lines, ["one", "two"], "undo restores a joined line")

b = Buffer.new([""])
n = 0
while n < Buffer::UNDO_MAX + 20
  b.insert_char("x")
  b.end_group
  n += 1
end
n = 0
n += 1 while b.undo
eq(n, Buffer::UNDO_MAX, "the undo stack caps at UNDO_MAX records")

group("Buffer: dirty lines")

b = Buffer.new(["a", "b", "c"])
b.take_dirty
b.set_cursor(1, 1)
b.insert_char("x")
eq(b.take_dirty, [1], "a single-line edit dirties only that line")
b.insert_text("\n")
eq(b.take_dirty, :all, "a line-count change dirties everything")

# ----------------------------------------------------------------- done

raise "#{$fails} failure(s)" if $fails > 0
puts "all passed"
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /home/norfolkh/os
cat v2/apps/editor/buffer.rb v2/tools/test_editor.rb 2>/dev/null | \
  ./v2/components/mruby/build/host/bin/mruby -
```

Expected: FAIL — `uninitialized constant Buffer`.

- [ ] **Step 3: Write the implementation**

Create `v2/apps/editor/buffer.rb`:

```ruby
# The editor's text buffer: the lines, the cursor, every mutation, and
# undo/redo. Selection and the clipboard arrive in Task 4.
#
# Calls no acid_* binding on purpose -- it is arrays and strings and
# nothing else, so v2/tools/test_editor.rb runs it under the host mruby
# with no OS underneath it. Everything that needs to draw lives in
# editor.rb.
#
# Every mutation goes through exactly two primitives: insert text at a
# position, and delete a range. A newline is just text, so splitting and
# joining lines are not separate operations, and undo has two record
# types to invert rather than six.
class Buffer
  # Deliberately a cap on records, not on bytes: an app VM runs in a fixed
  # mruby pool (vm_host.c logs its usage), and an unbounded history in a
  # long editing session is the kind of slow leak that shows up as a
  # mysterious allocation failure hours later.
  UNDO_MAX = 200

  attr_reader :cx, :cy

  def initialize(lines)
    @lines = (lines.nil? || lines.empty?) ? [""] : lines
    @cx = 0
    @cy = 0
    @undo = []
    @redo = []
    @modified = false
    # True once something has closed the current typing run, so the next
    # inserted character starts a fresh undo record instead of joining
    # the previous one. See insert_char.
    @group_closed = false
    @dirty = []
    @dirty_all = true
  end

  def lines
    @lines
  end

  def line_count
    @lines.length
  end

  def line(i)
    @lines[i] || ""
  end

  def current_line
    line(@cy)
  end

  def modified?
    @modified
  end

  # Called after a successful save: the text is unchanged, but it is no
  # longer different from what's on disk.
  def mark_saved
    @modified = false
  end

  # ---- cursor ----

  def set_cursor(x, y)
    y = 0 if y < 0
    y = @lines.length - 1 if y >= @lines.length
    x = 0 if x < 0
    x = line(y).length if x > line(y).length
    @cx = x
    @cy = y
    end_group
  end

  def move(dx, dy)
    if dy != 0
      ny = @cy + dy
      ny = 0 if ny < 0
      ny = @lines.length - 1 if ny >= @lines.length
      @cy = ny
      # A shorter line can't hold the old column; clamping rather than
      # remembering the "desired" column keeps this to one rule, and the
      # window is wide enough that the difference rarely shows.
      @cx = current_line.length if @cx > current_line.length
    end
    if dx != 0
      @cx += dx
      if @cx < 0
        if @cy > 0
          @cy -= 1
          @cx = current_line.length
        else
          @cx = 0
        end
      elsif @cx > current_line.length
        if @cy < @lines.length - 1
          @cy += 1
          @cx = 0
        else
          @cx = current_line.length
        end
      end
    end
    end_group
  end

  # ---- primitives (no undo record: the wrappers below own that, so undo
  #      itself can use these to put text back without recording the
  #      put-back as a fresh edit) ----

  def raw_insert(x, y, text)
    return [x, y] if text.nil? || text.length == 0
    parts = text.split("\n", -1)
    parts = [""] if parts.empty?
    src = line(y)
    head = src[0, x]
    tail = src[x, src.length - x]
    if parts.length == 1
      @lines[y] = head + parts[0] + tail
      mark_dirty(y)
      return [x + parts[0].length, y]
    end
    @lines[y] = head + parts[0]
    i = 1
    while i < parts.length - 1
      @lines.insert(y + i, parts[i])
      i += 1
    end
    last = parts[parts.length - 1]
    @lines.insert(y + parts.length - 1, last + tail)
    mark_dirty_all
    [last.length, y + parts.length - 1]
  end

  # Start must not come after end. Returns the text removed, so a caller
  # can record it for undo.
  def raw_delete(sx, sy, ex, ey)
    if sy == ey
      src = line(sy)
      text = src[sx, ex - sx]
      @lines[sy] = src[0, sx] + src[ex, src.length - ex]
      mark_dirty(sy)
      return text
    end
    parts = [line(sy)[sx, line(sy).length - sx]]
    i = sy + 1
    while i < ey
      parts << line(i)
      i += 1
    end
    parts << line(ey)[0, ex]
    @lines[sy] = line(sy)[0, sx] + line(ey)[ex, line(ey).length - ex]
    i = ey
    while i > sy
      @lines.delete_at(i)
      i -= 1
    end
    mark_dirty_all
    parts.join("\n")
  end

  # ---- recording edits ----

  def insert_text(text)
    return if text.nil? || text.length == 0
    bx = @cx
    by = @cy
    ex, ey = raw_insert(bx, by, text)
    push_undo([:ins, bx, by, text, bx, by])
    @cx = ex
    @cy = ey
    @modified = true
  end

  def delete_range(sx, sy, ex, ey)
    text = raw_delete(sx, sy, ex, ey)
    push_undo([:del, sx, sy, text, @cx, @cy])
    @cx = sx
    @cy = sy
    @modified = true
    text
  end

  # A run of typed non-space characters coalesces into one undo record, so
  # undo steps back by word rather than by letter -- the difference
  # between undo being useful and being a way to watch your own typing in
  # reverse. A space, a newline, a cursor move or any other kind of edit
  # ends the run.
  def insert_char(ch)
    if ch != " " && coalescable?
      raw_insert(@cx, @cy, ch)
      rec = @undo[@undo.length - 1]
      rec[3] = rec[3] + ch
      @cx += 1
      @modified = true
      @redo = []
      return
    end
    insert_text(ch)
    end_group if ch == " "
  end

  def split_line
    insert_text("\n")
    end_group
  end

  def backspace
    return if @cx == 0 && @cy == 0
    if @cx > 0
      delete_range(@cx - 1, @cy, @cx, @cy)
    else
      prev_len = line(@cy - 1).length
      delete_range(prev_len, @cy - 1, 0, @cy)
    end
    end_group
  end

  def delete_forward
    if @cx < current_line.length
      delete_range(@cx, @cy, @cx + 1, @cy)
    elsif @cy < @lines.length - 1
      delete_range(@cx, @cy, 0, @cy + 1)
    end
    end_group
  end

  # ---- undo ----

  def end_group
    @group_closed = true
  end

  def undo
    rec = @undo.pop
    return false if rec.nil?
    apply(rec, true)
    @redo.push(rec)
    end_group
    true
  end

  def redo
    rec = @redo.pop
    return false if rec.nil?
    apply(rec, false)
    @undo.push(rec)
    end_group
    true
  end

  # ---- highlight cache support ----

  # Which lines' cached tokens went stale since the last call, as a list of
  # indexes or :all when the line count itself changed (every index past
  # the edit shifted, so a list would have to name most of the file
  # anyway). Clears as it reports, the same read-and-clear shape
  # gfx_take_dirty uses in C.
  def take_dirty
    return_all = @dirty_all
    out = return_all ? :all : @dirty
    @dirty = []
    @dirty_all = false
    out
  end

  private

  def coalescable?
    return false if @group_closed
    rec = @undo[@undo.length - 1]
    return false if rec.nil?
    return false unless rec[0] == :ins
    return false unless rec[3].index("\n").nil?
    rec[2] == @cy && rec[1] + rec[3].length == @cx
  end

  def push_undo(rec)
    @undo.push(rec)
    @undo.shift if @undo.length > UNDO_MAX
    @redo = []
    @group_closed = false
  end

  def apply(rec, inverse)
    type, x, y, text, cx, cy = rec
    insert = (type == :ins) ? !inverse : inverse
    if insert
      raw_insert(x, y, text)
    else
      ex, ey = end_of(x, y, text)
      raw_delete(x, y, ex, ey)
    end
    if inverse
      set_cursor(cx, cy)
    elsif type == :ins
      ex, ey = end_of(x, y, text)
      set_cursor(ex, ey)
    else
      set_cursor(x, y)
    end
    @modified = true
  end

  def end_of(x, y, text)
    parts = text.split("\n", -1)
    return [x, y] if parts.empty?
    return [x + text.length, y] if parts.length == 1
    [parts[parts.length - 1].length, y + parts.length - 1]
  end

  def mark_dirty(i)
    @dirty << i unless @dirty.include?(i)
  end

  def mark_dirty_all
    @dirty_all = true
  end
end
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /home/norfolkh/os
cat v2/apps/editor/buffer.rb v2/tools/test_editor.rb | \
  ./v2/components/mruby/build/host/bin/mruby -
```

Expected: PASS — every line `ok`, final line `all passed`, exit 0.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/editor/buffer.rb v2/tools/test_editor.rb
git commit -m "$(cat <<'EOF'
v2: editor text buffer with undo/redo

Every mutation goes through two primitives -- insert text at a
position, delete a range -- so a newline needs no special case and undo
has two record types to invert rather than six. A typed run of
non-space characters coalesces into one record, so undo steps back by
word.

Records carry only the affected text, not a copy of the buffer: an app
VM runs in a fixed mruby pool, and 200 snapshots of a file is not a
cost this runtime should pay for a feature this ordinary.

Calls no acid_* binding, so it tests headless under the host mruby.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Selection, clipboard and find in `Buffer`

**Files:**
- Modify: `v2/apps/editor/buffer.rb`
- Modify: `v2/tools/test_editor.rb`

**Interfaces:**
- Consumes: `Buffer` from Task 3.
- Produces: `mark_set?`, `toggle_mark`, `clear_mark`, `selection_range` (`[sx, sy, ex, ey]` or `nil`), `selected_text` (`String` or `nil`), `delete_selection`, `copy`, `cut`, `paste`, `clipboard`, `find(query, from_x, from_y)` (`[x, y]` or `nil`).

- [ ] **Step 1: Write the failing test**

Insert into `v2/tools/test_editor.rb`, immediately before the `# ---- done` block:

```ruby
group("Buffer: selection")

b = Buffer.new(["abcd"])
b.set_cursor(1, 0)
b.toggle_mark
b.set_cursor(3, 0)
eq(b.selection_range, [1, 0, 3, 0], "mark before cursor")
eq(b.selected_text, "bc", "selected text on one line")

b = Buffer.new(["abcd"])
b.set_cursor(3, 0)
b.toggle_mark
b.set_cursor(1, 0)
eq(b.selection_range, [1, 0, 3, 0], "mark after cursor normalises")
eq(b.selected_text, "bc", "selected text is the same either way")

b = Buffer.new(["one", "two", "three"])
b.set_cursor(1, 0)
b.toggle_mark
b.set_cursor(2, 2)
eq(b.selected_text, "ne\ntwo\nth", "selection spans lines")

b = Buffer.new(["abc"])
b.set_cursor(1, 0)
b.toggle_mark
eq(b.selection_range, nil, "an empty selection is no selection")
b.toggle_mark
eq(b.mark_set?, false, "toggle_mark clears an existing mark")

group("Buffer: clipboard")

b = Buffer.new(["hello world"])
b.set_cursor(0, 0)
b.toggle_mark
b.set_cursor(5, 0)
eq(b.copy, true, "copy reports success")
eq(b.clipboard, "hello", "copy takes the selected text")
eq(b.lines, ["hello world"], "copy leaves the buffer alone")

b = Buffer.new(["hello world"])
b.set_cursor(0, 0)
b.toggle_mark
b.set_cursor(6, 0)
eq(b.cut, true, "cut reports success")
eq(b.lines, ["world"], "cut removes the selection")
eq(b.mark_set?, false, "cut clears the mark")
b.undo
eq(b.lines, ["hello world"], "cut undoes as one step")

b = Buffer.new(["ab"])
b.set_cursor(2, 0)
b.toggle_mark
b.set_cursor(0, 0)
b.cut
b.set_cursor(0, 0)
eq(b.paste, true, "paste reports success")
eq(b.lines, ["ab"], "paste puts it back")

b = Buffer.new(["xy"])
b.set_cursor(1, 0)
eq(b.paste, false, "paste with an empty clipboard is a no-op")

b = Buffer.new(["one", "two"])
b.set_cursor(0, 0)
b.toggle_mark
b.set_cursor(3, 1)
b.cut
eq(b.lines, [""], "cutting everything leaves one empty line")
b.set_cursor(0, 0)
b.paste
eq(b.lines, ["one", "two"], "pasting multi-line text restores the lines")

group("Buffer: find")

b = Buffer.new(["alpha beta", "gamma", "beta delta"])
eq(b.find("beta", 0, 0), [6, 0], "find forward on the first line")
eq(b.find("beta", 7, 0), [0, 2], "find continues onto later lines")
eq(b.find("alpha", 0, 2), [0, 0], "find wraps to the top")
eq(b.find("zzz", 0, 0), nil, "find reports no match")
eq(b.find("", 0, 0), nil, "find on an empty query is nil")
eq(b.find("beta", 1, 0), [6, 0], "find matches later on the cursor's own line")
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /home/norfolkh/os
cat v2/apps/editor/buffer.rb v2/tools/test_editor.rb | \
  ./v2/components/mruby/build/host/bin/mruby -
```

Expected: FAIL — `undefined method 'toggle_mark'`.

- [ ] **Step 3: Write the implementation**

In `v2/apps/editor/buffer.rb`, add `@mark_x = nil`, `@mark_y = nil` and `@clipboard = ""` to `initialize`, add `:clipboard` to the `attr_reader`, and add these public methods before the `private` keyword:

```ruby
  # ---- selection ----
  #
  # A mark, not shift-and-arrow: Shift is resolved into the character at
  # translate time (hal_input_sim.cpp), so a shifted arrow is
  # indistinguishable from a plain one and shift-selection cannot be
  # implemented at all here. Setting a mark and then moving is the same
  # idea reached by the one road that's open.

  def mark_set?
    !@mark_y.nil?
  end

  def toggle_mark
    if mark_set?
      clear_mark
    else
      @mark_x = @cx
      @mark_y = @cy
    end
  end

  def clear_mark
    @mark_x = nil
    @mark_y = nil
  end

  # [sx, sy, ex, ey] in document order, or nil when there's no mark or the
  # mark is exactly on the cursor -- so no caller has to ask which end
  # came first, and none has to special-case a zero-width span.
  def selection_range
    return nil unless mark_set?
    return nil if @mark_x == @cx && @mark_y == @cy
    if @mark_y < @cy || (@mark_y == @cy && @mark_x < @cx)
      [@mark_x, @mark_y, @cx, @cy]
    else
      [@cx, @cy, @mark_x, @mark_y]
    end
  end

  def selected_text
    r = selection_range
    return nil if r.nil?
    sx, sy, ex, ey = r
    return line(sy)[sx, ex - sx] if sy == ey
    parts = [line(sy)[sx, line(sy).length - sx]]
    i = sy + 1
    while i < ey
      parts << line(i)
      i += 1
    end
    parts << line(ey)[0, ex]
    parts.join("\n")
  end

  def delete_selection
    r = selection_range
    return false if r.nil?
    sx, sy, ex, ey = r
    clear_mark
    delete_range(sx, sy, ex, ey)
    end_group
    true
  end

  # ---- clipboard ----
  #
  # App-local. A clipboard shared with the Terminal and File Manager would
  # be a kernel service with its own ownership and lifetime questions;
  # this is the version that earns its keep today.

  def copy
    t = selected_text
    return false if t.nil?
    @clipboard = t
    true
  end

  def cut
    return false unless copy
    delete_selection
  end

  def paste
    return false if @clipboard.nil? || @clipboard.length == 0
    delete_selection if mark_set?
    insert_text(@clipboard)
    end_group
    true
  end

  # ---- find ----

  # Searches forward from (from_x, from_y), wrapping to the top of the
  # buffer exactly once, and returns [x, y] or nil. It wraps because a
  # query that only appears above the cursor still has to be findable --
  # scanning to the end and stopping would report "not found" for text
  # plainly on screen.
  def find(query, from_x, from_y)
    return nil if query.nil? || query.length == 0
    n = @lines.length
    i = 0
    while i <= n
      y = (from_y + i) % n
      start = (i == 0) ? from_x : 0
      hit = line(y).index(query, start)
      return [hit, y] unless hit.nil?
      i += 1
    end
    nil
  end
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /home/norfolkh/os
cat v2/apps/editor/buffer.rb v2/tools/test_editor.rb | \
  ./v2/components/mruby/build/host/bin/mruby -
```

Expected: PASS — `all passed`.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/editor/buffer.rb v2/tools/test_editor.rb
git commit -m "$(cat <<'EOF'
v2: editor selection, clipboard and find

A mark rather than shift-and-arrow: Shift is resolved into the
character at translate time, so a shifted arrow is indistinguishable
from a plain one and shift-selection cannot be built here at all.

selection_range normalises to document order so nothing downstream has
to ask which end came first. find wraps once, because a query that only
appears above the cursor still has to be findable.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Rewire `editor.rb` onto `Buffer`, resize to 420×280

No new UI in this task — the editor must behave exactly as it does today, on the new foundation, at the new size. That isolation is the point: if something breaks here, it's the rewiring, not a feature.

**Files:**
- Modify: `v2/apps/editor.rb`
- Modify: `v2/apps/editor.app.toml`
- Modify: `v2/apps/file_manager.rb:195-197` (`EDITOR_W`/`EDITOR_H`)

**Interfaces:**
- Consumes: `Buffer` from Tasks 3–4.
- Produces: `EditorApp` with `@buf` (a `Buffer`), `@scroll_x`, `@scroll_y`, and the layout constants below, which Tasks 6–11 draw against.

- [ ] **Step 1: Update the manifest and the File Manager's copy of the size**

`v2/apps/editor.app.toml`:

```toml
name = Editor
w = 420
h = 280
desc = Text editor for app source
multi = true
libs = editor/buffer.rb, editor/hl.rb, editor/cmdbar.rb, editor/touch.rb
```

In `v2/apps/file_manager.rb`, update the two constants that mirror it:

```ruby
  EDITOR_W = 420
  EDITOR_H = 280
```

- [ ] **Step 2: Rewrite the layout constants and state**

In `v2/apps/editor.rb`, replace the constants block:

```ruby
  # Must match editor.app.toml and kernel_layout.h's KERNEL_TITLE_BAR_H.
  # 420x280 gives 25 lines of 65 columns; the old 240x170 gave 14 of 35,
  # which is a viewer more than an editor. The screen is 640x360, so two
  # of these still fit side by side.
  WINDOW_W = 420
  WINDOW_H = 280
  TITLE_BAR_H = 16
  LINE_H = 10
  CHAR_W = 6

  # The status line moved to the bottom of the window: command mode (Task
  # 6) raises its strip above it, and a command surface that grows upward
  # from the bottom edge doesn't push the text you're looking at around.
  STATUS_Y = WINDOW_H - LINE_H
  TEXT_Y = TITLE_BAR_H

  GUTTER_CHARS = 4
  GUTTER_W = GUTTER_CHARS * CHAR_W
  TEXT_X = GUTTER_W + 2
```

and the `visible_*` helpers:

```ruby
  def visible_lines
    (STATUS_Y - TEXT_Y) / LINE_H
  end

  def visible_cols
    (WINDOW_W - TEXT_X) / CHAR_W
  end
```

- [ ] **Step 3: Route every edit and cursor read through `Buffer`**

Replace `on_create`'s state setup and `load_file`/`save_file`:

```ruby
  def on_create
    arg = acid_launch_arg
    @path = arg.empty? ? DEFAULT_FILE : arg
    @buf = Buffer.new(read_lines)
    @scroll_y = 0
    @scroll_x = 0
    @message = nil
  end

  def read_lines
    f = File.open(@path, "r")
    text = f.read
    f.close
    text.split("\n")
  rescue
    [""]
  end

  def save_file
    f = File.open(@path, "w")
    # Trailing newline, not just lines joined by one -- POSIX text files
    # end in one, and this app regularly saves real source files under
    # fsroot/App (the live v2/apps symlink): saving without it was
    # confirmed live to strip an existing app.rb's final newline on every
    # save, which is diff noise against git history for no reason.
    f.write(@buf.lines.join("\n") + "\n")
    f.close
    @buf.mark_saved
    @message = "saved"
    true
  rescue
    @message = "save failed"
    false
  end
```

Delete the now-unused `@lines`, `@cx`, `@cy`, `@saved_flash`, `current_line`, `move_cursor`, `insert_char`, `split_line` and `backspace` methods. Replace `on_key`:

```ruby
  def on_key(code, pressed)
    return unless pressed
    @message = nil
    if code == AcidKeys::UP
      @buf.move(0, -1)
    elsif code == AcidKeys::DOWN
      @buf.move(0, 1)
    elsif code == AcidKeys::LEFT
      @buf.move(-1, 0)
    elsif code == AcidKeys::RIGHT
      @buf.move(1, 0)
    elsif code == AcidKeys::ENTER
      @buf.split_line
    elsif code == AcidKeys::BACKSPACE
      @buf.backspace
    elsif code == AcidKeys::DELETE
      @buf.delete_forward
    elsif code == AcidKeys::TAB
      # Two spaces, not a tab character: every width calculation in this
      # app counts characters, and a literal tab would make the cursor
      # column and the drawn column disagree from that point on.
      @buf.insert_text("  ")
    elsif code >= 32 && code <= 126
      @buf.insert_char(code.chr)
    end
    ensure_scroll
    redraw
  end

  def ensure_scroll
    if @buf.cy < @scroll_y
      @scroll_y = @buf.cy
    elsif @buf.cy >= @scroll_y + visible_lines
      @scroll_y = @buf.cy - visible_lines + 1
    end
    if @buf.cx < @scroll_x
      @scroll_x = @buf.cx
    elsif @buf.cx >= @scroll_x + visible_cols
      @scroll_x = @buf.cx - visible_cols + 1
    end
  end
```

Replace the drawing methods to read from `@buf` and use the new geometry:

```ruby
  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    draw_gutter
    draw_lines
    draw_cursor
    draw_status
    acid_draw_window_border
  end

  def draw_status
    acid_fill_rect(0, STATUS_Y, WINDOW_W, LINE_H, BG_COLOR)
    left = @message ? @message : (file_label + (@buf.modified? ? " *" : ""))
    right = "#{@buf.cy + 1},#{@buf.cx + 1}  #{@buf.line_count}L"
    acid_draw_text(left[0, 28], 2, STATUS_Y + 1, STATUS_COLOR, BG_COLOR)
    acid_draw_text(right, WINDOW_W - right.length * CHAR_W - 2, STATUS_Y + 1,
                   STATUS_COLOR, BG_COLOR)
  end

  def draw_gutter
    acid_fill_rect(0, TEXT_Y, GUTTER_W, STATUS_Y - TEXT_Y, GUTTER_BG)
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      break if idx >= @buf.line_count
      num = (idx + 1).to_s
      acid_draw_text(num, GUTTER_W - num.length * CHAR_W - 2,
                     TEXT_Y + i * LINE_H + 1, GUTTER_COLOR, GUTTER_BG)
      i += 1
    end
  end

  def draw_lines
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      y = TEXT_Y + i * LINE_H
      acid_fill_rect(TEXT_X, y, WINDOW_W - TEXT_X, LINE_H, BODY_BG)
      if idx < @buf.line_count
        text = @buf.line(idx)
        visible_text = text[@scroll_x, visible_cols] || ""
        acid_draw_text(visible_text, TEXT_X, y + 1, TEXT_COLOR, BODY_BG)
      end
      i += 1
    end
  end

  def draw_cursor
    row = @buf.cy - @scroll_y
    return if row < 0 || row >= visible_lines
    col = @buf.cx - @scroll_x
    return if col < 0 || col >= visible_cols
    x = TEXT_X + col * CHAR_W
    y = TEXT_Y + row * LINE_H
    acid_fill_rect(x, y + LINE_H - 2, CHAR_W, 2, CURSOR_COLOR)
  end
```

`ESCAPE` is deliberately not handled yet — it becomes command mode in Task 6, and leaving it inert for one task is better than wiring save to a key that's about to mean something else.

- [ ] **Step 4: Verify in the sim**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build && ./v2/sim/build/acidos_sim
```

Open Menu → Editor. Check, by hand:
- the window is visibly larger and shows about 25 lines;
- typing inserts, Enter splits, Backspace joins at the start of a line, Delete joins at the end;
- arrows move and wrap between lines;
- the status line sits at the **bottom** and shows `notes.txt *` once you type, with line/column and the line count on the right;
- typing past the right edge scrolls horizontally and typing past the bottom scrolls vertically;
- File Manager → App → `editor.rb` opens this editor on that file at the new size.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/editor.rb v2/apps/editor.app.toml v2/apps/file_manager.rb
git commit -m "$(cat <<'EOF'
v2: editor on top of Buffer, at 420x280

Same behaviour, new foundation and new size: every edit and cursor read
now goes through Buffer, so undo and selection have somewhere to live,
and the window shows 25 lines of 65 columns instead of 14 of 35.

The status line moves to the bottom edge, where command mode's strip
will grow upward from it rather than pushing the text around. ESC is
deliberately inert for now rather than saving, since it is about to
mean something else.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Command mode — the strip and the immediate commands

**Files:**
- Create: `v2/apps/editor/cmdbar.rb`
- Modify: `v2/apps/editor.rb`

**Interfaces:**
- Consumes: `Buffer` (Tasks 3–4); `EditorApp`'s `WINDOW_W`, `WINDOW_H`, `LINE_H`, `CHAR_W`, `STATUS_Y`, `TEXT_Y`, `@buf`, `@message`, `ensure_scroll`, `redraw`, `save_file`.
- Produces: `module EditorCmd` mixed into `EditorApp`, with `cmd_open`, `cmd_close`, `cmd_active?`, `cmd_key(code)` (returns `true` when it consumed the key), `draw_cmd_strip`, and `CMD_ROWS`.

- [ ] **Step 1: Write the module**

Create `v2/apps/editor/cmdbar.rb`:

```ruby
# Command mode: ESC raises a strip of single-key commands over the bottom
# of the text area, one keypress runs one, ESC closes it.
#
# Not a menu bar, and not Ctrl/Alt chords, because neither is reachable
# here: hal_input_sim.cpp's translate_scancode drops Ctrl, Alt and the
# function keys outright, and the hardware target is a tablet with no
# keyboard to press them on anyway. A strip of plain letters is the one
# surface that works identically typed and tapped -- see the design doc.
#
# Prompts (find, goto, save-as) arrive in Task 7; this task is the strip
# and the commands that act immediately.
module EditorCmd
  # Three fixed rows, not a paging list: at 65 columns everything fits
  # with room to spare, and a fixed strip means a command never moves,
  # which is what makes the tap targets (Task 11) learnable.
  CMD_ROWS = [
    [["s", "save"], ["a", "save-as"], ["q", "close"], ["!", "run"],
     ["u", "undo"], ["r", "redo"]],
    [["x", "cut"], ["c", "copy"], ["v", "paste"], ["m", "mark"],
     ["/", "find"], ["n", "next"]],
    [["g", "goto"], ["t", "top"], ["b", "bottom"], ["h", "hilite"],
     ["p", "prev"], ["?", "keys"]]
  ]

  CMD_CELL_CHARS = 10
  CMD_BG = 0x123322    # THEME_PANEL's documented button-hover shade
  CMD_KEY_COLOR = 0x00FF66   # THEME_HARD
  CMD_TEXT_COLOR = 0xD4E6DB  # THEME_TEXT

  def cmd_strip_y
    STATUS_Y - CMD_ROWS.length * LINE_H
  end

  def cmd_active?
    @cmd_open ? true : false
  end

  def cmd_open
    @cmd_open = true
    @message = nil
  end

  def cmd_close
    @cmd_open = false
  end

  # Returns true when the key was consumed, so on_key can stop. An
  # unrecognised key closes the strip rather than sitting there swallowing
  # input -- a command surface you can get stuck inside is worse than one
  # you occasionally have to reopen.
  def cmd_key(code)
    return false unless cmd_active?
    if code == AcidKeys::ESCAPE
      cmd_close
      return true
    end
    cmd_close
    return true if code < 32 || code > 126
    cmd_run(code.chr)
    true
  end

  def cmd_run(ch)
    if ch == "s"
      save_file
    elsif ch == "u"
      @message = "nothing to undo" unless @buf.undo
    elsif ch == "r"
      @message = "nothing to redo" unless @buf.redo
    elsif ch == "m"
      @buf.toggle_mark
      @message = @buf.mark_set? ? "mark set" : "mark cleared"
    elsif ch == "c"
      @message = @buf.copy ? "copied" : "no selection"
    elsif ch == "x"
      @message = @buf.cut ? "cut" : "no selection"
    elsif ch == "v"
      @message = @buf.paste ? "pasted" : "clipboard empty"
    elsif ch == "t"
      @buf.set_cursor(0, 0)
    elsif ch == "b"
      @buf.set_cursor(0, @buf.line_count - 1)
    else
      @message = "no command '#{ch}'"
    end
    ensure_scroll
  end

  def draw_cmd_strip
    y = cmd_strip_y
    acid_fill_rect(0, y, WINDOW_W, CMD_ROWS.length * LINE_H, CMD_BG)
    row = 0
    while row < CMD_ROWS.length
      cells = CMD_ROWS[row]
      i = 0
      while i < cells.length
        x = 2 + i * CMD_CELL_CHARS * CHAR_W
        acid_draw_text(cells[i][0], x, y + row * LINE_H + 1, CMD_KEY_COLOR, CMD_BG)
        acid_draw_text(cells[i][1], x + 2 * CHAR_W, y + row * LINE_H + 1,
                       CMD_TEXT_COLOR, CMD_BG)
        i += 1
      end
      row += 1
    end
  end
end
```

- [ ] **Step 2: Mix it in and route ESC**

In `v2/apps/editor.rb`, add the include as the first line of the class body:

```ruby
class EditorApp < AcidApp
  include EditorCmd
```

In `on_key`, hand the key to command mode first, and make ESC open it:

```ruby
  def on_key(code, pressed)
    return unless pressed
    return redraw if cmd_key(code)
    @message = nil
    if code == AcidKeys::ESCAPE
      cmd_open
      return redraw
    end
    # ... the rest unchanged ...
```

In `redraw`, draw the strip last of the content, so it sits over the text:

```ruby
    draw_cursor
    draw_cmd_strip if cmd_active?
    draw_status
```

- [ ] **Step 3: Verify in the sim**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build && ./v2/sim/build/acidos_sim
```

Open Menu → Editor and check:
- ESC raises a three-row strip above the status line with green command letters;
- ESC again closes it;
- `ESC s` saves — status shows `saved`, and the `*` disappears;
- type a word, `ESC u` undoes the whole word, `ESC r` redoes it;
- `ESC u` on a fresh file shows `nothing to undo`;
- `ESC m`, arrow right a few times, `ESC c`, move elsewhere, `ESC v` pastes the copied text;
- `ESC t` and `ESC b` jump to the first and last line, scrolling to follow;
- `ESC z` shows `no command 'z'` and closes the strip.

- [ ] **Step 4: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/editor/cmdbar.rb v2/apps/editor.rb
git commit -m "$(cat <<'EOF'
v2: editor command mode on ESC

ESC raises a three-row strip of single-key commands over the bottom of
the text area: save, undo/redo, mark, cut/copy/paste, top/bottom.

Not a menu bar and not Ctrl chords, because neither is reachable --
translate_scancode drops Ctrl, Alt and the function keys, and the
hardware target has no keyboard to press them on. A strip of plain
letters is the one surface that works identically typed and tapped.

Fixed rows rather than a paging list, so a command never changes
position; the tap targets in a later task depend on that.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Prompts — find, find-next/prev, goto, save-as, close confirmation

**Files:**
- Modify: `v2/apps/editor/cmdbar.rb`
- Modify: `v2/apps/editor.rb`

**Interfaces:**
- Consumes: everything from Task 6, plus `Buffer#find` (Task 4).
- Produces: `cmd_prompt_active?`, `cmd_prompt_key(code)`, `draw_cmd_prompt`, and `@last_query` on `EditorApp`.

- [ ] **Step 1: Add prompt state and handling to `EditorCmd`**

In `v2/apps/editor/cmdbar.rb`, add to `cmd_close`:

```ruby
  def cmd_close
    @cmd_open = false
    @prompt = nil
    @prompt_text = nil
  end
```

Add these methods:

```ruby
  # A prompt keeps the strip up and takes text on the status row. The
  # three that need text are find, goto and save-as; everything else acts
  # on the keypress.
  def cmd_prompt_active?
    !@prompt.nil?
  end

  def cmd_prompt_open(kind, label)
    @prompt = kind
    @prompt_label = label
    @prompt_text = ""
    @cmd_open = true
  end

  def cmd_prompt_key(code)
    return false unless cmd_prompt_active?
    if code == AcidKeys::ESCAPE
      cmd_close
    elsif code == AcidKeys::ENTER
      text = @prompt_text
      kind = @prompt
      cmd_close
      cmd_prompt_submit(kind, text)
    elsif code == AcidKeys::BACKSPACE
      @prompt_text = @prompt_text[0, @prompt_text.length - 1] if @prompt_text.length > 0
    elsif code >= 32 && code <= 126
      @prompt_text = @prompt_text + code.chr
    end
    true
  end

  def cmd_prompt_submit(kind, text)
    if kind == :find
      return if text.length == 0
      @last_query = text
      find_from(@buf.cx + 1, @buf.cy)
    elsif kind == :goto
      n = text.to_i
      if n < 1 || n > @buf.line_count
        @message = "no line #{text}"
      else
        @buf.set_cursor(0, n - 1)
      end
    elsif kind == :saveas
      return if text.length == 0
      @path = text
      save_file
    end
    ensure_scroll
  end

  # Search forward from a position, wrapping, and move there. Separate
  # from cmd_prompt_submit so find-next and find-prev reuse it without
  # reopening the prompt.
  def find_from(x, y)
    hit = @buf.find(@last_query, x, y)
    if hit.nil?
      @message = "not found: #{@last_query}"
      return
    end
    @buf.set_cursor(hit[0], hit[1])
    @message = @last_query
  end

  def find_prev_from(x, y)
    # No backward search in Buffer on purpose: with wraparound, the
    # previous match is just "the last match reached by scanning forward
    # from here", and one search direction is one thing to get right.
    return @message = "no search yet" if @last_query.nil?
    best = nil
    pos = [0, 0]
    n = 0
    while n < 10000
      hit = @buf.find(@last_query, pos[0], pos[1])
      break if hit.nil?
      break if !best.nil? && hit[0] == best[0] && hit[1] == best[1]
      break if hit[1] > y || (hit[1] == y && hit[0] >= x)
      best = hit
      pos = [hit[0] + 1, hit[1]]
      n += 1
    end
    if best.nil?
      @message = "no earlier match"
      return
    end
    @buf.set_cursor(best[0], best[1])
    @message = @last_query
  end

  def draw_cmd_prompt
    acid_fill_rect(0, STATUS_Y, WINDOW_W, LINE_H, CMD_BG)
    text = "#{@prompt_label}: #{@prompt_text}_"
    acid_draw_text(text[0, WINDOW_W / CHAR_W - 1], 2, STATUS_Y + 1,
                   CMD_TEXT_COLOR, CMD_BG)
  end
```

Extend `cmd_run` — replace its `else` branch and add the new commands before it:

```ruby
    elsif ch == "/"
      cmd_prompt_open(:find, "find")
    elsif ch == "n"
      if @last_query.nil?
        @message = "no search yet"
      else
        find_from(@buf.cx + 1, @buf.cy)
      end
    elsif ch == "p"
      find_prev_from(@buf.cx, @buf.cy)
    elsif ch == "g"
      cmd_prompt_open(:goto, "line")
    elsif ch == "a"
      cmd_prompt_open(:saveas, "save as")
    elsif ch == "q"
      cmd_quit
    elsif ch == "?"
      @message = "ESC then a letter; see the strip"
    else
```

Add the quit handling:

```ruby
  # Two presses to lose unsaved work, and the second one has to be the
  # same key -- a status-line confirmation rather than a dialog, because
  # this app framework has no dialog concept and a confirmation needs
  # none.
  def cmd_quit
    if !@buf.modified? || @quit_armed
      acid_close_window(acid_my_task)
      return
    end
    @quit_armed = true
    @message = "unsaved -- ESC q again to close"
  end
```

`@quit_armed` must be cleared by anything else the user does. In `cmd_run`, make the first line:

```ruby
  def cmd_run(ch)
    was_armed = @quit_armed
    @quit_armed = false
    @quit_armed = was_armed if ch == "q"
```

- [ ] **Step 2: Route prompt keys first**

In `v2/apps/editor.rb`'s `on_key`, the prompt takes precedence over the strip:

```ruby
    return redraw if cmd_prompt_key(code)
    return redraw if cmd_key(code)
```

Anything that isn't a command also disarms a pending quit — otherwise typing,
moving, and *then* pressing `ESC q` once would close a modified buffer with no
warning. Add it beside the `@message` reset, on the path that runs when neither
handler consumed the key:

```ruby
    @message = nil
    @quit_armed = false
```

and in `redraw`, the prompt replaces the status line when it's up:

```ruby
    draw_cmd_strip if cmd_active?
    if cmd_prompt_active?
      draw_cmd_prompt
    else
      draw_status
    end
```

- [ ] **Step 3: Confirm the window-closing binding exists**

```bash
cd /home/norfolkh/os
grep -n "acid_close_window\|acid_my_task" v2/core/bindings/*.c v2/apps/*.rb
```

Expected: both are registered in `window_binding.c` and already used by `sysmon.rb`. If `acid_my_task` is not present, use whatever `sysmon.rb` passes to `acid_close_window` for its own row and mirror that.

- [ ] **Step 4: Verify in the sim**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build && ./v2/sim/build/acidos_sim
```

Open File Manager → App → `acid_blaster.rb` (a long file) in the Editor, then check:
- `ESC /` shows `find: _` on the status row with the strip still up; type `def`, Enter — the cursor jumps to the first `def` after the cursor and the view scrolls to it;
- `ESC n` repeatedly walks forward through matches and wraps to the top;
- `ESC p` walks back;
- `ESC /`, `zzzz`, Enter shows `not found: zzzz`;
- `ESC g`, `50`, Enter jumps to line 50; `ESC g`, `9999`, Enter shows `no line 9999`;
- `ESC /` then ESC cancels with nothing changed;
- type a character, `ESC q` shows `unsaved -- ESC q again to close`; press an arrow key, then `ESC q` again — it warns again rather than closing;
- `ESC q` twice in a row does close the window;
- `ESC a`, type `v2/fsroot/Tmp/scratch.txt`, Enter — status shows `saved`; confirm with File Manager → Tmp that the file is there.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/editor/cmdbar.rb v2/apps/editor.rb
git commit -m "$(cat <<'EOF'
v2: editor find, goto, save-as and close confirmation

Commands that need text keep the strip up and take it on the status
row. Find wraps; find-prev is the last match reached by scanning
forward, so there is only one search direction to get right.

Closing a modified buffer arms on the first ESC q and closes on the
second, and anything else disarms it. A status-line confirmation rather
than a dialog, because this app framework has no dialog concept.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: `Hl` — the Ruby tokenizer

**Files:**
- Create: `v2/apps/editor/hl.rb`
- Modify: `v2/tools/test_editor.rb`

**Interfaces:**
- Consumes: nothing.
- Produces: `module Hl` with `Hl.tokenize(line)` returning `[[text, color], ...]` covering the whole line in order, and the colour constants `Hl::KEYWORD`, `STRING`, `NUMBER`, `SYMBOL`, `IVAR`, `COMMENT`, `PLAIN`.

- [ ] **Step 1: Write the failing test**

Insert into `v2/tools/test_editor.rb`, before the `raise` at the end:

```ruby
group("Hl: tokenizer")

def toks(line)
  Hl.tokenize(line).map { |t| [t[0], t[1]] }
end

def colors_of(line, word)
  Hl.tokenize(line).each { |t| return t[1] if t[0] == word }
  nil
end

eq(Hl.tokenize("").length, 0, "an empty line has no tokens")

eq(Hl.tokenize("abc").map { |t| t[0] }.join(""), "abc",
   "tokens cover the whole line")
eq(Hl.tokenize("x = foo(1, :bar) # note").map { |t| t[0] }.join(""),
   "x = foo(1, :bar) # note", "tokens cover a busy line exactly")

eq(colors_of("def hi", "def"), Hl::KEYWORD, "def is a keyword")
eq(colors_of("ending = 1", "ending"), Hl::PLAIN,
   "a keyword inside an identifier is not a keyword")
eq(colors_of("x.end", "end"), Hl::KEYWORD, "end after a dot still reads as one")

eq(colors_of("s = \"hi\"", "\"hi\""), Hl::STRING, "double-quoted string")
eq(colors_of("s = 'hi'", "'hi'"), Hl::STRING, "single-quoted string")
eq(colors_of("s = \"a#b\"", "\"a#b\""), Hl::STRING,
   "a # inside a string does not start a comment")
eq(colors_of("s = \"a\\\"b\"", "\"a\\\"b\""), Hl::STRING,
   "an escaped quote does not end the string")

eq(colors_of("x # note", "# note"), Hl::COMMENT, "comment to end of line")
eq(colors_of("# whole", "# whole"), Hl::COMMENT, "whole-line comment")

eq(colors_of("x = :sym", ":sym"), Hl::SYMBOL, "symbol")
eq(colors_of("A::B", "::"), Hl::PLAIN, ":: is not a symbol")
eq(colors_of("A::B", "A"), Hl::SYMBOL, "a constant shares the symbol colour")
eq(colors_of("AcidKeys::ESCAPE", "ESCAPE"), Hl::SYMBOL,
   "the name after :: is a constant, not a symbol")

eq(colors_of("@ivar = 1", "@ivar"), Hl::IVAR, "instance variable")
eq(colors_of("x = 42", "42"), Hl::NUMBER, "number")
eq(colors_of("x = 0xFF66", "0xFF66"), Hl::NUMBER, "hex literal reads as one number")
eq(colors_of("1.upto(3)", "1"), Hl::NUMBER,
   "a number before a method call does not swallow the dot")
eq(colors_of("x = 1.5", "1.5"), Hl::NUMBER, "a decimal keeps its point")
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /home/norfolkh/os
cat v2/apps/editor/buffer.rb v2/apps/editor/hl.rb v2/tools/test_editor.rb 2>/dev/null | \
  ./v2/components/mruby/build/host/bin/mruby -
```

Expected: FAIL — `uninitialized constant Hl`.

- [ ] **Step 3: Write the implementation**

Create `v2/apps/editor/hl.rb`:

```ruby
# Ruby syntax highlighting for the editor: one line in, a list of
# [text, color] runs out, covering the line in order with nothing
# dropped.
#
# Per line on purpose. An edit then invalidates exactly one cache entry
# (Buffer#take_dirty), which is what keeps redraw cheap while typing. The
# cost is that multi-line strings, heredocs and =begin blocks are not
# understood -- getting those right means re-tokenizing from the top of
# the file on every keystroke, which is the wrong trade for the payoff.
# See the design doc.
#
# Calls no acid_* binding, so v2/tools/test_editor.rb runs it headless.
module Hl
  # Not kernel_theme.h's five chrome colours -- the wallpaper's own neon
  # palette (wallpaper_data.h), so highlighted source reads as part of
  # this OS rather than as a generic editor theme dropped into it.
  KEYWORD = 0xFF2D78   # neon pink
  STRING  = 0xFFD400   # yellow
  NUMBER  = 0x00E5FF   # cyan
  SYMBOL  = 0xB026FF   # violet -- THEME_VIOLET, shared with constants
  IVAR    = 0xFF7A00   # orange
  COMMENT = 0x9DAAA3   # THEME_MUTED
  PLAIN   = 0xD4E6DB   # THEME_TEXT

  KEYWORDS = ["def", "end", "class", "module", "if", "elsif", "else",
              "unless", "while", "until", "do", "return", "yield", "nil",
              "true", "false", "self", "and", "or", "not", "begin",
              "rescue", "ensure", "case", "when", "then", "next", "break",
              "attr_reader", "attr_accessor", "require", "include"]

  def self.tokenize(line)
    out = []
    i = 0
    n = line.length
    while i < n
      ch = line[i, 1]
      if ch == "#"
        out << [line[i, n - i], COMMENT]
        i = n
      elsif ch == "\"" || ch == "'"
        stop = string_end(line, i, ch, n)
        out << [line[i, stop - i], STRING]
        i = stop
      elsif ch == ":" && line[i + 1, 1] == ":"
        # Scope resolution, not a symbol. Without this the ":B" of
        # "AcidKeys::ESCAPE" reads as a symbol -- and this codebase's apps
        # are full of exactly that constant.
        out << ["::", PLAIN]
        i += 2
      elsif ch == ":" && ident_start?(line[i + 1, 1])
        j = i + 1
        j += 1 while j < n && ident_char?(line[j, 1])
        out << [line[i, j - i], SYMBOL]
        i = j
      elsif ch == "@"
        j = i + 1
        j += 1 while j < n && ident_char?(line[j, 1])
        out << [line[i, j - i], IVAR]
        i = j
      elsif ident_start?(ch)
        j = i
        j += 1 while j < n && ident_char?(line[j, 1])
        word = line[i, j - i]
        out << [word, word_color(word)]
        i = j
      elsif digit?(ch)
        j = number_end(line, i, n)
        out << [line[i, j - i], NUMBER]
        i = j
      else
        j = i
        j += 1 while j < n && plain_at?(line, j)
        j = i + 1 if j == i
        out << [line[i, j - i], PLAIN]
        i = j
      end
    end
    out
  end

  # Index just past the closing quote, or the end of the line for a string
  # that never closes -- an unterminated quote is a line you are still
  # typing, and colouring the rest of it as a string is what makes that
  # visible.
  def self.string_end(line, start, quote, n)
    j = start + 1
    while j < n
      c = line[j, 1]
      if c == "\\"
        j += 2
        next
      end
      return j + 1 if c == quote
      j += 1
    end
    n
  end

  # Digits, underscores and hex letters, plus a '.' only when a digit
  # follows it -- so "1.5" is one number but "1.upto" is a number and then
  # a method call.
  def self.number_end(line, start, n)
    j = start
    while j < n
      c = line[j, 1]
      if digit?(c) || c == "_" || hex_char?(c)
        j += 1
      elsif c == "." && digit?(line[j + 1, 1])
        j += 1
      else
        break
      end
    end
    j
  end

  def self.word_color(word)
    return KEYWORD if KEYWORDS.include?(word)
    return SYMBOL if (word[0, 1] =~ /[A-Z]/)
    PLAIN
  end

  def self.plain_at?(line, j)
    c = line[j, 1]
    return false if c == "#" || c == "\"" || c == "'" || c == "@"
    return false if ident_start?(c) || digit?(c)
    return false if c == ":" && line[j + 1, 1] == ":"
    return false if c == ":" && ident_start?(line[j + 1, 1])
    true
  end

  def self.ident_start?(c)
    return false if c.nil?
    (c =~ /[A-Za-z_]/) ? true : false
  end

  def self.ident_char?(c)
    return false if c.nil?
    (c =~ /[A-Za-z0-9_]/) ? true : false
  end

  def self.digit?(c)
    return false if c.nil?
    (c =~ /[0-9]/) ? true : false
  end

  def self.hex_char?(c)
    return false if c.nil?
    (c =~ /[xXa-fA-F]/) ? true : false
  end
end
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /home/norfolkh/os
cat v2/apps/editor/buffer.rb v2/apps/editor/hl.rb v2/tools/test_editor.rb | \
  ./v2/components/mruby/build/host/bin/mruby -
```

Expected: PASS — `all passed`.

Note on `hex_char?`: it is only ever consulted from inside a number that already started with a digit, so `0xFF66` reads as one token while a bare `abc` never does. If the `1.upto(3)` or `x = 0xFF66` assertions fail, that interaction is where to look.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/editor/hl.rb v2/tools/test_editor.rb
git commit -m "$(cat <<'EOF'
v2: ruby tokenizer for the editor

One line in, [text, color] runs out, covering the line exactly. Per
line on purpose: an edit invalidates one cache entry, which is what
keeps redraw cheap while typing. Multi-line strings and heredocs are
therefore not understood, documented in the module and the design doc.

Colours are the wallpaper's neon palette rather than the five chrome
colours, so highlighted source reads as part of this OS instead of a
generic editor theme dropped into it.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Draw highlighted lines

**Files:**
- Modify: `v2/apps/editor.rb`

**Interfaces:**
- Consumes: `Hl.tokenize` (Task 8), `Buffer#take_dirty` (Task 3), `EditorCmd#cmd_run` (Task 6).
- Produces: `hl_tokens(index)`, `hl_invalidate`, `@hl_on`.

- [ ] **Step 1: Add the cache and the highlighted draw path**

In `v2/apps/editor.rb`'s `on_create`, after `@buf` is built:

```ruby
    # On for Ruby, off for anything else -- a .txt file has no syntax to
    # show and colouring prose at random is worse than leaving it alone.
    # ESC h overrides it for this window.
    @hl_on = @path.end_with?(".rb")
    @hl_cache = []
```

Add these methods:

```ruby
  # Tokens for one line, tokenized on first sight and kept until that
  # line changes. Buffer reports what went stale (take_dirty); :all means
  # the line count itself moved, so every cached index past the edit is
  # wrong and the cheapest correct answer is to start over.
  def hl_tokens(index)
    hl_invalidate
    cached = @hl_cache[index]
    return cached unless cached.nil?
    toks = Hl.tokenize(@buf.line(index))
    @hl_cache[index] = toks
    toks
  end

  def hl_invalidate
    dirty = @buf.take_dirty
    return if dirty == []
    if dirty == :all
      @hl_cache = []
      return
    end
    dirty.each { |i| @hl_cache[i] = nil }
  end

  def draw_hl_line(index, y)
    col = 0
    limit = @scroll_x + visible_cols
    hl_tokens(index).each do |t|
      text = t[0]
      start_col = col
      col += text.length
      next if col <= @scroll_x
      break if start_col >= limit
      cut = @scroll_x - start_col
      cut = 0 if cut < 0
      vis = text[cut, text.length - cut]
      screen_col = start_col + cut - @scroll_x
      room = visible_cols - screen_col
      vis = vis[0, room] if vis.length > room
      acid_draw_text(vis, TEXT_X + screen_col * CHAR_W, y + 1, t[1], BODY_BG)
    end
  end
```

Replace the drawing half of `draw_lines`:

```ruby
      if idx < @buf.line_count
        if @hl_on
          draw_hl_line(idx, y)
        else
          visible_text = @buf.line(idx)[@scroll_x, visible_cols] || ""
          acid_draw_text(visible_text, TEXT_X, y + 1, TEXT_COLOR, BODY_BG)
        end
      end
```

`hl_invalidate` must also run when highlighting is off, or a buffer edited with it off keeps stale tokens when it comes back on. Add to the top of `draw_lines`:

```ruby
    hl_invalidate
```

- [ ] **Step 2: Draw the selection behind the text**

In `draw_lines`, fill the selected span before drawing the line. Add above the `if idx < @buf.line_count` block:

```ruby
      sel = selection_span(idx)
      unless sel.nil?
        sx = sel[0] - @scroll_x
        ex = sel[1] - @scroll_x
        sx = 0 if sx < 0
        ex = visible_cols if ex > visible_cols
        acid_fill_rect(TEXT_X + sx * CHAR_W, y, (ex - sx) * CHAR_W, LINE_H, SEL_BG) if ex > sx
      end
```

Add the helper and the colour constant:

```ruby
  SEL_BG = 0x123322    # THEME_PANEL's documented button-hover shade,
                       # the same highlight file_manager.rb uses for its
                       # selected row

  # The [start_col, end_col] of the selection on one line, or nil. A line
  # fully inside a multi-line selection runs to its own length plus one,
  # so the newline it swallowed is visible as a highlighted cell rather
  # than the selection appearing to stop short at the end of the text.
  def selection_span(index)
    r = @buf.selection_range
    return nil if r.nil?
    sx, sy, ex, ey = r
    return nil if index < sy || index > ey
    from = (index == sy) ? sx : 0
    to = (index == ey) ? ex : @buf.line(index).length + 1
    return nil if to <= from
    [from, to]
  end
```

Note `SEL_BG` is drawn before the text, and `acid_draw_text` paints its own background — so selected text inside the fill still shows `BODY_BG` behind the glyphs. That is acceptable and matches how `file_manager.rb` renders its selected row; the fill on either side of each glyph is what reads as the highlight.

- [ ] **Step 3: Wire the toggle**

In `v2/apps/editor/cmdbar.rb`'s `cmd_run`, replace the `h` placeholder (add it before the `else`):

```ruby
    elsif ch == "h"
      @hl_on = !@hl_on
      @message = @hl_on ? "highlight on" : "highlight off"
```

In `draw_status`, show the state — replace the `right` line:

```ruby
    right = "#{@buf.cy + 1},#{@buf.cx + 1}  #{@buf.line_count}L#{@hl_on ? '  hl' : ''}"
```

- [ ] **Step 4: Verify in the sim**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build && ./v2/sim/build/acidos_sim
```

File Manager → App → `acid_blaster.rb`, then check:
- keywords are pink, strings yellow, numbers cyan, constants and symbols violet, `@ivars` orange, comments muted grey;
- the status line shows `hl` on the right;
- `ESC h` turns colour off and back on, and the `hl` marker follows;
- typing on a line recolours **that** line immediately and leaves the rest alone;
- pressing Enter to split a line recolours correctly below the split;
- scrolling right with a long line keeps the colours aligned with the text;
- open `v2/fsroot/Home/notes.txt` (Menu → Editor with no argument): highlighting is off, `hl` is absent, and `ESC h` still turns it on;
- `ESC m` then arrow around: the selection fills behind the text across several lines.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/editor.rb v2/apps/editor/cmdbar.rb
git commit -m "$(cat <<'EOF'
v2: draw highlighted source and the selection

Tokens are cached per line and dropped only for the lines Buffer
reports stale, so typing recolours one line rather than the file. A
line-count change drops the whole cache, since every index past the
edit moved.

Each token is drawn as its own clipped run, which horizontal scrolling
needs anyway. On by default for .rb, off otherwise, ESC h toggles.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: Touch editing

**Files:**
- Create: `v2/apps/editor/touch.rb`
- Modify: `v2/apps/editor.rb`

**Interfaces:**
- Consumes: `Buffer` (Tasks 3–4), `EditorCmd` (Tasks 6–7), `EditorApp`'s layout constants and `ensure_scroll`.
- Produces: `module EditorTouch` with `editor_touch(x, y, pressed)`, mixed into `EditorApp` and called from `on_touch`.

- [ ] **Step 1: Write the module**

Create `v2/apps/editor/touch.rb`:

```ruby
# Touch editing: tap to place the cursor, drag to select, tap the gutter
# for a whole line, tap the status line for command mode, tap a command
# on the strip to run it.
#
# The reference editor this one learns from is keyboard-only. The
# hardware target is a tablet, so this is the half that actually matters
# there -- and it is the same command surface the keyboard drives, not a
# second one bolted on.
#
# Press and release are tracked explicitly rather than with the usual
# press-once-per-hold guard, because dragging is exactly the case that
# guard exists to suppress. The one-shot targets -- the strip and the
# status line -- keep the guard, via @tap_consumed.
module EditorTouch
  def editor_touch(x, y, pressed)
    unless pressed
      @touch_down = false
      @tap_consumed = false
      return false
    end

    fresh = !@touch_down
    @touch_down = true

    # One-shot targets first, and only on the press that started the
    # hold: holding a finger on the strip must not re-run its command
    # every frame.
    if fresh
      return true if touch_command_strip(x, y)
      return true if touch_status(x, y)
    end
    return false if @tap_consumed
    return false if y < TEXT_Y || y >= STATUS_Y

    if x < GUTTER_W
      return touch_gutter(y, fresh)
    end
    touch_text(x, y, fresh)
  end

  def touch_command_strip(x, y)
    return false unless cmd_active?
    return false if cmd_prompt_active?
    sy = cmd_strip_y
    return false if y < sy || y >= STATUS_Y
    row = (y - sy) / LINE_H
    cells = CMD_ROWS[row]
    return true if cells.nil?
    col = (x - 2) / (CMD_CELL_CHARS * CHAR_W)
    cell = cells[col]
    if cell.nil? || col < 0
      cmd_close
    else
      cmd_close
      cmd_run(cell[0])
    end
    @tap_consumed = true
    true
  end

  def touch_status(x, y)
    return false if y < STATUS_Y
    ( x )
    if cmd_active?
      cmd_close
    else
      cmd_open
    end
    @tap_consumed = true
    true
  end

  def touch_gutter(y, fresh)
    row = (y - TEXT_Y) / LINE_H + @scroll_y
    return false if row >= @buf.line_count
    if fresh
      @buf.clear_mark
      @buf.set_cursor(0, row)
      @buf.toggle_mark
    end
    # Extending down the gutter selects whole lines: the mark stays at the
    # start of the first, the cursor runs to the end of the current.
    @buf.set_cursor(@buf.line(row).length, row)
    ensure_scroll
    true
  end

  def touch_text(x, y, fresh)
    row = (y - TEXT_Y) / LINE_H + @scroll_y
    row = @buf.line_count - 1 if row >= @buf.line_count
    col = (x - TEXT_X) / CHAR_W + @scroll_x
    col = 0 if col < 0
    if fresh
      @buf.clear_mark
      @buf.set_cursor(col, row)
      @drag_from = [@buf.cx, @buf.cy]
      ensure_scroll
      return true
    end
    # Continuing a hold: this is a drag, so anchor a mark at wherever the
    # press landed and let the cursor run.
    unless @buf.mark_set?
      @buf.set_cursor(@drag_from[0], @drag_from[1])
      @buf.toggle_mark
    end
    @buf.set_cursor(col, row)
    ensure_scroll
    true
  end
end
```

- [ ] **Step 2: Mix it in**

In `v2/apps/editor.rb`:

```ruby
class EditorApp < AcidApp
  include EditorCmd
  include EditorTouch
```

and add the handler:

```ruby
  def on_touch(x, y, pressed)
    @message = nil if pressed
    redraw if editor_touch(x, y, pressed)
  end
```

- [ ] **Step 3: Verify in the sim**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build && ./v2/sim/build/acidos_sim
```

Activate the window **once**, then drive with the mouse only. Check:
- clicking a character in the text places the cursor exactly there (test near the right edge of a long line after scrolling horizontally);
- pressing and dragging across several lines selects them, and the selection fill follows;
- releasing leaves the selection in place, and `ESC c` then copies it;
- clicking a gutter line number selects that whole line; dragging down the gutter extends by lines;
- clicking the status line opens command mode; clicking it again closes it;
- with the strip up, clicking `undo` in it undoes;
- holding a finger on the strip's `undo` cell does **not** undo repeatedly.

- [ ] **Step 4: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/editor/touch.rb v2/apps/editor.rb
git commit -m "$(cat <<'EOF'
v2: touch editing in the editor

Tap to place the cursor, drag to select, tap the gutter for a line, tap
the status line for command mode, tap a command on the strip to run it.

Press and release are tracked explicitly rather than with the usual
press-once-per-hold guard: dragging is exactly the case that guard
exists to suppress. The one-shot targets keep it.

The reference editor is keyboard-only; on a tablet this is the half
that matters, and it drives the same command surface rather than a
second one bolted on.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Run the edited app

**Files:**
- Modify: `v2/apps/editor/cmdbar.rb`

**Interfaces:**
- Consumes: `acid_spawn_app(path, w, h, arg)` (already bound, used by `file_manager.rb:210`); `EditorCmd` (Tasks 6–7).
- Produces: `cmd_run_file`.

- [ ] **Step 1: Add the command**

In `v2/apps/editor/cmdbar.rb`, add to `cmd_run` before the `else`:

```ruby
    elsif ch == "!"
      cmd_run_file
```

and add:

```ruby
  DEFAULT_RUN_W = 240
  DEFAULT_RUN_H = 170

  # Save, then launch the file being edited as a live app window. The
  # whole point of this editor is that fsroot/App is a real symlink to
  # v2/apps, so a change to an app's source is live on its next launch
  # with no rebuild step -- this makes that a two-keystroke loop instead
  # of a trip through the File Manager.
  def cmd_run_file
    unless @path.end_with?(".rb")
      @message = "not a ruby file"
      return
    end
    return unless save_file
    w, h = run_geometry
    acid_spawn_app(@path, w, h, "")
    @message = "running #{file_label}"
  end

  # The app's own manifest decides its window size, exactly as the Menu
  # and File Manager do. A .rb with no manifest beside it is still worth
  # running -- it just gets a default-sized window.
  def run_geometry
    toml = @path[0, @path.length - 3] + ".app.toml"
    f = File.open(toml, "r")
    text = f.read
    f.close
    w = nil
    h = nil
    text.split("\n").each do |line|
      line = line.strip
      next if line.empty? || line.start_with?("#")
      eq = line.index("=")
      next unless eq
      key = line[0, eq].strip
      value = line[eq + 1, line.length - eq - 1].strip
      w = value.to_i if key == "w"
      h = value.to_i if key == "h"
    end
    return [DEFAULT_RUN_W, DEFAULT_RUN_H] if w.nil? || h.nil? || w < 1 || h < 1
    [w, h]
  rescue
    [DEFAULT_RUN_W, DEFAULT_RUN_H]
  end
```

- [ ] **Step 2: Verify in the sim**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build && ./v2/sim/build/acidos_sim
```

Check:
- File Manager → App → `piano.rb`, change something visible in it (e.g. a label string), then `ESC !` — the file saves and a Piano window opens showing the change;
- `ESC !` on `v2/fsroot/Home/notes.txt` (Menu → Editor with no argument) shows `not a ruby file`;
- a `.rb` with no manifest beside it still opens, at 240×170. Test by `ESC a`-saving the current buffer to `v2/fsroot/Tmp/mini.rb` first;
- an app that is a singleton (`multi = false`) focuses its existing window rather than opening a second, unchanged from the Menu's behaviour.

Then restore whatever you changed in `piano.rb`:

```bash
cd /home/norfolkh/os && git checkout v2/apps/piano.rb
```

- [ ] **Step 3: Run the whole test suite and commit**

```bash
cd /home/norfolkh/os
cc -Wall -Wextra -o v2/sim/build/test_lib_paths \
   v2/tools/test_lib_paths.c v2/core/vm_host/lib_paths.c && ./v2/sim/build/test_lib_paths
cat v2/apps/editor/buffer.rb v2/apps/editor/hl.rb v2/tools/test_editor.rb | \
   ./v2/components/mruby/build/host/bin/mruby -
cmake --build v2/sim/build
```

Expected: both suites print `all passed`, the build is clean.

```bash
git add v2/apps/editor/cmdbar.rb
git commit -m "$(cat <<'EOF'
v2: run the edited app from the editor

ESC ! saves and launches the file being edited, sized from its own
manifest. fsroot/App is a real symlink to v2/apps, so an app's source
is already live on its next launch -- this makes that a two-keystroke
loop instead of a trip through the File Manager.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review notes

**Spec coverage.** Every section of the design doc maps to a task: per-app libraries → Tasks 1–2; command mode → Task 6; prompts and the close confirmation → Task 7; undo/redo → Task 3; selection and clipboard → Task 4; highlighting → Tasks 8–9; touch → Task 10; Run → Task 11; the status line → Tasks 5, 7 and 9; the window resize → Task 5; the test plan → Tasks 1, 3, 4, 8.

**Two spec details deliberately implemented differently, and why:**

- The spec says `ESC ?` opens a full key list. Task 7 makes it a one-line status message pointing at the strip instead. The strip is already on screen showing every command when `?` is pressed, so a second panel restating it would be the only screen in this editor that exists to describe another screen. If you want the prose version, it is a small addition to Task 7 rather than a change to it.
- The spec describes `p` (find-previous) as a peer of `n`. Task 7 implements it by scanning forward from the top and keeping the last match before the cursor, rather than adding a backward search to `Buffer`. One search direction is one thing to get right, and `Buffer#find` already wraps.

**Known interaction to watch.** `acid_draw_text` paints its own background, so selected text drawn over `SEL_BG` keeps `BODY_BG` behind each glyph — the highlight reads as the fill between and around characters. Task 9 Step 2 documents this. If it looks wrong in practice, the fix is a `SEL_BG` background argument on the text call for selected runs, which is a one-line change in `draw_hl_line` and `draw_lines`.
