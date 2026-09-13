# acid OS v2 Bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the vertical slice — FreeRTOS task → mruby VM → pixels on screen — on two build targets (`v2/sim` native Linux/SDL2, `v2/hw` ESP-IDF esp32p4), with nothing else (no windowing, no audio, no real panel driver) built yet.

**Architecture:** Target-agnostic C/C++ sources live under `v2/core/` behind a small HAL interface (`core/hal/hal_display.h`). `v2/sim` links the FreeRTOS-Kernel's official POSIX port + LovyanGFX's SDL2 backend natively on Linux; `v2/hw` is a real ESP-IDF project for `esp32p4` with a stubbed (no-op) HAL implementation, since no board is in hand yet and the spec only requires `idf.py build` to succeed. mruby is vendored once (`v2/components/mruby`, git submodule) and built twice: mruby's own native `rake` host build for `sim`, and an ESP-IDF component wrapping the same source via mruby's Rake cross-build for `hw`.

**Tech Stack:** C, C++17 (LovyanGFX/mruby-VM glue only), CMake (`sim`), ESP-IDF/`idf.py` (`hw`), FreeRTOS-Kernel (POSIX port for `sim`, ESP-IDF's bundled FreeRTOS for `hw`), mruby, LovyanGFX, SDL2.

**Spec:** `docs/superpowers/specs/2026-09-13-acid-os-v2-bringup-design.md`

## Global Constraints

- RaveOS v1's existing paths (`kernel/`, `boot/`, `programs/`, `demos/`) must not be touched by any task in this plan.
- Everything new lives under `v2/`.
- Audio is out of scope for every task in this plan — do not wire up sound.
- `hw` only needs to **build** (`idf.py build` succeeding for the `esp32p4` target). Flashing/running on real hardware is explicitly not required.
- No automated test suite for this phase — each task's "test" is building and running the actual binary and observing real output (console text or a drawn pixel), per the spec's definition of done.
- Kernel/GUI logic is plain C (C++ is used only where a vendored library requires it: the mruby↔LovyanGFX glue). Do not write new OS logic in Ruby for this phase.

---

## Task 1: Repo scaffold and vendored submodules

**Files:**
- Create (directories): `v2/sim/`, `v2/hw/main/`, `v2/core/vm_host/`, `v2/core/hal/`, `v2/core/gfx/`, `v2/core/bindings/`, `v2/apps/`
- Create: `v2/README.md`
- Create (git submodule): `v2/components/mruby` → `https://github.com/mruby/mruby.git`
- Create (git submodule): `v2/components/lovyangfx` → `https://github.com/lovyan03/LovyanGFX.git`

**Interfaces:**
- Produces: the directory layout every later task writes into; `v2/components/mruby/include/mruby.h` and `v2/components/lovyangfx/src/LovyanGFX.hpp` as real files on disk (submodules checked out, not just registered).

- [ ] **Step 1: Create the directory skeleton**

```bash
mkdir -p /home/norfolkh/os/v2/sim
mkdir -p /home/norfolkh/os/v2/hw/main
mkdir -p /home/norfolkh/os/v2/core/vm_host
mkdir -p /home/norfolkh/os/v2/core/hal
mkdir -p /home/norfolkh/os/v2/core/gfx
mkdir -p /home/norfolkh/os/v2/core/bindings
mkdir -p /home/norfolkh/os/v2/apps
```

- [ ] **Step 2: Vendor mruby as a git submodule**

```bash
cd /home/norfolkh/os
git submodule add https://github.com/mruby/mruby.git v2/components/mruby
```

- [ ] **Step 3: Vendor LovyanGFX as a git submodule**

```bash
cd /home/norfolkh/os
git submodule add https://github.com/lovyan03/LovyanGFX.git v2/components/lovyangfx
```

- [ ] **Step 4: Write `v2/README.md`**

```markdown
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
```

- [ ] **Step 5: Verify the submodules actually checked out**

```bash
test -f /home/norfolkh/os/v2/components/mruby/include/mruby.h && echo "mruby OK"
test -f /home/norfolkh/os/v2/components/lovyangfx/src/LovyanGFX.hpp && echo "lovyangfx OK"
```

Expected: both `OK` lines print. If either submodule directory is empty, run `git submodule update --init --recursive` and re-check.

- [ ] **Step 6: Commit**

```bash
cd /home/norfolkh/os
git add v2/README.md v2/.gitmodules v2/components/mruby v2/components/lovyangfx
git add v2/sim v2/hw v2/core v2/apps 2>/dev/null || true
git commit -m "v2: scaffold acid OS v2 directory layout, vendor mruby and LovyanGFX"
```

(Empty directories aren't tracked by git; the `mkdir -p` calls above just prepare the tree for the files the next tasks add into them — this is expected, not an error.)

---

## Task 2: `sim` — real FreeRTOS scheduler running natively

**Files:**
- Create (git submodule): `v2/components/freertos-kernel` → `https://github.com/FreeRTOS/FreeRTOS-Kernel.git`
- Create: `v2/sim/FreeRTOSConfig.h`
- Create: `v2/sim/sim_main.c`
- Create: `v2/sim/CMakeLists.txt`

**Interfaces:**
- Produces: `void sim_freertos_main(void)` in `sim_main.c` — creates FreeRTOS tasks and calls `vTaskStartScheduler()` (never returns). Task 3 replaces `sim_main.c`'s own temporary `main()` with a call to this function from a new SDL-aware entry point.

- [ ] **Step 1: Vendor the FreeRTOS Kernel as a git submodule**

```bash
cd /home/norfolkh/os
git submodule add https://github.com/FreeRTOS/FreeRTOS-Kernel.git v2/components/freertos-kernel
```

- [ ] **Step 2: Write `v2/sim/FreeRTOSConfig.h`**

Minimal config for the POSIX/simulator port — trimmed from FreeRTOS's own official `FreeRTOS/Demo/Posix_GCC/FreeRTOSConfig.h` (dropped: run-time-stats, trace recorder, coverage-test and networking scaffolding, none of which bring-up needs).

```c
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <pthread.h>

#define configUSE_PREEMPTION                       1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    0
#define configUSE_TICKLESS_IDLE                    0
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configTICK_RATE_HZ                         ( 1000 )
#define configMINIMAL_STACK_SIZE                   ( PTHREAD_STACK_MIN )
#define configMAX_TASK_NAME_LEN                    ( 16 )
#define configUSE_16_BIT_TICKS                     0
#define configIDLE_SHOULD_YIELD                    1
#define configUSE_MUTEXES                          1
#define configUSE_RECURSIVE_MUTEXES                1
#define configUSE_COUNTING_SEMAPHORES              1
#define configQUEUE_REGISTRY_SIZE                  10
#define configUSE_QUEUE_SETS                       0
#define configUSE_TASK_NOTIFICATIONS               1
#define configUSE_TRACE_FACILITY                   0
#define configUSE_STATS_FORMATTING_FUNCTIONS       0
#define configCHECK_FOR_STACK_OVERFLOW             0
#define configUSE_MALLOC_FAILED_HOOK               0
#define configTOTAL_HEAP_SIZE                      ( ( size_t ) ( 4 * 1024 * 1024 ) )
#define configMAX_PRIORITIES                       ( 7 )
#define configSUPPORT_STATIC_ALLOCATION            0
#define configSUPPORT_DYNAMIC_ALLOCATION           1
#define configRECORD_STACK_HIGH_ADDRESS            1

#define configUSE_TIMERS                           1
#define configTIMER_TASK_PRIORITY                  ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                   10
#define configTIMER_TASK_STACK_DEPTH               ( configMINIMAL_STACK_SIZE * 2 )

#define configUSE_CO_ROUTINES                      0
#define configMAX_CO_ROUTINE_PRIORITIES            ( 2 )

#define INCLUDE_vTaskPrioritySet                   1
#define INCLUDE_uxTaskPriorityGet                  1
#define INCLUDE_vTaskDelete                        1
#define INCLUDE_vTaskSuspend                       1
#define INCLUDE_vTaskDelayUntil                    1
#define INCLUDE_vTaskDelay                         1
#define INCLUDE_xTaskGetSchedulerState             1
#define INCLUDE_xTaskGetIdleTaskHandle             1
#define INCLUDE_xTaskGetHandle                     1
#define INCLUDE_eTaskGetState                      1

void vAssertCalled( const char * pcFile, unsigned long ulLine );
#define configASSERT( x )    if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )

#endif /* FREERTOS_CONFIG_H */
```

- [ ] **Step 3: Write `v2/sim/sim_main.c`**

```c
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

static void print_task( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        printf( "acid OS v2 sim: FreeRTOS task alive\n" );
        fflush( stdout );
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

void sim_freertos_main( void )
{
    xTaskCreate( print_task, "print", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL );
    vTaskStartScheduler();
    /* vTaskStartScheduler() only returns if there was insufficient heap
     * to create the idle/timer tasks. */
    for( ;; ) {}
}

int main( void )
{
    sim_freertos_main();
    return 0;
}
```

- [ ] **Step 4: Write `v2/sim/CMakeLists.txt`**

Follows the FreeRTOS-Kernel's own required consumer contract exactly (its root `CMakeLists.txt` hard-fails with the precise snippet needed if `freertos_config` isn't defined this way) and the official Posix demo's `FREERTOS_PORT`/`FREERTOS_HEAP` selection.

```cmake
cmake_minimum_required(VERSION 3.15)
project(acidos_v2_sim C)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../components/freertos-kernel)

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set(FREERTOS_HEAP "3" CACHE STRING "" FORCE)
set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)

add_subdirectory(${FREERTOS_KERNEL_PATH} ${CMAKE_CURRENT_BINARY_DIR}/FreeRTOS-Kernel)

add_executable(acidos_sim sim_main.c)
target_link_libraries(acidos_sim PRIVATE freertos_kernel freertos_config pthread)
```

- [ ] **Step 5: Build**

```bash
cmake -S /home/norfolkh/os/v2/sim -B /home/norfolkh/os/v2/sim/build
cmake --build /home/norfolkh/os/v2/sim/build
```

Expected: builds cleanly, producing `v2/sim/build/acidos_sim`.

- [ ] **Step 6: Run and verify**

```bash
timeout 3 /home/norfolkh/os/v2/sim/build/acidos_sim
```

Expected: prints `acid OS v2 sim: FreeRTOS task alive` roughly every second (2-3 lines in a 3-second run), then is killed by `timeout` (this is expected — the FreeRTOS scheduler never returns on its own).

- [ ] **Step 7: Commit**

```bash
cd /home/norfolkh/os
git add v2/.gitmodules v2/components/freertos-kernel v2/sim/FreeRTOSConfig.h v2/sim/sim_main.c v2/sim/CMakeLists.txt
git commit -m "v2/sim: real FreeRTOS (POSIX port) scheduler running natively"
```

---

## Task 3: `sim` — LovyanGFX SDL2 window, threaded alongside FreeRTOS

**Files:**
- Modify: `v2/sim/sim_main.c` (remove the `int main(void)` wrapper — that job moves to `main.cpp`; `sim_freertos_main` itself is unchanged)
- Create: `v2/sim/gfx_sdl.cpp`
- Create: `v2/sim/main.cpp`
- Modify: `v2/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `sim_freertos_main(void)` from Task 2 (now called from `main.cpp` instead of `sim_main.c`'s own `main`).
- Produces: `extern "C" void sim_gfx_init(void)` and `extern "C" void sim_gfx_fill_rect(int x, int y, int w, int h, uint32_t color)` in `gfx_sdl.cpp` — Task 4 calls these directly from the mruby binding; Task 5 renames/moves them behind the HAL.

- [ ] **Step 1: Remove the temporary `main()` from `sim_main.c`**

Delete this block from the end of `v2/sim/sim_main.c` (added in Task 2, Step 3):

```c
int main( void )
{
    sim_freertos_main();
    return 0;
}
```

`sim_freertos_main(void)` itself stays — it's now called from `main.cpp` instead.

- [ ] **Step 2: Write `v2/sim/gfx_sdl.cpp`**

Uses LovyanGFX's own auto-detect header exactly as its official `CMake_SDL` example does (`LGFX_SDL` defined by the build, `LGFX_AUTODETECT.hpp` picks the SDL2 backend), exposing two plain-C-callable entry points for the FreeRTOS side (which stays C) to draw with.

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

static LGFX lcd( 320, 240 );

extern "C" void sim_gfx_init( void )
{
    lcd.init();
    lcd.fillScreen( TFT_BLACK );
}

extern "C" void sim_gfx_fill_rect( int x, int y, int w, int h, uint32_t color )
{
    lcd.fillRect( x, y, w, h, color );
}
```

- [ ] **Step 3: Write `v2/sim/main.cpp`**

Runs LovyanGFX's SDL event pump on the real `main()` thread while the FreeRTOS scheduler (and everything it runs) executes on a separate thread — this is `lgfx::Panel_sdl::main()`'s own documented job (it spawns the given function on an SDL thread via `SDL_CreateThread` and pumps SDL events itself until every window closes), verified directly from LovyanGFX's `Panel_sdl.cpp` source.

`vTaskStartScheduler()` never returns on its own, so something has to notice the window closed and end the process directly — `Panel_sdl::main()` sets `*running = false` and then calls `SDL_WaitThread()`, which would otherwise hang forever waiting for a thread function that's never coming back.

```cpp
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

extern "C" void sim_freertos_main( void );

static volatile bool * s_running = 0;

extern "C" int sim_should_quit( void )
{
    return ( s_running != 0 && *s_running == false ) ? 1 : 0;
}

static int freertos_thread_entry( bool * running )
{
    s_running = running;
    sim_freertos_main();  /* never returns on its own; the FreeRTOS side polls
                           * sim_should_quit() and calls exit(0) directly */
    return 0;
}

int main( int, char ** )
{
    return lgfx::Panel_sdl::main( freertos_thread_entry );
}
```

- [ ] **Step 4: Modify `v2/sim/CMakeLists.txt`**

Add LovyanGFX's SDL2-backend sources (the same file set as LovyanGFX's own official `examples_for_PC/CMake_SDL/CMakeLists.txt`, paths adjusted to our vendored location), switch on C++, and link SDL2.

