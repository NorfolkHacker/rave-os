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

- ~~**A real path for user-written system programs, not just Forth
  scripts.**~~ Done, 2026-08-17 -- shipped as the paint/sprite designer +
  Forth graphics words feature. Real-hardware testing (2026-08-18) then
  found and fixed five bugs sharing one root cause -- see
  `docs/BUILD_LOG.md`'s 2026-08-18 entry -- and added a real filename
  field to SAVE. PAINT now opens visibly, stays live (cursor, canvas,
  color-picking) for the whole session, and saves multiple named
  sprites to `/HOME/<name>`, confirmed on real hardware.

- **`kmain()`'s loop doesn't run at all while a Forth script's own
  loop blocks -- a real concurrency model, not another one-off hook.**
  The root cause behind all five bugs the 2026-08-18 PAINT pass fixed:
  every fix so far has been giving `PLOOP` its own copy of one more
  piece of what `kmain()`'s loop normally does (`REFRESH`,
  `PALETTE-PICK`, `SAVE-PICK`), one at a time, as each gap was
  discovered. That's sustainable for one script but doesn't scale --
  the real fix is some way for a script's `BEGIN...UNTIL` to yield
  control back to `kmain()` periodically instead of looping forever
  inside one call. Feasible without paging or process isolation:
  `forth.c` already compiles words into real bytecode
  (`OP_LITERAL`/`OP_CALL_WORD`/`OP_BRANCH`/...) with a persistent
  `struct forth_vm` holding VM state across calls -- the right shape
  for a resumable "run N steps, return, resume" scheduler. Raised
  separately: real *process* separation (programs living outside the
  kernel entirely, not just cooperative stepping inside it) is a
  further, bigger ask than this -- worth distinguishing the two when
  this gets designed, not conflating them. Treat as its own full
  `superpowers:brainstorming` cycle, not a quick add-on.

- **FILES' plain start-menu launcher shows stale data.** Found
  2026-08-18 while verifying PAINT's new SAVE feature: clicking
  `FILES` from the start menu (`STARTMENU_ITEM_FILES`'s handler,
  `kernel.c`) just sets window state and raises -- it never calls
  `fs_list_dir()` again, so it shows whatever was listed once at boot,
  regardless of what's since been written to disk. `CONFIG`/`GAMES`
  don't have this problem (both already route through
  `open_files_at()`, which does refresh). Likely fix: make the plain
  `FILES` launcher call `open_files_at(cwd, ..., cwd, ...)` (re-list
  the *current* cwd) instead of just opening the window -- small,
  bounded, not attempted here since it surfaced mid-verification of an
  unrelated feature.

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
