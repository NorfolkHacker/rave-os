# Extracting kmain()'s per-frame loop into a callable function

## Purpose

`docs/IDEAS.md`'s "Real userspace" entry names real per-window event
delivery (a `SYS_WAIT_EVENT` syscall) as the largest remaining piece
of sub-project (C). Investigation before this spec found the real
blocker: once `kmain()` calls `enter_ring3()`, it never regains
control -- its own per-frame update loop (mouse/keyboard polling,
redrawing the desktop and the five built-in apps) simply stops running
for good, the same limitation every ring-3 proof this session has
already relied on. A `SYS_WAIT_EVENT` syscall's CPL0 handler would
need to re-drive that same per-frame work itself, in a loop, until an
event lands on the calling ring-3 program's window -- but the loop is
currently ~918 lines inlined directly inside `kmain()`, not something
any other code can call.

This spec covers only the extraction itself: turning that loop body
into a standalone, callable function with **zero behavior change**.
It does not add event delivery, does not add input routing for
`WIN_KIND_RING3` (which has none today), and does not touch
`syscall.h` at all. Those are separate, later pieces this
investigation deliberately did not bundle in -- extraction is a real
prerequisite, valuable and independently verifiable on its own, and
much lower-risk to land by itself than as part of a larger change.

## Scope

**In scope:**
- Promoting every `kmain()`-local variable the loop body (currently
  lines ~2511-3429) actually references to file scope (`static`),
  matching the existing precedent `windows[]`/`z_order[]`/`mx`/`my`
  already set (all three already file-scope, confirmed by reading the
  code directly, not assumed).