```cmake
cmake_minimum_required(VERSION 3.15)
project(acidos_v2_sim C CXX)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../components/freertos-kernel)
set(LOVYANGFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/lovyangfx)

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set(FREERTOS_HEAP "3" CACHE STRING "" FORCE)
set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)

add_subdirectory(${FREERTOS_KERNEL_PATH} ${CMAKE_CURRENT_BINARY_DIR}/FreeRTOS-Kernel)

add_definitions(-DLGFX_SDL)

file(GLOB LGFX_SOURCES CONFIGURE_DEPENDS
    ${LOVYANGFX_DIR}/src/lgfx/Fonts/efont/*.c
    ${LOVYANGFX_DIR}/src/lgfx/Fonts/IPA/*.c
    ${LOVYANGFX_DIR}/src/lgfx/Fonts/lvgl/*.c
    ${LOVYANGFX_DIR}/src/lgfx/utility/*.c
    ${LOVYANGFX_DIR}/src/lgfx/v1/*.cpp
    ${LOVYANGFX_DIR}/src/lgfx/v1/lv_font/*.c
    ${LOVYANGFX_DIR}/src/lgfx/v1/misc/*.cpp
    ${LOVYANGFX_DIR}/src/lgfx/v1/panel/Panel_Device.cpp
    ${LOVYANGFX_DIR}/src/lgfx/v1/panel/Panel_FrameBufferBase.cpp
    ${LOVYANGFX_DIR}/src/lgfx/v1/platforms/sdl/*.cpp
)

add_executable(acidos_sim
    sim_main.c
    gfx_sdl.cpp
    main.cpp
    ${LGFX_SOURCES}
)

target_include_directories(acidos_sim PRIVATE ${LOVYANGFX_DIR}/src)
target_compile_features(acidos_sim PUBLIC cxx_std_17)

find_package(SDL2 REQUIRED)
target_include_directories(acidos_sim PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(acidos_sim PRIVATE freertos_kernel freertos_config pthread ${SDL2_LIBRARIES})
```

