# acid OS v2 — bring-up design

Date: 2026-09-13
Status: approved design, pending spec self-review sign-off

## Context

RaveOS (this repo) is being kept as-is ("v1") — a bare-metal x86 kernel with a
Forth interpreter and a custom GUI stack. This spec starts "acid OS v2", a new
project living alongside it in `v2/`, that abandons that foundation entirely
in favor of an architecture modeled on **family-mruby-os**
(https://family-mruby.github.io/, https://blog.silentworlds.info/family-mruby-os-freertosbesunomicrorubymarutivmgou-xiang-2/):
FreeRTOS + one-mruby-VM-per-task application isolation, targeting the same
hardware spec as the M5Stack Tab5 (ESP32-P4 dual-core RISC-V + ESP32-C6), with
an eventual custom cyberdeck built around it.

This is not a clone. Confirmed facts about family-mruby-os (from their own
architecture page and the blog post, both fetched and quoted during
brainstorming) are noted as **[confirmed]** below; everything else is this
project's own design, informed by but not copied from theirs — in several
places explicitly diverging on purpose (no Forth, no AOT-Ruby kernel, C-only
kernel/GUI for now, mruby reserved for apps).

acid OS v2 is a large, multi-phase project. This spec covers **only the first
phase: bring-up** — proving the core vertical slice (FreeRTOS task → mruby VM
→ pixels on screen) on two build targets before any real GUI, audio, or app
exists. Later phases (windowing/GUI, audio, first app, real hardware bring-up,
custom cyberdeck) are a roadmap, each to get its own spec when its turn comes.

## Decisions made during brainstorming

- **Repo layout:** new top-level `v2/` directory in this same repo (not a
  separate repo, not replacing `main`). RaveOS v1's existing paths
  (`kernel/`, `boot/`, `programs/`, `demos/`) are untouched.
- **Scripting language:** real mruby (not PicoRuby, not a bespoke language,
  no Forth). Apps are written in actual Ruby.
- **Simulation platform:** a native Linux build with SDL2, not QEMU. This
  matches family-mruby-os's own approach **[confirmed: their Linux build is
  "not a mock... runs the real kernel, the real desktop and the real apps,
  with the drivers swapped underneath"]**.
- **v1 reuse:** only `kernel/audio/synth.c`/`.h` (the 8-voice SID-style
  software synth — self-contained DSP, produces raw PCM independent of the
  SB16 driver it currently feeds). Everything else — GUI, window manager,
  font, paint app, shell — is a clean-slate design for v2, not ported.
- **Kernel/GUI language:** plain C (diverges from family-mruby-os, where
  **[confirmed]** "the kernel and the desktop are written in Ruby... compiled
  ahead of time to native code," with only user apps interpreted). Chosen for
  bring-up simplicity — no AOT Ruby-to-native toolchain needed before there's
  even a window manager to compile. Nothing forecloses migrating pieces to
  AOT Ruby in a later phase once the C version works.
- **Ollama (qwen3.5:9B):** a dev-time helper for this project's own
  development process only. Not a feature of acid OS v2 itself.
- **Audio:** strictly deferred to a later sub-project (#3 on the roadmap).
  Bring-up does not wire up sound at all.
- **Hardware target:** must build successfully (`idf.py build` for
  `esp32p4`) as part of bring-up's definition of done. Flashing to real
  hardware is not required — the board isn't in hand yet — but a silently
  broken hardware build would defeat the purpose of maintaining two targets.

## Architecture

Two build targets share one body of target-agnostic C source, split at a HAL
(hardware abstraction layer) boundary:

- **`sim` target** — a plain native Linux build (CMake, not ESP-IDF). Links
  the FreeRTOS Kernel's official POSIX/simulator port (real FreeRTOS running
  natively, not a pretend scheduler) + LovyanGFX's SDL2 simulator backend
  (LovyanGFX is **[confirmed]** what family-mruby-os itself uses for
  drawing — their API is "RPC implemented on top of LovyanGFX") + mruby.
  LovyanGFX's SDL2 backend (`lgfx::Panel_sdl`) is itself real and
  **[verified]**: an official example ships in LovyanGFX's own repo
  (`examples_for_PC/PlatformIO_SDL`), and independent CMake-based Linux
  simulator projects exist built on it (e.g. `HangX-Ma/LGFX-simulator-SDL`),
  confirming it isn't locked to a particular build tool.
- **`hw` target** — a real ESP-IDF project (`idf.py set-target esp32p4`).
  ESP-IDF's own FreeRTOS + LovyanGFX's real panel/touch driver for the Tab5's
  display + touch, mruby vendored the same way as `sim`.
  **Known risk:** LovyanGFX supports pure ESP-IDF (non-Arduino) builds, but
  most of its documented examples and community usage assume the Arduino
  framework — the pure-ESP-IDF path is real but less-traveled. Not a
  blocker, but worth watching during implementation; if it proves painful,
  falling back to a hand-rolled minimal panel driver for just the Tab5's
  controller is the fallback, not switching frameworks.

This intentionally does **not** use ESP-IDF's own experimental
`--preview set-target linux` host-target feature — that mechanism is meant
for host-side unit testing and restricts the component set significantly;
it's a poor fit for a windowed SDL2 simulator with real display/input.

Everything above the HAL line — VM host task, app loader, mruby bindings,
window manager (once it exists in a later phase), the ported synth (once
wired up in a later phase) — is identical source for both targets. Only the
HAL implementation differs per target (display init/blit, input polling,
audio callback wiring, time/tick source) — and for bring-up, only
display/input are implemented; audio HAL is out of scope entirely.

The FreeRTOS POSIX port and the mruby vendoring approach below are this
project's own design choices, informed by real working prior art
(`mruby-esp32`, an unrelated third-party ESP-IDF+mruby template) but not
confirmed to be family-mruby-os's own actual implementation — their real
source (split across `fmruby-core`, `fmruby-graphics-audio`, etc.) was not
directly inspected for this spec.

## Directory layout

```
v2/
  sim/                    # native Linux build (plain CMake)
    CMakeLists.txt
    main.c                # host entry point, starts FreeRTOS scheduler
  hw/                      # ESP-IDF project for esp32p4
    CMakeLists.txt
    sdkconfig.defaults
    main/
      app_main.c           # ESP-IDF entry point, starts app core
  core/                    # shared, target-agnostic C — the actual OS
    vm_host/               # spawns/owns mruby VM FreeRTOS tasks, per-VM heap
    hal/                   # HAL interface (display, input) — headers only
    gfx/                   # thin drawing API on top of HAL, exposed to mruby
    bindings/              # mruby C bindings exposing gfx/events to Ruby
  components/
    mruby/                 # vendored as a git submodule
    lovyangfx/              # vendored as a git submodule (no confirmed
                            # ESP Component Registry listing found; submodule
                            # matches the mruby vendoring approach)
  apps/
    hello.rb                # first mruby script bring-up runs
  README.md
```

`core/hal/` defines the interface only (e.g. `hal_display_init()`,
`hal_display_blit()`, `hal_input_poll()`); `sim/` and `hw/` each provide one
implementation, wired to LovyanGFX's two backends.

## Data flow — boot sequence

1. Platform entry point (`sim/main.c` or `hw/main/app_main.c`) starts the
   FreeRTOS scheduler.
2. A `vm_host` task (`core/vm_host/`) is created first — it owns
   display/input HAL init and app-loading logic.
3. `vm_host` allocates a memory pool, creates one mruby VM instance, and
   spawns a FreeRTOS task that runs that VM against `apps/hello.rb`.
4. `hello.rb` calls into `core/bindings/` to reach `core/gfx/` — e.g. clear
   the screen, draw a rectangle, print text — proving Ruby app code can reach
   the display through the whole stack.
5. `vm_host`'s task loop polls input via the HAL and can (in principle)
   terminate the VM task if it faults. Full crash-isolation/restart machinery
   (family-mruby-os's **[confirmed]** "misbehaving app terminated, its memory
   pool reallocated, and restarted") is real scope for a later sub-project,
   not bring-up — there's only one app to run right now, nothing to restart
   into.

## Error handling (bring-up scope)

Minimal by design. If the mruby VM task hits an unrecoverable error (parse
error, unhandled exception, HAL call failure), it logs to stdout/serial and
halts that task only — `vm_host` and the rest of the system keep running. No
auto-restart, no multi-VM juggling. This proves the fault stays contained to
the VM task rather than taking down the whole scheduler; it does not
implement recovery.

## Testing / definition of done

No automated test suite for bring-up — this phase is infrastructure proving
a vertical slice, and correctness here is "it draws the thing," verified by
running it. Bring-up is done when, on **both** targets independently:

- **`sim`:** `cmake --build` produces a native binary; running it opens an
  SDL2 window, an mruby VM task runs `hello.rb`, and something that Ruby code
  drew is visible in the window.
- **`hw`:** `idf.py build` succeeds for `esp32p4`. Flashing/running on real
  M5Stack Tab5 hardware is not required (board not in hand yet), but the
  build must succeed and stay succeeding.

## Roadmap (future specs, not part of this one)

1. Bring-up (this spec)
2. Windowing/GUI framework (own visual style, not a Windows-3.1 clone)
3. Audio subsystem — port `synth.c`'s DSP, wire to SDL2 audio callback (sim)
   / I2S (hw)
4. First real app/game end-to-end
5. Real M5Stack Tab5 hardware bring-up (flash + run)
6. Custom cyberdeck hardware