- Moving the loop body itself -- including its own per-iteration
  `old_*` damage-tracking snapshot variables (confirmed, by reading
  the code directly: these have no cross-iteration persistence
  requirement at all, each one is declared and initialized fresh from
  live state at the top of every iteration, then compared against
  that same live state, possibly mutated by that iteration's own
  input handling, at the bottom -- so they move into the new
  function's body completely unchanged, no `static` needed) -- into a
  new function, `kmain_frame(void)`.
- `kmain()` itself shrinks to its existing one-time setup (unchanged)
  followed by `for (;;) { kmain_frame(); }`.

**Out of scope, deliberately:**
- **Any new return value, output parameter, or event-reporting
  mechanism on `kmain_frame()`.** It stays `void`, exactly mirroring
  today's loop body's own lack of one. Whatever `SYS_WAIT_EVENT`
  eventually needs from it is a separate design question for whichever
  slice adds `WIN_KIND_RING3` input routing (today's `WIN_KIND_RING3`
  has zero click/drag/keyboard handling -- see
  `docs/superpowers/specs/2026-08-29-ring3-window-design.md`'s Out of
  Scope).
- **Any change to what the loop actually does.** This is a pure
  mechanical extraction; if a line's behavior would need to change to
  make the extraction work, that is itself a red flag to stop and
  reconsider, not something to route around silently.
- **Struct-parameter threading instead of file-scope statics.**
  Investigated and rejected: nothing in this codebase needs more than
  one instance of this desktop state (no multi-window-manager-instance
  use case exists, unlike e.g. `struct window_content` which already
  exists precisely because `draw_window_by_index()` legitimately needs
  to be called once per window). File-scope `static` is a smaller,
  more mechanical diff for the same result, and matches the pattern
  three of the loop's own variables (`windows[]`/`z_order[]`/`mx`/`my`)
  already use.

## Design

### Which variables move

Every `kmain()`-local declared before the loop (lines ~2018-2053) that
the loop body (2511-3429) actually references must move to file scope.
The definitive list is whatever the compiler says is undefined once
the loop body is cut out -- a natural, self-checking safety net for
this specific kind of mechanical relocation, not something to
hand-enumerate and risk getting subtly wrong in a document that could
drift from the code. Roughly (confirmed by reading the declaration
block directly): `w`/`h`, `dragging_window`, `taskbar_hovered`,
`menu_hovered_item`, `cursor_color`, the five apps' own widget structs
(`co`/`ci`/`hist`/`vm`, `shell_co`/`shell_ci`/`shell_hist`/`sh`,
`bar`/`menu`/`splash`, FILES' five buttons + `clipboard` +
`name_input`, EDITOR's `ed`/`save_btn`), `cwd`, `file_entries[]`,
`files_selected_mask`. A handful of the block's other declarations
(`prev_left_held`/`prev_right_held`, `ata_status`/`fs_status`,
`editor_path`/`editor_title`) are used only during one-time setup, not
inside the loop -- these stay exactly where they are, in `kmain()`,
untouched.

### The extraction itself

```c
static void kmain_frame(void) {
    /* the entire current loop body, lines 2511-3429, unchanged */
}

void kmain(void) {
    /* all existing one-time setup, unchanged */
    for (;;) {
        kmain_frame();
    }
}
```

`kmain_frame()` is `static` (internal linkage) -- nothing outside
`kernel.c` calls it yet; that changes only when a later slice adds a
syscall path that does.

### Verified-safe: no control-flow obstacles

Checked directly (not assumed): the loop body contains exactly one
`break;`, confirmed by reading its surrounding code to be inside a
nested `for (idx = 0; idx < file_entry_count; idx++)` search loop (in
the FILES rename-handling block), not the outer frame loop -- moving
it into a separate function does not change its meaning. Zero
`continue`/`goto` anywhere in the loop body.

## Testing

**Host-buildable:** no host test exists for this (there never has been
one for `kmain()`'s GUI loop -- it's not pure logic, it's the whole
desktop). The cross-compiler build itself is the first real check:
every relocated variable the loop body needs will surface as an
"undeclared identifier" compile error if missed, and a clean build is
necessary (not sufficient) proof the relocation is complete.

**Headless QEMU, this session's most rigorous verification yet.**
Every prior slice's proof was new, additive code with nothing existing
to regress -- this change touches the one code path all five existing
built-in apps depend on for every interaction, so "boots to a normal
desktop" alone is not enough. Verification instead scripts a fixed
interaction sequence via the QEMU monitor's `sendkey`/`mouse_move`/
`mouse_button` commands (the same monitor primitives already used
earlier this session to prove the mouse driver works, independent of
any host display), capturing a screendump after each step:

1. Boot to the desktop (baseline).
2. Open the start menu, click FORTH -- confirms window-open/z-order.
3. Type a short Forth line into its console input, confirms keyboard
   routing and `console_input`/`console_history` state.
4. Drag the FORTH window by its titlebar to a new position -- confirms
   drag state and the per-window `touched[]` diffing.
5. Open PAINT from the start menu, click to draw a pixel -- confirms
   `paint.grid_generation` tracking and a different app's own
   click-routing path.
6. Open FILES, click into a directory -- confirms `cwd`/
   `file_entries[]`/`files_selected_mask` handling.
7. Open EDITOR and SHELL similarly, confirming their own widget state.

Run this exact sequence **twice**: once against the current,
unmodified `main` (the "before" set, captured first, on this spec's
own worktree before any code changes), once against the extracted
code (the "after" set) -- then compare each numbered step's screendump
between the two runs. Any pixel difference at any step is a real
regression; this refactor's entire purpose is producing an identical
"after" set, so anything less means a variable's relocation missed a
subtlety the compiler couldn't catch (e.g. an initializer that
implicitly depended on `kmain()`'s one-time setup order in a way file
scope's static-zero-init changes). A final regression check --
`kmain()` at boot showing a normal desktop, no earlier temporary-proof
scaffolding left behind -- is not needed here the way it was for every
prior slice, since this change has no temporary code to remove; the
before/after comparison above already covers the "did anything break"
question more rigorously than a single boot screendump could.