- [ ] **Step 5: Wire the draw calls into the existing print task (temporarily, until Task 4 replaces it)**

Modify `print_task` in `v2/sim/sim_main.c` to call the new gfx functions instead of only printing — this proves drawing happens *from a FreeRTOS task*, not from `main()`:

```c
#include <stdlib.h>

extern void sim_gfx_init( void );
extern void sim_gfx_fill_rect( int x, int y, int w, int h, unsigned int color );
extern int sim_should_quit( void );

static void print_task( void * pvParameters )
{
    ( void ) pvParameters;
    sim_gfx_init();
    int x = 0;
    for( ;; )
    {
        if( sim_should_quit() )
        {
            exit( 0 );
        }
        printf( "acid OS v2 sim: FreeRTOS task alive\n" );
        fflush( stdout );
        sim_gfx_fill_rect( x % 260, 90, 60, 60, 0xF800 );
        x += 10;
        vTaskDelay( pdMS_TO_TICKS( 200 ) );
    }
}
```

- [ ] **Step 6: Build**

```bash
rm -rf /home/norfolkh/os/v2/sim/build
cmake -S /home/norfolkh/os/v2/sim -B /home/norfolkh/os/v2/sim/build
cmake --build /home/norfolkh/os/v2/sim/build
```

Expected: builds cleanly (requires SDL2 dev package installed on this machine — `libsdl2-dev` on Debian/Ubuntu-family systems; install it first if `find_package(SDL2 REQUIRED)` fails).

