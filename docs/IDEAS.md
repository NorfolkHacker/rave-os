# Rave-OS Ideas Journal

Not a plan, not a spec -- just a running list of things worth considering
later. Entries here haven't been brainstormed, scoped, or committed to;
they're a starting point for a future `superpowers:brainstorming` session,
not a queue.

- ~~**`touched[WIN_KIND_PAINT]` doesn't actually track `paint.grid[][]`
  content changes -- may only redraw live by incidental window
  overlap.**~~ Done, 2026-08-22 -- root-caused with
  `superpowers:systematic-debugging` before fixing. Confirmed live: real
  mouse-driven painting (`PLOOP`) always redrew correctly, because the
  cursor's own per-frame damage footprint sits on whatever cell it's
  painting -- not because grid changes were tracked. The real gap was
  narrower than first suspected: a typed `PIXEL` command reaching a
  canvas cell with *no* mouse movement and *no* overlapping window
  (reproduced with `PAINT` then `0 0 3 PIXEL` at FORTH's console,
  targeting a cell outside FORTH's own window bounds) failed to redraw
  before this fix. Fixed by adding `paint.grid_generation` (bumped
  unconditionally by `forth_hook_pixel()`) and comparing it in
  `touched[WIN_KIND_PAINT]`'s diff -- the exact same "generation counter,
  only ever increases" pattern `console_output.h`'s own `generation`
  field already established for the same problem. Re-ran the isolated
  repro case: renders correctly now. See `docs/BUILD_LOG.md`'s entry for
  the same date.

- ~~**`font.c` has no glyphs for `(`/`)` -- render invisible, same class
  of bug as the `?`/`/` one below.**~~ Done, 2026-08-22 -- added
  `G_LPAREN`/`G_RPAREN` and their `case`s in `font_glyph()`'s switch,
  same shape as the `?` fix. `(RUN FAILED)` now renders with visible
  parentheses, confirmed via a live SHELL `RUN NOPE` screendump. Added
  `(`/`)` to `test_font.c`'s covered set as a regression guard.

- ~~**`font.c`'s `?` and `/` glyphs render as zero-width.**~~ Done,
  2026-08-20. Root cause turned out to be narrower than the original
  report: `/` already had a correct, non-blank `G_SLASH` glyph in
  `kernel/gfx/font.c` all along (confirmed with a host-built probe
  dumping its 7x5 bitmap) -- it's just a sparse single-pixel diagonal,
  easy to mistake for blank at a glance. `?` was the real bug: no
  `G_QMARK` glyph existed anywhere and no `case '?'` in `font_glyph()`'s
  switch, so it silently fell through to the default space glyph.
  Fixed by adding `G_QMARK` and its case. Added
  `kernel/tests/test_font.c` (host-built, no QEMU needed) as a
  regression guard -- it checks every character `font_glyph()` claims
  to cover renders as non-blank, so a future glyph silently missing
  its switch case fails loudly instead of rendering as invisible.

- ~~**256 colour.**~~ Done, 2026-08-22 -- resolved in favor of richer
  palette use within the existing true-color pipeline (the hardware was
  never actually limited to 256 colors; a real indexed mode would have
  been a deliberate retro constraint, not a technical necessity, and
  wasn't what was wanted). Every window kind now gets its own
  `accent_color` (border + hovered-control highlight) instead of all
  five sharing one identical green -- FORTH keeps the original
  androidacid.com accent, FILES/SHELL/EDITOR/PAINT reuse colors already
  vetted in PAINT's own sprite palette (blue/orange/purple/red) rather
  than inventing new ones. Body/titlebar backgrounds stay the same
  neutral dark shade for every window; only the bright accent role
  varies. See `docs/BUILD_LOG.md`'s entry for the same date.

- ~~**Audio: an 8-channel SID-like chip emulation.**~~ Done, 2026-08-22 --
  but only the foundational piece. Decomposed with the user into three
  independent sub-projects (see
  `docs/superpowers/specs/2026-08-22-sb16-audio-driver-design.md`): (A) a
  real hardware audio output path, (B) the actual multi-voice SID-like
  software synthesizer this entry originally described (oscillators,
  waveforms, filter, envelope, 8 voices), (C) a control surface/
  note-sequencing language to drive it. This entry's original premise --
  "no audio subsystem exists in this kernel at all yet (no sound card
  driver, no PC speaker output, nothing)" -- is no longer true: (A)
  shipped as a `kernel/drivers/sb16.c`/`.h` Sound Blaster 16 driver
  (DSP reset/detect handshake, 8237 DMA controller channel 1 programmed
  for single-cycle 8-bit playback, an IRQ5 completion handler) and a new
  bare Forth word `BEEP` that plays one hardcoded 400Hz square-wave test
  tone through it -- proof this kernel can drive real (emulated) hardware
  audio output end to end, verified via headless QEMU inspecting the
  actual WAV samples produced. (B), the real synthesizer, and (C), any
  control surface for it, are both still entirely unbuilt -- separate
  future specs, not started, and not what shipped here. See
  `docs/BUILD_LOG.md`'s entry for the same date.

  (B) has now also shipped, 2026-08-23 (see
  `docs/superpowers/specs/2026-08-23-sid-synth-design.md` and
  `docs/BUILD_LOG.md`'s entry for the same date): `kernel/audio/synth.c`/
  `.h` now implements the real oscillators, envelope, and mixing this
  entry originally asked for -- 8 independent voices, 4 waveforms
  (pulse/saw/triangle/noise), an 88-key ona-number note table addressing
  exact DDS phase increments for 22050Hz, a full ADSR envelope state
  machine per voice, and an 8-voice software mixer, streamed continuously
  through the SB16 driver via auto-init DMA and driven manually from the
  FORTH console with 7 new words (`VOICE`/`WAVE`/`DUTY`/`ONA`/`ADSR`/
  `GATE-ON`/`GATE-OFF`). (C) -- a real control surface / note-sequencing
  language beyond those manual words -- and the resonant filter this
  entry also originally described both remain unbuilt, separate future
  work.

- ~~**Upgrade the FILES window.**~~ Done, 2026-08-16 -- both candidates
  this entry originally floated shipped the same day. Multi-select +
  clipboard-style CUT/COPY/PASTE landed in FILES (plus `mv`/`cp` in
  SHELL, both built on new `fs_move`/`fs_copy_file` primitives). Then a
  real in-OS text editor landed separately: a new multi-line `editor.c`
  widget, a fourth `WIN_KIND_EDITOR` window, and SHELL's `EDIT <path>`
  command -- `/BIN` scripts and `/ETC/CONFIG` can now be authored without
  scratch-image byte editing. See `docs/BUILD_LOG.md`'s entries for both
  and their design specs under `docs/superpowers/specs/`.

- ~~**Home/End/Delete key support.**~~ Done, 2026-08-22 -- `keyboard.c`
  now decodes all three as extended pseudo-characters, and the editor
  handles them: `editor_move_home()`/`editor_move_end()` jump to the
  start/end of the current line, `editor_delete_forward()` deletes at
  the cursor. Scoped to the editor only (the "obvious first consumer"
  this entry called out) -- `console_input.c`'s single-line fields
  (Forth console, Shell console, FILES rename, PAINT filename) still
  only handle arrow keys. See `docs/BUILD_LOG.md`'s entry for the same
  date.

- ~~**A struct-based refactor of `kernel.c`'s five window-pipeline
  functions.**~~ Done, 2026-08-22 -- `move_window_content()`,
  `draw_files_group()`, `draw_window_by_index()`, `draw_scene()`, and
  `update_and_present()` now take a single `struct window_content *`
  instead of their own hand-widened parameter lists (`update_and_present()`
  alone went from ~35 params to 22). See `docs/BUILD_LOG.md`'s entry for
  the same date.

- ~~**A real path for user-written system programs, not just Forth
  scripts.**~~ Done, 2026-08-17 -- shipped as the paint/sprite designer +
  Forth graphics words feature. Real-hardware testing (2026-08-18) then
  found and fixed five bugs sharing one root cause -- see
  `docs/BUILD_LOG.md`'s 2026-08-18 entry -- and added a real filename
  field to SAVE. PAINT now opens visibly, stays live (cursor, canvas,
  color-picking) for the whole session, and saves multiple named
  sprites to `/HOME/<name>`, confirmed on real hardware.

- ~~**PAINT still needs to become an actual sprite editor, not just a
  proof of concept.**~~ Done, 2026-08-22 -- all four sub-projects raised
  2026-08-17 landed the same day (2026-08-22): SHELL launch, LOAD,
  and finally the palette-chooser popup with delete-colors below.
  - ~~**Should launch from SHELL, not just via `RUN PAINT` typed into a
    FORTH console window.**~~ Done, 2026-08-22 -- SHELL now intercepts
    `RUN <target>` the same way it already intercepted `EDIT`, sharing
    a new `handle_run_command()` with the FORTH console instead of
    duplicating the launch logic. See `docs/BUILD_LOG.md`'s entry for
    the same date.
  - ~~**The 8-color bottom-strip palette is too small and always
    visible, wasting canvas space.**~~ Done, 2026-08-22 -- replaced by a
    click-to-open popup showing 16 colors (up from 8), overlaying the
    canvas's own top-left corner. See `docs/BUILD_LOG.md`'s entry for
    the same date.
  - ~~**Need the ability to delete colours** from whatever the palette
    becomes -- not just pick from a fixed set.~~ Done, 2026-08-22 --
    right-click any popup swatch to hide/restore it (session-only; the
    master 16-color list and saved sprites' own meaning never change).
  - ~~**Needs LOAD, not just SAVE.**~~ Done, 2026-08-22 -- a LOAD button
    sits beside SAVE (same filename field, split row, no window
    resize), reads `/HOME/<name>` back into the grid only if it's
    exactly 256 bytes, and clamps each byte to a valid palette index in
    case of a bad/hand-edited file. See `docs/BUILD_LOG.md`'s entry for
    the same date.

- ~~**Naming: "FORTH" and "Rave-OS" read as two different things and
  shouldn't.**~~ Resolved, 2026-08-22 -- re-audited and found this
  already fixed by later, unrelated work: every window (FORTH included)
  now titles itself `"RAVE-OS <KIND>"` (`draw_forth_group()` etc. via
  `windows[i].title`), the boot messages all read `Rave-OS: ...`, and
  the desktop banner reads `RAVE-OS`. The start menu's bare item labels
  (`FORTH`, `FILES`, `SHELL`, ...) are a normal, consistent menu
  convention, not a naming clash. The one concrete example raised
  during this pass -- a desktop icon still labeled plain `FORTH` --
  turned out to be dead code that never renders at all; see the new
  entry below. Nothing left to fix here.

- ~~**`desktop_icon.c`'s functions are dead code -- nothing calls
  them.**~~ Resolved, 2026-08-22 -- deleted `kernel/gui/desktop_icon.c`/
  `.h` and their `kernel/Makefile` entries, since nothing called them
  and the start menu had already fully replaced desktop icons as the
  way to launch apps. Also dropped the now-dangling references to
  `desktop_icon.c` from three comments in `shell.c`/`startmenu.c`/
  `startmenu.h` that cited it as a design-pattern example. See
  `docs/BUILD_LOG.md`'s entry for the same date. (Re-wiring desktop
  icons back in as a live feature was the other option raised when
  this was found -- not chosen; would be its own task if wanted later.)

- ~~**`kmain()`'s loop doesn't run at all while a Forth script's own
  loop blocks.**~~ Done, 2026-08-19/20 -- shipped as a real cooperative
  scheduler, not another one-off hook: a fiber/coroutine primitive
  (`context_switch.asm`), a fixed program-slot table (`scheduler.c`),
  automatic yield insertion at every `BEGIN...UNTIL` back-edge, `RUN`
  spawning a non-blocking scheduled program, and a close-then-timeout
  kill path for a window whose program ignores a graceful close. Real
  process isolation (ring 3/paging/syscalls) was explicitly scoped
  out as "Rave-OS v2 scale" -- programs still share one address space,
  ring 0, cooperative-only. See `docs/superpowers/specs/2026-08-19-concurrency-design.md`
  and `docs/BUILD_LOG.md`'s entry for the same date for the full design
  and verification.

- ~~**FILES' plain start-menu launcher shows stale data.**~~ Done,
  2026-08-18 -- `STARTMENU_ITEM_FILES` now routes through
  `open_files_at()` too, passing `cwd` as its own target to re-list in
  place. Verified headlessly: a sprite saved from a live PAINT session
  shows up immediately from the plain `FILES` item, no navigate-away-
  and-back workaround needed.

- ~~**A boot-time config screen: CPU count / RAM for the VM.**~~
  Dropped, 2026-08-22 -- CPU/RAM for a QEMU launch is a host-side flag
  (`-smp`, `-m`) decided before the VM even starts, so it's already
  fully addressable from the command line (`qemu-system-i386 -smp 2 -m
  256 ...`) with zero need for Rave-OS itself to have any say in it. An
  in-OS equivalent (real SMP/RAM-detection) was the only version that
  would've actually needed kernel work, and isn't wanted.

- ~~**A real cross-compilation toolchain, not host `gcc -m32`.**~~
  Done, 2026-08-22 -- built a real `i686-elf-gcc`/binutils cross-compiler
  via the classic OSDev recipe, now `kernel/Makefile`'s default (full
  replacement, not an optional second path). See
  `docs/BUILD_LOG.md`'s entry for the same date.

- **Different resolution settings, not just the one hardcoded
  640x480.** Raised 2026-08-20. `boot/stage2.asm`'s `VBE_MODE` is
  hardcoded to `0x112` (640x480, 32bpp) -- the kernel has no path to
  request or fall back across multiple VBE modes at all. Worth
  revisiting once there's an actual reason to want more screen space
  (PAINT's palette-chooser redesign above is one candidate), but a real
  feature here means both stage2 querying/selecting among multiple VBE
  modes and the whole GUI layer's widgets (`kernel.c`'s window-pipeline
  functions, `taskbar.c`, `startmenu.c`, etc.) no longer assuming one
  fixed screen size -- likely a bigger lift than it first sounds.
