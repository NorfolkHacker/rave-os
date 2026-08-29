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

  The resonant filter has now also shipped, 2026-08-24 (see
  `docs/superpowers/specs/2026-08-23-sid-filter-ringmod-design.md` and
  `docs/BUILD_LOG.md`'s entry for the same date): one shared fixed-point
  resonant state-variable filter (matching the real SID's own
  one-filter-for-all-voices architecture, scaled from 3 to 8 voices),
  combinable low/band/high-pass output modes, per-voice routing (bypass
  or through the filter), plus per-voice assignable ring modulation on
  the triangle waveform (a feature the original entry didn't ask for but
  that rounds out the SID-style feature set). (C) -- a real control
  surface / note-sequencing language -- remains unbuilt, still separate
  future work.

  A per-voice arpeggio effect has now also shipped, 2026-08-25 (see
  `docs/superpowers/specs/2026-08-25-synth-arpeggio-design.md` and
  `docs/BUILD_LOG.md`'s entry for the same date): each voice can hold an
  explicit 2-4 note list (`ARP-NOTE`) that, once armed (`ARP-ON`),
  cycles up-only with wraparound at a configurable, sample-accurate
  millisecond rate (`ARP-RATE`), fully independent of the shared
  filter/ring-mod machinery described just above -- an arpeggiating
  voice can still be filter-routed or ring-modulated like any other.
  This is a separate, self-contained addition, not part of (and does
  not replace the need for) (C)'s still-unbuilt real note-sequencing
  language -- there is no pattern/song sequencing, timing beyond a
  single per-voice rate, or scripting here, just a fixed-length pitch
  cycle per voice.

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

- ~~**Different resolution settings, not just the one hardcoded
  640x480.**~~ Done (build-time slice), 2026-08-26 -- `boot/Makefile`'s
  `VBE_MODE` is now a build-time override (`make VBE_MODE=0x115` for
  800x600, default `0x112`/640x480 unchanged), and `kernel/kernel.c`'s
  five hardcoded window positions scale against the real screen size.
  Verified headlessly in QEMU at both resolutions -- see
  `docs/BUILD_LOG.md`'s entry for this date. What's still open: stage2
  itself doesn't query/select among multiple VBE modes at runtime, so
  this is a fixed choice baked in at build time, not a real
  auto-detected/negotiated resolution -- that runtime piece is a
  bigger lift than it first sounds and stays a candidate for later.

- **Real userspace: ring 3, paging, syscalls -- separate address
  spaces per program.** Raised 2026-08-26. Everything today (PAINT,
  EDITOR, SHELL, FORTH scripts run via `RUN`) is compiled straight into
  `kernel.c` and scheduled as cooperative fibers sharing one ring-0
  address space -- there's no process isolation at all, just the
  scheduler built 2026-08-19/20. That work explicitly scoped real
  process isolation out as "Rave-OS v2 scale" at the time (see
  `docs/superpowers/specs/2026-08-19-concurrency-design.md`). Actually
  landing it means a page-table/paging layer, a ring 3 switch, a
  syscall interface for the ring-0 services programs currently call
  directly (fs, gfx, audio, window management), and some real notion
  of a loadable/relocatable program image separate from being baked
  into the kernel binary -- a large, foundational lift, not an
  incremental one.

  (A), paging itself, has now also shipped, 2026-08-26 (see
  `docs/superpowers/specs/2026-08-26-paging-design.md` and
  `docs/BUILD_LOG.md`'s entry for the same date): `kernel/arch/paging.c`/
  `.h` builds a single static page directory of 4MB PSE pages
  identity-mapping physical memory 1:1 (every address maps to itself,
  proven a true no-op for the kernel's own code, stack, `boot_info`,
  backbuffer, and the real VBE framebuffer in headless-QEMU
  verification), switched on in `kmain()` right after
  `interrupts_init()`. `kernel/arch/isr.c`'s page-fault handler now
  reports the faulting address and error code instead of a bare
  generic banner, confirmed to actually fire correctly against a
  deliberately-unmapped address. This is only the foundation, though:
  no guard pages, no ring 3, and no per-process address spaces exist
  yet -- every mapped page is supervisor-only and there's still exactly
  one address space, shared by every fiber, identical to before this
  landed. (B) ring 3 + a minimal syscall ABI, (C) a real syscall
  surface for existing kernel services, and (D) a loadable/relocatable
  program format all remain entirely unbuilt, separate future work.

  (B), ring 3 + a minimal syscall ABI, has now also shipped, 2026-08-27
  (see `docs/superpowers/specs/2026-08-27-ring3-syscall-design.md` and
  `docs/BUILD_LOG.md`'s entry for the same date): `kernel/arch/gdt.c`/
  `.h` builds a kernel-owned GDT with real DPL 3 user code/data
  segments and a TSS (so a ring3->ring0 transition has a safe kernel
  stack to land on), `paging_set_user()` flips one page directory
  entry's User/Supervisor bit so ring 3 code can actually fetch its
  own instructions, and `kernel/arch/ring3.asm` implements
  `enter_ring3()` (the CPL0->CPL3 switch) and a hand-written `int 0x80`
  trap-gate stub (`eax` in/out, `ebx` the one argument, every other
  register clobbered) dispatching into `kernel/arch/syscall.c`'s
  `syscall_dispatch()` (later split by sub-project (C)'s Task 1 into a
  pure `syscall_dispatch_core()` staying in `syscall.c` and the real
  `syscall_dispatch()` that `ring3.asm` calls, moved to the new
  `kernel/arch/syscall_fs.c`). Proven end to end in headless QEMU: a temporary
  ring-3 payload round-tripped a real syscall (`SYS_TEST`'s argument
  and return value both crossing the ring3/ring0 boundary intact) and
  then deliberately executed a CPL0-only instruction, producing exactly
  the expected `PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000`
  banner -- confirming both the syscall ABI and CPL enforcement work on
  real hardware, not just that they compile. That verification also
  caught a real bug in (B)'s own Task 1, `paging_set_user()` (a missing
  TLB flush after a live PDE change -- fixed as its own dedicated
  commit, permanent, unlike the temporary proof payload itself, which
  was fully removed afterward). (C) a real syscall surface for existing
  kernel services (fs, gfx, audio, window management) and (D) a
  loadable/relocatable program format both remain entirely unbuilt,
  separate future work.

  (C)'s first slice, a single real filesystem syscall, has now also
  shipped, 2026-08-27 (see
  `docs/superpowers/specs/2026-08-27-fs-syscall-design.md` and
  `docs/BUILD_LOG.md`'s entry for the same date): `SYS_READ_FILE`, a
  thin wrapper around `kernel/fs/fs.c`'s existing `fs_read_file()`,
  reachable from CPL 3 through the same `int 0x80` trap gate (B)
  built, its four arguments (`path`, `buf`, `buf_size`, `out_size`)
  carried across the boundary as a single pointer to a
  `struct sys_read_file_args` rather than extending `ring3.asm`'s
  register ABI. Proven end to end in headless QEMU: a temporary ring-3
  payload read `/BIN/HELLO`'s real, on-disk content via
  `SYS_READ_FILE`, checked its length and leading bytes, and only then executed
  the same deliberate CPL0-only instruction (B)'s own proof used,
  producing the identical `PANIC: GENERAL PROTECTION FAULT` /
  `CODE=0x00000000` banner -- reaching it at all requires the
  filesystem read to have actually round-tripped correctly, not just
  the syscall plumbing. That verification also caught a real ordering
  bug in the temporary proof payload's own call-site placement (not in
  any permanent syscall machinery): placed immediately after
  `interrupts_enable()`, mirroring (B)'s own call site, the payload ran
  before `/BIN/HELLO` was created on disk later in `kmain()`, so the
  read correctly failed and produced an indistinguishable-looking black
  screen until the call site was moved to after `/BIN/HELLO`'s
  creation. This is deliberately only one operation out of `fs.h`'s
  roughly a dozen (create, delete, list, mkdir, and the rest all remain
  unwrapped), and gfx/audio/window-management have no syscall surface
  at all yet -- both remain separate future work, and per this spec's
  own Purpose section, window-management and audio in particular will
  need real per-program ownership design once they're tackled, not
  just a thin wrapper like this slice's `SYS_READ_FILE`. (D) a
  loadable/relocatable program format remains entirely unbuilt as well.

  (C)'s second slice, two more real filesystem syscalls, has now also
  shipped, 2026-08-27 (see
  `docs/superpowers/specs/2026-08-27-fs-syscall-write-list-design.md`
  and `docs/BUILD_LOG.md`'s entry for the same date): `SYS_CREATE_FILE`
  and `SYS_LIST_DIR`, thin wrappers around `kernel/fs/fs.c`'s existing
  `fs_create_file()`/`fs_list_dir()`, using the same single-pointer
  args-struct convention the first slice established -- no new syscall
  plumbing was needed. Proven end to end in headless QEMU: a temporary
  ring-3 payload created `/TMP/RING3.TXT` via `SYS_CREATE_FILE`, then
  listed `/TMP` via `SYS_LIST_DIR` and confirmed the entry was actually
  present in the returned listing, only then executing the same
  deliberate CPL0-only instruction the prior two proofs used, producing
  the identical `PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000`
  banner. This is still not the rest of `fs.h`'s surface -- `fs_delete`,
  `fs_rename`, `fs_move`, `fs_copy_file`, `fs_append_file`, and
  `fs_create_dir` all remain unwrapped -- and gfx/audio/window-management
  syscalls and (D) a loadable/relocatable program format all remain
  entirely unbuilt, same as before.

  (C)'s third slice, two more real filesystem syscalls, has now also
  shipped, 2026-08-29 (see `docs/BUILD_LOG.md`'s entry for the same
  date): `SYS_DELETE` and `SYS_CREATE_DIR`, thin wrappers around
  `kernel/fs/fs.c`'s existing `fs_delete()`/`fs_create_dir()`.
  Implemented directly rather than through a full spec+plan+SDD cycle
  -- bounded, not architectural, since it added no new syscall
  plumbing: both take a single pointer argument, so unlike the prior
  two slices' multi-argument syscalls, neither needed a new args
  struct -- `ebx` carries the path pointer directly. Proven end to end
  in headless QEMU: a temporary ring-3 payload created then deleted
  `/TMP/DELME.TXT` via `SYS_CREATE_FILE`/`SYS_DELETE`, created
  `/TMP/NEWDIR` via `SYS_CREATE_DIR`, then listed `/TMP` via
  `SYS_LIST_DIR` and confirmed the final snapshot matched (`NEWDIR`
  present, `DELME` absent), only then executing the same deliberate
  CPL0-only instruction every prior proof used, producing the
  identical `PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000`
  banner, on the first attempt. `fs_rename`, `fs_move`, `fs_copy_file`,
  and `fs_append_file` still remain unwrapped, and gfx/audio/
  window-management syscalls and (D) a loadable/relocatable program
  format all remain entirely unbuilt, same as before.

  (C)'s fourth slice, the last four real filesystem syscalls, has now
  also shipped, 2026-08-29 (see `docs/BUILD_LOG.md`'s entry for the
  same date): `SYS_RENAME`, `SYS_MOVE`, `SYS_COPY_FILE`, and
  `SYS_APPEND_FILE`, thin wrappers around `kernel/fs/fs.c`'s existing
  `fs_rename()`/`fs_move()`/`fs_copy_file()`/`fs_append_file()`.
  Implemented directly, same as the third slice -- bounded, no new
  syscall plumbing needed. Proven end to end in headless QEMU: a
  temporary ring-3 payload chained all four together -- created
  `/TMP/ORIG.TXT`, appended to it, copied it into `/HOME`, renamed the
  `/TMP` copy to `RENAMED.TXT`, then moved that into `/ETC` -- gating
  success on `/HOME` containing `ORIG.TXT` and `/ETC` containing
  `RENAMED.TXT` via two `SYS_LIST_DIR` calls, then executing the same
  deliberate CPL0-only instruction every prior proof used, producing
  the identical `PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000`
  banner, on the first attempt. **With this slice, sub-project (C)'s
  coverage of `kernel/fs/fs.h` is complete** -- every real filesystem
  operation is now reachable from ring 3 (`fs_path_join()`/
  `fs_path_parent()` are pure path-string helpers with no filesystem
  side effect, not planned as syscalls). gfx/audio/window-management
  syscalls and (D) a loadable/relocatable program format remain
  entirely unbuilt, separate future work.

  (C)'s first gfx slice has now also shipped, 2026-08-29 (see
  `docs/BUILD_LOG.md`'s entry for the same date): `SYS_GFX_WIDTH`,
  `SYS_GFX_HEIGHT`, `SYS_GFX_CLEAR`, `SYS_GFX_PUT_PIXEL`,
  `SYS_GFX_FILL_RECT`, and `SYS_GFX_PRESENT_RECT`, thin wrappers
  around `kernel/gfx/graphics.h`. Needed a new dispatcher-chain link
  rather than just more `if` cases in an existing file: a new
  `kernel/arch/syscall_gfx.c` holds the real, `graphics.h`-touching
  `syscall_dispatch_gfx()`, and `syscall_fs.c`'s `syscall_dispatch()`
  now falls through to it instead of calling `syscall_dispatch_core()`
  directly -- keeping `syscall_dispatch_core()` (what the host test
  links) dependency-free on both `fs.h` and `graphics.h`.
  Deliberately no per-window clipping or ownership: brainstormed with
  the user first, who chose the thin-wrapper approach over building
  real ownership now, since that would require integrating with
  `kernel/gui/window.c` before any gfx syscall could ship -- explicitly
  deferred to the window-management slice, which needs that
  integration anyway. Proven end to end in headless QEMU: a temporary
  ring-3 payload cleared the screen red, drew a centered 100x100 green
  square, set a blue pixel at its exact center (using
  `SYS_GFX_WIDTH`/`SYS_GFX_HEIGHT` to compute the center), and
  presented it -- confirmed directly in the proof screendump itself
  (the panic banner drew on top without clearing the rest, so the
  scene stayed visible underneath it), on the first attempt. Audio and
  window-management syscalls and (D) a loadable/relocatable program
  format remain entirely unbuilt, same as before.

  (C)'s first audio slice has now also shipped, 2026-08-29 (see
  `docs/BUILD_LOG.md`'s entry for the same date): `SYS_SYNTH_SET_WAVEFORM`,
  `SYS_SYNTH_SET_ONA`, `SYS_SYNTH_SET_ADSR`, `SYS_SYNTH_GATE_ON`, and
  `SYS_SYNTH_GATE_OFF`, thin wrappers around `kernel/audio/synth.h` --
  enough to make a voice play a note from ring 3, not synth.h's much
  larger remaining API (duty cycle, ring modulation, filter routing,
  arpeggio, global filter cutoff/resonance/mode), same YAGNI scoping
  the gfx slice used. Extended the dispatcher chain a fourth link:
  `syscall_dispatch_gfx()` now falls through to a new
  `syscall_dispatch_audio()` (`kernel/arch/syscall_audio.c`) instead of
  `syscall_dispatch_core()` directly. Proven end to end in headless
  QEMU: since there's no screendump that can confirm sound, a temporary
  ring-3 payload instead read `synth_voices[0]` back directly after
  each call (a legitimate read -- PDE 0 is already fully
  user-accessible, and `synth_voices[]` is an ordinary extern global,
  not something the syscall boundary hides) to confirm each argument
  actually reached and changed the real kernel state: waveform, pitch,
  envelope stage after gate-on, the ADSR's exact sustain-level scaling,
  and envelope stage after gate-off, all before the usual deliberate
  `#GP` proof tail. On the first attempt. Window-management syscalls
  and (D) a loadable/relocatable program format remain entirely
  unbuilt, same as before.

  (C)'s window-management slice has now also shipped, 2026-08-29 (see
  `docs/superpowers/specs/2026-08-29-ring3-window-design.md` and
  `docs/BUILD_LOG.md`'s entry for the same date) -- deliberately scoped
  far smaller than full interactivity after investigation showed the
  real cost: `kernel.c`'s window system is five hardcoded slots with
  bespoke inline content-drawing, no process/ownership concept exists
  anywhere, and `kmain()` never regains control once `enter_ring3()` is
  called, so real event delivery would need extracting its per-frame
  update loop into a callable function first -- comparable in scope to
  (D). Brainstormed with the user, who chose a static, non-interactive
  window instead of committing to that bigger lift now. Ships a sixth
  window slot (`WIN_KIND_RING3`, `MAX_WINDOWS` now 6) and two syscalls,
  `SYS_WINDOW_OPEN`/`SYS_WINDOW_CLOSE`, wrapping two new
  `kernel.c`-private functions (no pre-existing subsystem header to
  wrap, unlike fs/gfx/audio) that draw and present the window's chrome
  synchronously inside the syscall itself. No dragging, no
  click/keyboard event delivery -- a ring-3 program draws its own
  content using the gfx syscalls already shipped. Extended the
  dispatcher chain a fifth link. Proven end to end in headless QEMU: a
  baseline boot (new slot wired in, no payload) confirmed the
  `WINDOW_OPEN == 0` boot-time-init trap was caught before it could ship
  a garbage window on every ordinary boot; the proof itself showed a
  real bordered window -- titlebar, controls, distinct accent color,
  drawn content -- rendered on the ordinary desktop, with the usual
  panic banner confirming the in-payload open/close state checks also
  passed. On the first attempt. Real event delivery/interactivity and
  (D) a loadable/relocatable program format remain entirely unbuilt,
  separate future work -- (D) may even be a prerequisite, since window
  ownership arguably wants a real "program" concept first.

  (D), a loadable program format, has now also shipped, 2026-08-29 (see
  `docs/superpowers/specs/2026-08-29-loadable-program-design.md` and
  `docs/BUILD_LOG.md`'s entry for the same date) -- the last remaining
  piece of this entire "Real userspace" entry. A real *relocatable*
  format was investigated and rejected: RaveOS has no malloc/heap
  anywhere, so a relocatable loader's free-address allocator would be
  new infrastructure nothing else in this codebase needs. Instead, one
  fixed load address, `0x00200000`, checked empirically (`nm` on a real
  build showed the kernel's own `.bss` ending at `0x440b0`, comfortably
  clear) rather than guessed. `programs/hello/` is the actual format
  deliverable -- a tiny freestanding program, its own linker script
  fixing its origin, and a `Makefile` using the same cross-toolchain
  `kernel/` already does to produce a raw flat binary via `objcopy -O
  binary`, identical to how `kernel.bin` itself is built. Needed no new
  syscalls at all: `program_load_and_run()` (`kernel/kernel.c`) reads a
  path's real content via the already-shipped `fs_read_file()` straight
  into the fixed address and `enter_ring3()`s into it -- loading a
  program is something ring 0 does, not something a ring-3 program
  calls on itself. The compiled demo binary is seeded onto `fs.img` as
  `/BIN/USERPROG.BIN` the same write-once way `/BIN/HELLO` already is
  (this repo has no host-side disk-image tooling). Proven end to end in
  headless QEMU: reaching the usual `#GP` proof banner required
  `hello.bin`'s own compiled code, loaded fresh from a real on-disk
  file, to have actually executed and gotten `SYS_TEST`'s correct
  return value -- not just that the loader's plumbing ran. On the first
  attempt.

  **With this slice, all four sub-projects of this entry -- (A) paging,
  (B) ring 3 + a minimal syscall ABI, (C) a real syscall surface
  (fs/gfx/audio/window), and (D) a loadable program format -- have
  shipped in some form.** What remains, all explicitly deferred rather
  than built, several comparable in scope to what's already here: real
  per-window interactivity (dragging, event delivery -- needs
  extracting `kmain()`'s per-frame loop into a callable function
  first); `synth.h`'s much larger remaining API; true program
  relocation and running more than one loaded program at a time.

  Raised 2026-08-29, during investigation into real event delivery:
  **PAINT and FORTH (and, by the same reasoning, EDITOR/FILES/SHELL)
  should eventually be their own separately-compiled binaries,
  launched from userspace via (D)'s `program_load_and_run()`, not
  built directly into the kernel binary the way all five are today.**
  This is a natural extension of the same "separate from being baked
  into the kernel binary" goal this entry's own Purpose section
  originally named for (D) -- currently only satisfied for the one
  `programs/hello/` demo, not any of the five real built-in apps.
  Depends on both remaining deferred pieces above: real event delivery
  (so a loaded program can actually receive its own window's
  mouse/keyboard input, not just draw once and exit) and, likely,
  `SYS_WINDOW_OPEN` gaining real content-ownership once more than one
  ring-3 window can exist -- today's single `WIN_KIND_RING3` slot
  assumes exactly one loaded program running at a time. Not scoped or
  started; noted here so it isn't lost while the prerequisite pieces
  are tackled.

  Once each app is its own binary, its own settings could live in
  `/ETC` (or `/HOME`) as real config files read via the already-shipped
  fs syscalls, the same pattern `/ETC/CONFIG` already establishes for
  one kernel-side setting today (`fx_default_from_config()`, seeding a
  default if missing, read once at boot -- see `kernel/kernel.c`).
  Extending that same idea per-program (a PAINT binary reading its own
  `/ETC/PAINT.CFG` for e.g. last-used color, a FORTH binary reading
  its own startup script path) falls naturally out of both ideas above
  landing, rather than needing new design of its own.

  A real prerequisite for event delivery shipped 2026-08-29 (see
  `docs/superpowers/specs/2026-08-29-kmain-frame-extraction-design.md`
  and `docs/BUILD_LOG.md`'s entry for the same date): `kmain()`'s
  ~916-line per-frame loop is now a standalone `kmain_frame()`
  function, not inlined directly inside `kmain()`. Pure mechanical
  refactor, zero behavior change -- verified via a scripted QEMU-monitor
  interaction sequence (menu, FORTH keyboard input, window drag,
  cross-window PAINT launch, FILES) replayed identically against the
  pre- and post-refactor builds, all 9 screendumps pixel-identical.
  `SYS_WAIT_EVENT` itself, and the input routing `WIN_KIND_RING3` still
  entirely lacks, remain separate, unstarted future work -- this only
  removed the one blocker that made a syscall re-driving this loop
  structurally impossible before.

  `SYS_WAIT_EVENT` itself has now also shipped, 2026-08-29 (see
  `docs/superpowers/specs/2026-08-29-ring3-window-events-design.md`
  and `docs/BUILD_LOG.md`'s entry for the same date): a ring-3 program
  can now block on real click and window-close events instead of only
  drawing once and sitting static, by re-driving `kmain_frame()` itself
  from inside the syscall -- real input keeps being processed while
  the program is "blocked," unlike every prior ring-3 proof's own
  infinite loop, which always froze the desktop for good. Needed no
  new architecture (dragging/raising/closing already worked generically
  for the ring-3 window); did surface and fix a real, previously-latent
  bug in the process (`draw_window_by_index()`'s ring-3 case really was
  reachable, and really did get reached, corrupting the window's
  display the moment any redraw swept its region -- fixed by redrawing
  its chrome, though the content area still reverts to backdrop under
  the same sweep, a real documented limitation, not solved here). On
  the first *design*, but not the first *build* -- root-caused via
  direct evidence after the first proof attempt showed the window
  vanishing. Real per-window interactivity now means: click and close,
  not yet keyboard, not yet reliable content persistence across
  unrelated redraws, and still only one ring-3 program at a time.

  Content persistence itself has now also shipped, 2026-08-29 (see
  `docs/superpowers/specs/2026-08-29-ring3-window-content-persistence-design.md`
  and `docs/BUILD_LOG.md`'s entry for the same date): a kernel-owned
  shadow buffer mirrors every pixel a ring-3 program draws, and
  `draw_window_by_index()` blits it back over the window's body on any
  redraw the program didn't cause itself -- a second click's rect no
  longer wipes the first's. Surfaced and fixed a real, subtle,
  previously-latent bug in the process: the shadow buffer's first cut
  was an ordinary static array, and the linker happened to place part
  of it directly on the classic x86 VGA memory hole (0xA0000-0xBFFFF),
  silently eating writes to that range; root-caused via serial tracing
  down to the individual failing addresses, then fixed by giving the
  buffer a fixed physical address, reusing `graphics.c`'s own
  `backbuffer[]` precedent for the same class of problem. Remaining,
  documented, accepted-as-is: the window's rounded bottom corners can
  show square pixels where blitted content overlaps them (cosmetic
  only); still only one ring-3 program at a time.

  Keyboard events shipped 2026-08-29 too (see
  `docs/superpowers/specs/2026-08-29-ring3-window-keyboard-events-design.md`
  and `docs/BUILD_LOG.md`'s entry for the same date): `RING3_EVENT_KEY`,
  delivered through the existing `SYS_WAIT_EVENT`, driven by a new
  persistent `ring3_focused` flag that mirrors every other window's own
  focus model exactly (set on a click landing inside the body, cleared
  by any click elsewhere -- no auto-focus-on-open, no special-casing).
  Carries the same reduced-vocabulary `char` every other focused text
  field in this kernel already gets from `keyboard_poll_char()` --
  printable ASCII, backspace, newline, arrows/home/end/delete; no raw
  scancodes, no modifier keys. Real per-window interactivity is now:
  click, close, and keyboard, all three -- still only one ring-3
  program at a time, and the single-pending-event slot still means a
  click and a keypress landing in the exact same frame can silently
  cost one of them (unsolved, same category as the pre-existing
  click-vs-close race).

- **Real floating-point arithmetic.** Raised 2026-08-26. Every
  `kernel/Makefile` build uses `-mgeneral-regs-only`, which forbids the
  FPU/SSE registers entirely -- there's no float/double anywhere in the
  kernel, only deliberate fixed-point and integer arithmetic (see
  `kernel/audio/synth.c`'s phase-increment and filter comments, which
  call this out explicitly as the reason no floating point is used).
  That flag exists because GCC's `__attribute__((interrupt))` ISR
  handlers can't safely touch FPU/SSE state without it. Actually
  supporting floats would mean either scoping `-mgeneral-regs-only` down
  to just the interrupt-handler translation units instead of the whole
  kernel, or adding real FPU/SSE context save-restore on every interrupt
  entry/exit -- plus deciding what would actually use it (a nicer synth
  filter? real VBE mode math?) since nothing currently needs it enough
  to justify the lift.

  The compiler-level barrier is gone as of 2026-08-26 (see
  `docs/BUILD_LOG.md`'s entry for the same date): a spike first checked
  whether `context_switch()`'s cooperative fiber swap (see the
  userspace entry above) needed real FPU save/restore too, since it
  only saves the four GP callee-saved registers. Disassembly showed it
  doesn't -- the x87 register stack is fully call-clobbered per the
  standard ABI, and GCC always spills any live float to memory before a
  call (`context_switch()` included) and reloads it after, so no fiber
  can ever observe another fiber's in-flight FPU state through a
  switch. `-mgeneral-regs-only` moved from the kernel-wide `CFLAGS` to
  an `isr.o`-only `ISR_CFLAGS` in `kernel/Makefile`, with no
  `context_switch.asm` changes needed. `float`/`double` now compile
  cleanly everywhere except `arch/isr.c` (confirmed: a probe function
  with `-mgeneral-regs-only` doesn't error at compile time, it silently
  routes float arithmetic through libgcc soft-float calls instead of
  touching any FPU/SSE register -- which don't exist in this
  `-nostdlib` build, so `isr.c` using a float still fails, just at link
  time instead of compile time). Nothing in the kernel actually uses
  floating point yet -- what would (a nicer synth filter? real VBE mode
  math?) is still an open, separate question.