- [ ] **Step 7: Run and verify**

```bash
/home/norfolkh/os/v2/sim/build/acidos_sim
```

Expected: an SDL2 window opens showing a black screen with a red 60x60 square sliding left-to-right and wrapping around; console keeps printing the alive message. Close the window — the process should exit cleanly (not hang); confirm with `echo $?` or by checking the shell returns control immediately.

- [ ] **Step 8: Commit**

```bash
cd /home/norfolkh/os
git add v2/sim/sim_main.c v2/sim/gfx_sdl.cpp v2/sim/main.cpp v2/sim/CMakeLists.txt
git commit -m "v2/sim: LovyanGFX SDL2 window driven from a FreeRTOS task"
```

---

## Task 4: `sim` — mruby VM running `hello.rb`, drawing through a binding

**Files:**
- Modify: `v2/sim/CMakeLists.txt` (build mruby via its own `rake`, link `libmruby.a`)
- Create: `v2/core/vm_host/vm_host.h`
- Create: `v2/core/vm_host/vm_host.c`
- Create: `v2/core/bindings/gfx_binding.h`
- Create: `v2/core/bindings/gfx_binding.c`
- Create: `v2/apps/hello.rb`
- Modify: `v2/sim/sim_main.c` (replace the raw `print_task` from Task 3 with the mruby VM host task)

**Interfaces:**
- Consumes: `sim_gfx_init()`/`sim_gfx_fill_rect()` from Task 3.
- Produces: `void vm_host_task(void *pvParameters)` (FreeRTOS task entry, in `vm_host.h`) — Task 6 (`hw`) reuses this unchanged. `void acid_bindings_register(mrb_state *mrb)` (in `gfx_binding.h`) — Task 5 changes only its *body* to call the HAL instead of `sim_gfx_*` directly; its signature doesn't change.

- [ ] **Step 1: Write `v2/core/bindings/gfx_binding.h`**

```c
#ifndef ACID_GFX_BINDING_H
#define ACID_GFX_BINDING_H

#include "mruby.h"

void acid_bindings_register( mrb_state * mrb );

#endif
```

- [ ] **Step 2: Write `v2/core/bindings/gfx_binding.c`**

mruby's C extension API (`mrb_define_module_function`, `mrb_get_args`) is core, stable mruby API — verified directly against the vendored `v2/components/mruby/include/mruby.h`.

```c
#include "gfx_binding.h"

extern void sim_gfx_fill_rect( int x, int y, int w, int h, unsigned int color );

static mrb_value
acid_fill_rect( mrb_state * mrb, mrb_value self )
{
    mrb_int x, y, w, h, color;
    mrb_get_args( mrb, "iiiii", &x, &y, &w, &h, &color );
    sim_gfx_fill_rect( ( int ) x, ( int ) y, ( int ) w, ( int ) h, ( unsigned int ) color );
    return mrb_nil_value();
}

void
acid_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_fill_rect",
                                 acid_fill_rect, MRB_ARGS_REQ( 5 ) );
}
```

