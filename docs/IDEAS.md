# Rave-OS Ideas Journal

Not a plan, not a spec -- just a running list of things worth considering
later. Entries here haven't been brainstormed, scoped, or committed to;
they're a starting point for a future `superpowers:brainstorming` session,
not a queue.

- **256 colour.** The backbuffer/graphics pipeline is currently a fixed
  32-bit packed-RGB backdrop built around one near-black + acid-green
  palette (`graphics.c`, `kernel.c`'s `backdrop_color()`). Revisit
  whether this means richer palette use within the existing true-color
  pipeline, or something closer to a real indexed/paletted colour mode
  -- not yet decided, just flagged.

- **Audio: an 8-channel SID-like chip emulation.** C64 SID (MOS 6581/8580)
  as the inspiration -- its oscillators/waveforms/filter/envelope model --
  but with 8 voices instead of the real chip's 3. No audio subsystem
  exists in this kernel at all yet (no sound card driver, no PC speaker
  output, nothing) -- this would be a new subsystem from scratch, not an
  extension of something existing.

- ~~**Upgrade the FILES window.**~~ Done, 2026-08-16 -- both candidates
  this entry originally floated shipped the same day. Multi-select +
  clipboard-style CUT/COPY/PASTE landed in FILES (plus `mv`/`cp` in
  SHELL, both built on new `fs_move`/`fs_copy_file` primitives). Then a
  real in-OS text editor landed separately: a new multi-line `editor.c`
  widget, a fourth `WIN_KIND_EDITOR` window, and SHELL's `EDIT <path>`
  command -- `/BIN` scripts and `/ETC/CONFIG` can now be authored without
  scratch-image byte editing. See `docs/BUILD_LOG.md`'s entries for both
  and their design specs under `docs/superpowers/specs/`.

- **Home/End/Delete key support.** `keyboard.c` only decodes the 4 arrow
  keys as extended pseudo-characters (`KEY_UP`/`KEY_DOWN`/`KEY_LEFT`/
  `KEY_RIGHT`, `keyboard.h`) -- Home, End, and Delete aren't decoded at
  all yet. Surfaced as a real gap while designing the text editor
  (2026-08-16): the editor's own spec explicitly deferred these three
  keys rather than add new scancode decoding with unverified
  headless-QEMU support in the same stage. Would need `keyboard.c`'s
  extended-scancode table extended, then wiring into whichever widgets
  want them (the editor being the obvious first consumer, but
  `console_input.c`'s fields could use Home/End too).

- **A struct-based refactor of `kernel.c`'s five window-pipeline
  functions.** `move_window_content()`, `draw_window_by_index()`,
  `draw_scene()`, and `update_and_present()` have each been widened three
  times now across separate stages (FILES' selection-mask type change,
  FILES' three new clipboard buttons, then the text editor's `ed`/
  `save_btn`) -- `update_and_present()` is up to roughly 30 parameters.
  Flagged as worth a real cleanup (e.g. a `struct window_content` bundle
  passed by pointer) by two separate final-branch reviews now, both of
  which explicitly deferred it rather than bundle an unrelated
  refactor into a feature's own fix wave. A fifth window kind would
  widen all five signatures again -- worth doing before that happens,
  not after.

- **A real path for user-written system programs, not just Forth
  scripts.** The actual question: how would someone write and run
  something like a word processor or a paint program on Rave-OS, as
  opposed to a `/BIN` Forth script (`RUN`) or the shell's own built-in
  commands (fixed, compiled into the kernel, not user-extensible at
  all)? This directly revisits a gap RUN's own design deliberately
  deferred rather than solved: "a real machine-code loader for /BIN is a
  huge, separate undertaking (no paging, no process isolation, no binary
  format defined)" (`docs/BUILD_LOG.md`, the RUN stage). Whatever this
  becomes -- a real loadable-binary format with process isolation, a
  richer Forth with enough GUI/windowing words to build an app in, some
  third thing -- it's a substantial architectural undertaking, not a
  bounded task; treat it as its own full `superpowers:brainstorming` ->
  spec -> plan cycle when picked up, not a quick add-on to SHELL or
  FILES.
