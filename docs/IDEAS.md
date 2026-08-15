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

- **Upgrade the FILES window.** No specifics given yet -- FILES currently
  covers Stage A-F (list/navigate, open, directories, delete, create,
  rename) plus opening at `/HOME`; SHELL now covers the same filesystem
  from a typed console with its own extras (case-insensitive path
  matching, `.`/`..`). Worth a real brainstorming pass on what "upgrade"
  means before starting -- multi-select, move/copy, a text editor so
  `/BIN` scripts and `/ETC/CONFIG` can be authored in-OS instead of only
  via scratch-image byte editing, something else entirely.

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