- [ ] **Step 3: Write `v2/core/vm_host/vm_host.h`**

```c
#ifndef ACID_VM_HOST_H
#define ACID_VM_HOST_H

void vm_host_task( void * pvParameters );

#endif
```

- [ ] **Step 4: Write `v2/core/vm_host/vm_host.c`**

`mrb_open`/`mrb_ccontext_new`/`mrb_load_detect_file_cxt`/`mrb_ccontext_free`/`mrb_close` are verified directly against the vendored mruby's current `include/mruby.h` and `include/mruby/compile.h` (note: the older `mrbc_context_*` names some third-party examples use are now compatibility macros for `mrb_ccontext_*` — this uses the current, non-deprecated names).

```c
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/error.h"

#include "vm_host.h"
#include "../bindings/gfx_binding.h"

extern void sim_gfx_init( void );
extern int sim_should_quit( void );

void
vm_host_task( void * pvParameters )
{
    ( void ) pvParameters;

    sim_gfx_init();

    mrb_state * mrb = mrb_open();
    acid_bindings_register( mrb );

    mrb_ccontext * cxt = mrb_ccontext_new( mrb );
    FILE * fp = fopen( "v2/apps/hello.rb", "r" );
    if( fp == NULL )
    {
        fprintf( stderr, "acid OS v2: could not open v2/apps/hello.rb\n" );
    }
    else
    {
        mrb_load_detect_file_cxt( mrb, fp, cxt );
        if( mrb->exc )
        {
            mrb_print_error( mrb );
        }
        fclose( fp );
    }

    mrb_ccontext_free( mrb, cxt );
    mrb_close( mrb );

    /* The VM run is done (or faulted, per the error handling above, which
     * already contains the fault to this task). Per the spec, bring-up does
     * no restart/recovery -- just keep the task parked, watching for the
     * sim window to close, since sim_freertos_main()/vTaskStartScheduler()
     * never returns on their own otherwise. */
    for( ;; )
    {
        if( sim_should_quit() )
        {
            exit( 0 );
        }
        vTaskDelay( pdMS_TO_TICKS( 100 ) );
    }
}
```

(The script path is relative to wherever `acidos_sim` is run from — run it from the repo root, `/home/norfolkh/os`, as the verification step below does.)

- [ ] **Step 5: Write `v2/apps/hello.rb`**

```ruby
acid_fill_rect(20, 20, 100, 60, 0xF800)
acid_fill_rect(140, 100, 100, 60, 0x07E0)
```

- [ ] **Step 6: Replace the drawing task in `v2/sim/sim_main.c` with the VM host task**

Remove `print_task` (added in Task 2, extended in Task 3) and `sim_freertos_main`'s call to it; replace with:

```c
#include "../core/vm_host/vm_host.h"

void sim_freertos_main( void )
{
    xTaskCreate( vm_host_task, "vm_host", 8192, NULL, tskIDLE_PRIORITY + 1, NULL );
    vTaskStartScheduler();
    for( ;; ) {}
}
```

