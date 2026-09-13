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
- `components/` — vendored git submodules (`mruby`, `lovyangfx`).
- `apps/` — mruby scripts that run on the VM host.

## Building

`sim`:
```
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
./v2/sim/build/acidos_sim
```

`hw` (requires ESP-IDF installed and sourced):
```
cd v2/hw
idf.py set-target esp32p4
idf.py build
```
