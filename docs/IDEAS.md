# Rave-OS Ideas Journal

Not a plan, not a spec -- just a running list of things worth considering
later. Entries here haven't been brainstormed, scoped, or committed to;
they're a starting point for a future `superpowers:brainstorming` session,
not a queue.

- **`font.c`'s `?` and `/` glyphs render as zero-width.** Found
  2026-08-20 while verifying the concurrency scheduler's kill-timeout
  path: editing `/BIN/PAINT` live in the in-OS `EDIT` window showed
  `MOUSE-DOWN? IF` and `EDIT /BIN/PAINT` with no visible `?`/`/` at
  all -- the characters are genuinely present in the buffer (the file
  still parses and runs correctly; `EDIT /BIN/PAINT` still resolves
  the right path), it's purely a rendering gap. Cosmetic only, but a
  real trap for anyone reading a script back through `EDIT` or `cat`
  expecting to see punctuation that's actually there.

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

- ~~**A real path for user-written system programs, not just Forth
  scripts.**~~ Done, 2026-08-17 -- shipped as the paint/sprite designer +
  Forth graphics words feature. Real-hardware testing (2026-08-18) then
  found and fixed five bugs sharing one root cause -- see
  `docs/BUILD_LOG.md`'s 2026-08-18 entry -- and added a real filename
  field to SAVE. PAINT now opens visibly, stays live (cursor, canvas,
  color-picking) for the whole session, and saves multiple named
  sprites to `/HOME/<name>`, confirmed on real hardware.

- **PAINT still needs to become an actual sprite editor, not just a
  proof of concept.** Raised 2026-08-17, alongside the real-hardware
  bug the 2026-08-18 pass fixed (see above) -- these are the parts of
  that same report that weren't about the bug itself and are still
  open:
  - **Should launch from SHELL, not just via `RUN PAINT` typed into a
    FORTH console window.** It already lives in `/BIN`; SHELL should be
    able to run it directly the way it runs any other `/BIN` script,
    without requiring the user to first open a separate FORTH window.
  - **The 8-color bottom-strip palette is too small and always visible,
    wasting canvas space.** Wanted instead: a real palette-chooser
    popup/dialog that appears on demand, not a fixed strip -- almost
    certainly needs a bigger-than-8 color set once it's not fighting for
    screen space.
  - **Need the ability to delete colours** from whatever the palette
    becomes -- not just pick from a fixed set.
  - **Needs LOAD, not just SAVE.** SAVE gained a real filename field
    2026-08-18 (multiple named sprites under `/HOME/<name>`, no longer
    just one fixed path), but it's still write-only from the app's own
    perspective -- there's no way to re-open a previously saved sprite
    back into the canvas. A real sprite editor needs both halves of
    that round-trip.
  - This adds up to substantially more than a bug-fix pass -- likely its
    own `superpowers:brainstorming` cycle (palette-chooser UI is a real
    design question, not just an implementation detail) rather than a
    quick patch to the existing plan.

- **Naming: "FORTH" and "Rave-OS" read as two different things and
  shouldn't.** Raised 2026-08-17 -- the FORTH console window and the OS
  as a whole are conceptually the same project, but the UI and docs
  currently use both names in ways that read as if they're separate.
  Worth a pass to pick one consistent name and apply it across window
  titles/branding/docs, rather than leaving it ambiguous which name
  refers to what.

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

- **A boot-time config screen: CPU count / RAM for the VM.** Raised
  2026-08-18. Needs unpacking before it's actionable: CPU/RAM for a
  QEMU *launch* is a host-side flag (`-smp`, `-m`) decided before the
  VM starts, so a config screen for that would be a launcher
  tool/script living outside Rave-OS entirely -- different from an
  in-OS boot config screen, which would need real SMP/RAM-detection
  support this kernel doesn't have at all yet. Which one is actually
  wanted needs deciding first. Tied to a second open question, also
  raised the same day: what Rave-OS's actual minimum viable
  RAM/CPU footprint even is -- worth answering before building a
  config screen around it.