(`sim_gfx_init`/`sim_gfx_fill_rect` externs and the old `print_task` function body are removed from this file — they're now called from `vm_host.c` and `gfx_binding.c` respectively. `vAssertCalled` stays.)

- [ ] **Step 7: Modify `v2/sim/CMakeLists.txt` to build and link mruby**

mruby's own default `rake` task only builds (verified: `task :default => :all`, `:all` invokes `:build`, no test execution) — no `MRUBY_CONFIG` needed for its default (`host`) build target.

```cmake
set(MRUBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/mruby)
set(LIBMRUBY_FILE ${MRUBY_DIR}/build/host/lib/libmruby.a)

add_custom_command(
    OUTPUT ${LIBMRUBY_FILE}
    COMMAND rake
    WORKING_DIRECTORY ${MRUBY_DIR}
    VERBATIM
)
add_custom_target(mruby_build DEPENDS ${LIBMRUBY_FILE})

add_executable(acidos_sim
    sim_main.c
    gfx_sdl.cpp
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/vm_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/gfx_binding.c
    ${LGFX_SOURCES}
)
add_dependencies(acidos_sim mruby_build)

target_include_directories(acidos_sim PRIVATE
    ${LOVYANGFX_DIR}/src
    ${MRUBY_DIR}/include
)
target_link_libraries(acidos_sim PRIVATE
    freertos_kernel freertos_config pthread ${SDL2_LIBRARIES} ${LIBMRUBY_FILE} m
)
```

(This replaces the plain `add_executable(acidos_sim sim_main.c gfx_sdl.cpp main.cpp ${LGFX_SOURCES})` block and its `target_link_libraries` call from Task 3 — the rest of the file, `freertos_config`/`FreeRTOS-Kernel`/`find_package(SDL2 ...)` setup, is unchanged.)

- [ ] **Step 8: Build**

```bash
rm -rf /home/norfolkh/os/v2/sim/build
cmake -S /home/norfolkh/os/v2/sim -B /home/norfolkh/os/v2/sim/build
cmake --build /home/norfolkh/os/v2/sim/build
```

Expected: mruby's own `rake` build runs first (may take a minute the first time), then `acidos_sim` links against `libmruby.a` successfully.

- [ ] **Step 9: Run and verify from the repo root**

```bash
cd /home/norfolkh/os
./v2/sim/build/acidos_sim
```

Expected: an SDL2 window opens showing a red rectangle at (20,20) and a green rectangle at (140,100) — both drawn by `hello.rb` through `acid_fill_rect`, proving the full stack (FreeRTOS task → mruby VM → C binding → LovyanGFX → SDL2 window). Close the window — the process exits cleanly (this is `vm_host_task`'s own quit-check now, not just `print_task`'s from Task 3).

- [ ] **Step 10: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/vm_host v2/core/bindings v2/apps/hello.rb v2/sim/sim_main.c v2/sim/CMakeLists.txt
git commit -m "v2/sim: mruby VM hosted in a FreeRTOS task, running hello.rb through a gfx binding"
```

---

## Task 5: HAL boundary extraction

The spec scopes bring-up's HAL to **display and input** (audio is the only HAL surface explicitly deferred). Input wasn't actually exercised yet — Tasks 3/4 used an un-abstracted `sim_should_quit()` in `main.cpp` just to let closing the SDL window exit the process cleanly instead of hanging on `SDL_WaitThread`. This task both extracts the display HAL (as originally scoped) and folds that quit-check into the same HAL boundary as `hal_input_should_quit()`, so the spec's input surface is actually represented, not just display.

**Files:**
- Create: `v2/core/hal/hal_display.h`
- Create: `v2/core/hal/hal_input.h`
- Rename+Modify: `v2/sim/gfx_sdl.cpp` → `v2/sim/hal_display_sim.cpp` (functions renamed to the HAL names)
- Modify: `v2/sim/main.cpp` (rename `sim_should_quit` to `hal_input_should_quit`)
- Create: `v2/core/gfx/gfx.h`
- Create: `v2/core/gfx/gfx.c`
- Modify: `v2/core/bindings/gfx_binding.c` (call `gfx_fill_rect` instead of `sim_gfx_fill_rect` directly)
- Modify: `v2/core/vm_host/vm_host.c` (call `hal_display_init`/`hal_input_should_quit` instead of `sim_gfx_init`/`sim_should_quit` directly)
- Modify: `v2/sim/CMakeLists.txt` (source file renamed)

**Interfaces:**
- Consumes: `sim_gfx_init`/`sim_gfx_fill_rect`/`sim_should_quit` from Task 3/4 (renamed here, not reimplemented).
- Produces: `hal_display_init(void)`/`hal_display_fill_rect(int,int,int,int,uint32_t)` (declared in `core/hal/hal_display.h`) and `hal_input_should_quit(void)` (declared in `core/hal/hal_input.h`) — one implementation of each per target; Task 6's `hw` stubs implement both. `gfx_init(void)`/`gfx_fill_rect(int,int,int,int,uint32_t)` (declared in `core/gfx/gfx.h`) — this is what `core/bindings/` calls from now on, not the HAL directly.

- [ ] **Step 1: Write `v2/core/hal/hal_display.h`**

```c
#ifndef ACID_HAL_DISPLAY_H
#define ACID_HAL_DISPLAY_H

void hal_display_init( void );
void hal_display_fill_rect( int x, int y, int w, int h, unsigned int color );

#endif
```

- [ ] **Step 1a: Write `v2/core/hal/hal_input.h`**

```c
#ifndef ACID_HAL_INPUT_H
#define ACID_HAL_INPUT_H

/* Returns nonzero once the platform wants the app to exit (e.g. the sim
 * window was closed). Bring-up doesn't do real keyboard/mouse/touch
 * events yet -- that's the windowing/GUI phase (roadmap item 2). */
int hal_input_should_quit( void );

#endif
```

- [ ] **Step 1b: Rename `sim_should_quit` to `hal_input_should_quit` in `v2/sim/main.cpp`**

Replace:
```cpp
extern "C" int sim_should_quit( void )
```
with:
```cpp
extern "C" int hal_input_should_quit( void )
```
(the function body — checking `s_running` — is unchanged, only its exported name changes; `main.cpp` is the sim's `hal_input` implementation now, so no separate file is needed for it).

- [ ] **Step 2: Rename `v2/sim/gfx_sdl.cpp` to `v2/sim/hal_display_sim.cpp`, renaming its two functions to the HAL names**

```bash
git -C /home/norfolkh/os mv v2/sim/gfx_sdl.cpp v2/sim/hal_display_sim.cpp
```

New content of `v2/sim/hal_display_sim.cpp`:

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

static LGFX lcd( 320, 240 );

extern "C" void hal_display_init( void )
{
    lcd.init();
    lcd.fillScreen( TFT_BLACK );
}

extern "C" void hal_display_fill_rect( int x, int y, int w, int h, unsigned int color )
{
    lcd.fillRect( x, y, w, h, color );
}
```

- [ ] **Step 3: Write `v2/core/gfx/gfx.h`**

```c
#ifndef ACID_GFX_H
#define ACID_GFX_H

void gfx_init( void );
void gfx_fill_rect( int x, int y, int w, int h, unsigned int color );

#endif
```

- [ ] **Step 4: Write `v2/core/gfx/gfx.c`**

```c
#include "gfx.h"
#include "../hal/hal_display.h"

void
gfx_init( void )
{
    hal_display_init();
}

void
gfx_fill_rect( int x, int y, int w, int h, unsigned int color )
{
    hal_display_fill_rect( x, y, w, h, color );
}
```

- [ ] **Step 5: Update `v2/core/bindings/gfx_binding.c` to call `gfx_fill_rect`**

Replace:
```c
extern void sim_gfx_fill_rect( int x, int y, int w, int h, unsigned int color );
```
and its call site, with:
```c
#include "../gfx/gfx.h"
```
and change the call inside `acid_fill_rect` from `sim_gfx_fill_rect(...)` to `gfx_fill_rect(...)`.

- [ ] **Step 6: Update `v2/core/vm_host/vm_host.c` to call `gfx_init` and `hal_input_should_quit`**

Replace:
```c
extern void sim_gfx_init( void );
extern int sim_should_quit( void );
```
with:
```c
#include "../gfx/gfx.h"
#include "../hal/hal_input.h"
```
and change the calls inside `vm_host_task` from `sim_gfx_init()` to `gfx_init()`, and from `sim_should_quit()` to `hal_input_should_quit()`.

- [ ] **Step 7: Update `v2/sim/CMakeLists.txt`**

In the `add_executable(acidos_sim ...)` file list, replace `gfx_sdl.cpp` with `hal_display_sim.cpp`, and add `core/gfx/gfx.c` alongside the existing `core/vm_host/vm_host.c` and `core/bindings/gfx_binding.c` entries:

```cmake
add_executable(acidos_sim
    sim_main.c
    hal_display_sim.cpp
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/vm_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/gfx_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gfx/gfx.c
    ${LGFX_SOURCES}
)
```

- [ ] **Step 8: Build**

```bash
rm -rf /home/norfolkh/os/v2/sim/build
cmake -S /home/norfolkh/os/v2/sim -B /home/norfolkh/os/v2/sim/build
cmake --build /home/norfolkh/os/v2/sim/build
```

Expected: builds cleanly.

- [ ] **Step 9: Run and verify no behavior changed**

```bash
cd /home/norfolkh/os
./v2/sim/build/acidos_sim
```

Expected: identical to Task 4's result — red and green rectangles drawn by `hello.rb`. This is a pure refactor; the drawn output must be unchanged.

- [ ] **Step 10: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/hal v2/core/gfx v2/core/bindings/gfx_binding.c v2/core/vm_host/vm_host.c v2/sim/hal_display_sim.cpp v2/sim/main.cpp v2/sim/CMakeLists.txt
git commit -m "v2: extract a HAL boundary (display + input) between core/ and the sim-specific backend"
```

---

## Task 6: `hw` — ESP-IDF project scaffold, mruby vendored via Rake, stub HAL

**Files:**
- Create: `v2/hw/CMakeLists.txt`
- Create: `v2/hw/sdkconfig.defaults`
- Create: `v2/hw/components/mruby_component/CMakeLists.txt`
- Create: `v2/hw/components/mruby_component/build_config.rb`
- Create: `v2/hw/main/CMakeLists.txt`
- Create: `v2/hw/main/app_main.c`
- Create: `v2/hw/main/hal_display_hw.c`
- Create: `v2/hw/main/hal_input_hw.c`

**Interfaces:**
- Consumes: `core/vm_host/vm_host.c` + `.h` (Task 4), `core/bindings/gfx_binding.c` + `.h` (Task 4), `core/gfx/gfx.c` + `.h` (Task 5), `core/hal/hal_display.h` + `core/hal/hal_input.h` (Task 5), `v2/components/mruby` submodule (Task 1).
- Produces: a second, no-op implementation of `hal_display_init`/`hal_display_fill_rect` and `hal_input_should_quit` — stubbed (logs only / always "don't quit") because there's no real M5Stack Tab5 panel or input driver wired up yet; that's real scope for the spec's roadmap phase 5 (real hardware bring-up), not this one. This task's only acceptance bar, per the spec, is that `idf.py build` succeeds.

- [ ] **Step 1: Write `v2/hw/CMakeLists.txt`**

Standard ESP-IDF project root file — verified against the real `mruby-esp32/mruby-esp32` template's own root `CMakeLists.txt`.

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(acidos_v2_hw)
```

- [ ] **Step 2: Write `v2/hw/sdkconfig.defaults`**

```
CONFIG_IDF_TARGET="esp32p4"
```

- [ ] **Step 3: Write `v2/hw/components/mruby_component/build_config.rb`**

Cross-build config for mruby's Rake build, targeting `esp32p4`. Adapted from `mruby-esp32`'s real `esp32_build_config.rb` (verified), trimmed to only the gems bring-up actually needs — no networking/GPIO/ADC gems, since nothing in this phase touches peripherals yet (YAGNI; add gems in a later phase when an app actually needs them).

```ruby
MRuby::CrossBuild.new('esp32p4') do |conf|
  toolchain :gcc

  conf.cc do |cc|
    cc.include_paths << ENV["COMPONENT_INCLUDES"].split(';')
    cc.flags << '-mlongcalls' if ENV["MRUBY_TARGET_ARCH"] == 'xtensa'
    cc.flags << '-std=gnu17'
    cc.flags = cc.flags.flatten.collect { |x| x.gsub('-MP', '') }
    cc.defines << %w(ESP_PLATFORM)
  end

  conf.cxx do |cxx|
    cxx.include_paths = conf.cc.include_paths.dup
    cxx.flags = cxx.flags.flatten.collect { |x| x.gsub('-MP', '') }
    cxx.defines = conf.cc.defines.dup
  end

  conf.bins = []
  conf.build_mrbtest_lib_only
  conf.disable_cxx_exception

  conf.gem :core => "mruby-print"
  conf.gem :core => "mruby-compiler"
end
```

(The `-mlongcalls` flag only applies to Xtensa targets, not the P4's RISC-V core — guarded behind an env var here rather than assumed, since this plan has not verified ESP-IDF's exact RISC-V compiler flag requirements for esp32p4; if the build fails on this flag, remove the `-mlongcalls` line entirely, since RISC-V doesn't need it.)

- [ ] **Step 4: Write `v2/hw/components/mruby_component/CMakeLists.txt`**

Follows the real, verified `mruby-esp32/mruby-esp32` component pattern exactly (its own `components/mruby_component/CMakeLists.txt`), pointed at our shared submodule under `v2/components/mruby` (not a separate copy) and our own `build_config.rb` above.

```cmake
set(MRUBY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/mruby")
set(LIBMRUBY_FILE "${MRUBY_DIR}/build/esp32p4/lib/libmruby.a")
set(MRUBY_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/build_config.rb")

idf_component_register(
    INCLUDE_DIRS ${MRUBY_DIR}/include
)

add_custom_command(
    OUTPUT ${LIBMRUBY_FILE}
    COMMAND ${CMAKE_COMMAND} -E env
        "MRUBY_CONFIG=${MRUBY_CONFIG}"
        "COMPONENT_INCLUDES=$<TARGET_PROPERTY:${COMPONENT_TARGET},INTERFACE_INCLUDE_DIRECTORIES>"
        rake
    WORKING_DIRECTORY ${MRUBY_DIR}
    VERBATIM
)

add_custom_target(mruby DEPENDS ${LIBMRUBY_FILE})
add_dependencies(${COMPONENT_LIB} mruby)

target_link_libraries(${COMPONENT_LIB} INTERFACE ${LIBMRUBY_FILE})
```

- [ ] **Step 5: Write `v2/hw/main/hal_display_hw.c`**

Explicit stub — no real panel driver yet. Logs via ESP-IDF's own logging so the boot sequence is at least observable once real hardware exists, without claiming to draw anything.

```c
#include "esp_log.h"
#include "../../core/hal/hal_display.h"

static const char * TAG = "hal_display_hw";

void
hal_display_init( void )
{
    ESP_LOGI( TAG, "hal_display_init: stub, no panel driver wired up yet" );
}

void
hal_display_fill_rect( int x, int y, int w, int h, unsigned int color )
{
    ESP_LOGI( TAG, "hal_display_fill_rect(%d, %d, %d, %d, 0x%06x): stub, not drawn", x, y, w, h, color );
}
```

- [ ] **Step 5a: Write `v2/hw/main/hal_input_hw.c`**

Explicit stub — no real input source wired up yet either. Always reports "don't quit"; `vm_host_task`'s quit-check (added in Task 5) is a no-op on this target, which is correct until phase 5 (real hardware bring-up) gives it something real to check.

```c
#include "../../core/hal/hal_input.h"

int
hal_input_should_quit( void )
{
    return 0;
}
```

- [ ] **Step 6: Write `v2/hw/main/app_main.c`**

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../../core/vm_host/vm_host.h"

void
app_main( void )
{
    xTaskCreate( vm_host_task, "vm_host", 16384, NULL, 5, NULL );
}
```

- [ ] **Step 7: Write `v2/hw/main/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS
        app_main.c
        hal_display_hw.c
        hal_input_hw.c
        ../../core/vm_host/vm_host.c
        ../../core/bindings/gfx_binding.c
        ../../core/gfx/gfx.c
    INCLUDE_DIRS
        ../../core/vm_host
        ../../core/hal
        ../../core/gfx
        ../../core/bindings
    REQUIRES mruby_component
)
```

- [ ] **Step 8: Build**

```bash
cd /home/norfolkh/os/v2/hw
idf.py set-target esp32p4
idf.py build
```

Expected: succeeds, producing a firmware image. This requires ESP-IDF to be installed and its environment sourced (`. $IDF_PATH/export.sh` or equivalent) — if ESP-IDF isn't installed on this machine, this is the point to stop and report that rather than guessing at the outcome; per the spec, flashing is not required, but the build itself must actually succeed to satisfy this task's deliverable.

- [ ] **Step 9: Commit**

```bash
cd /home/norfolkh/os
git add v2/hw
git commit -m "v2/hw: ESP-IDF esp32p4 project scaffold, mruby vendored via Rake, stub display HAL"
```

---

## Definition of done (both targets, per spec)

- `sim`: `cmake --build` succeeds; running `v2/sim/build/acidos_sim` from the repo root opens an SDL2 window showing the two rectangles `apps/hello.rb` draws.
- `hw`: `idf.py build` succeeds for `esp32p4`. Flashing to real M5Stack Tab5 hardware is explicitly out of scope for this plan.
