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
