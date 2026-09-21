# acid OS v2

Successor to RaveOS (kept as "v1" under this repo's existing `kernel/`,
`boot/`, `programs/`, `demos/`), modeled on family-mruby-os: FreeRTOS +
one-mruby-VM-per-app-task isolation, targeting the M5Stack Tab5 hardware
spec (ESP32-P4 + ESP32-C6).

See `docs/superpowers/specs/2026-09-13-acid-os-v2-bringup-design.md` for
the design this directory implements.

## Layout

- `sim/` — native Linux build (plain CMake). FreeRTOS's own POSIX port +
  LovyanGFX's SDL2 backend, for fast iteration without hardware.
- `hw/` — real ESP-IDF project targeting `esp32p4`.
- `core/` — target-agnostic C/C++ shared by both: `vm_host/` (spawns and
  owns mruby VM FreeRTOS tasks), `hal/` (the display/input interface each
  target implements once), `gfx/` (thin drawing API over the HAL),
  `bindings/` (mruby C bindings exposing `gfx/` to Ruby).
- `components/` — vendored git submodules (`mruby`, `lovyangfx`,
  `freertos-kernel`).
- `apps/` — mruby scripts that run on the VM host.
- `carts/` — example `.cart` programs, i.e. apps written outside the OS
  and installed into `apps/` by the Load Cart app. See `carts/README.txt`
  for the format.

## Carts

An app doesn't have to be born in `apps/`. A `.cart` is a plain mruby app
with a `# name:` / `# w:` / `# h:` / `# desc:` / `# libs:` comment header,
kept on a card or anywhere else outside this repo; the Load Cart app
(`apps/cart.rb`) browses `carts/`, `~/carts` and the host's `/media`,
`/mnt` and `/run/media` mount points, and installs one as
`apps/<slug>.rb` plus a generated `apps/<slug>.app.toml`. It joins the
Menu at the next boot, or runs immediately from Load Cart's own RUN
button. On hardware this is where a memory card's mount point joins that
root list.

Installing a cart writes into `apps/`, so an installed cart shows up as a
new untracked app in `git status` — commit it or delete it like any other
file. A cart can never overwrite an app that wasn't installed from a cart
(`source = cart` in the generated manifest is what marks one); a cart's
code, once installed, is as privileged as any other app here.

## Building

Step zero, for both targets: `mruby` itself carries a nested submodule
(`mrbgems/mruby-compiler/lib/prism`), so a non-recursive submodule init
leaves `prism/` empty and breaks both builds (mruby's own bytecode compiler
needs it). From the repo root:
```
git submodule update --init --recursive
```

`sim`:
```
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
./v2/sim/build/acidos_sim
```

`hw` (requires ESP-IDF >= 5.3, installed and sourced — that's the first
release with `esp32p4` target support):
```
cd v2/hw
idf.py set-target esp32p4
idf.py build
```
