# Build-Time VBE Resolution Selection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the hardcoded 640x480 assumption from `boot/stage2.asm`'s VBE mode and `kernel/kernel.c`'s five window positions, so the whole disk image can be built against a different resolution (e.g. 800x600) via a Makefile variable, with zero behavior change at the existing default.

**Architecture:** Two independent, additive changes. (1) `boot/stage2.asm`'s `VBE_MODE equ 0x112` becomes `%ifndef`-overridable, and `boot/Makefile` gains a `VBE_MODE` variable passed through via `nasm -D`, following the exact pattern already used there for `KERNEL_SECTORS`/`KERNEL_START_SECTOR`. (2) `kernel/kernel.c`'s five hardcoded window `.x`/`.y` literals become expressions scaled against the real boot-time screen size (`w`/`h`, already read from `boot_info` at runtime) relative to a 640x480 baseline. Nothing else changes — `graphics.c`, the taskbar, and the start menu are already resolution-agnostic.

**Tech Stack:** x86 real-mode assembly (nasm), freestanding C (i686-elf-gcc), GNU Make, QEMU (headless, `-accel kvm`, monitor `screendump`) for verification.

**Spec:** `docs/superpowers/specs/2026-08-26-resolution-settings-design.md`

## Global Constraints

- Default build (`make` / `make run` with no `VBE_MODE` override) must remain byte-for-byte identical in behavior to today — no regression at 640x480.
- No runtime mode selection/fallback, no window-size scaling, no bpp changes — see the spec's "Out of scope" list.
- `BASELINE_W`/`BASELINE_H` are `640`/`480` — the values being replaced, used only as the scaling reference point.
- The verification target resolution is 800x600 (VBE mode `0x115`).

---

### Task 1: Overridable VBE mode in the bootloader

**Files:**
- Modify: `boot/stage2.asm:33-37` (the `VBE_MODE`/`VBE_MODE_LFB` block)
- Modify: `boot/Makefile:1-4` (add the `VBE_MODE` variable) and `boot/Makefile:26-37` (`disk.img` recipe, both `nasm` invocations of `stage2.asm`)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: a `VBE_MODE` Make variable (default `0x112`) that Task 3's verification build overrides to `0x115`. `stage2.asm` continues to write the real, BIOS-reported width/height/pitch/bpp for whichever mode this is into `boot_info` at `BOOT_INFO_ADDR` — that mechanism is untouched.

- [ ] **Step 1: Make `VBE_MODE` overridable in `stage2.asm`**

  Current (`boot/stage2.asm:33-37`):
  ```asm
  ; VBE mode 0x112 = 640x480, 32 bits/pixel, linear framebuffer. Bit 14
  ; (0x4000) of the mode number tells VBE function 4F02h to use the linear
  ; framebuffer addressing model instead of legacy bank-switched addressing.
  VBE_MODE       equ 0x112
  VBE_MODE_LFB   equ VBE_MODE | 0x4000
  ```
  Replace with:
  ```asm
  ; VBE mode number -- 0x112 = 640x480, 0x115 = 800x600, both 32
  ; bits/pixel linear-framebuffer VESA modes. Normally passed in by
  ; boot/Makefile via `nasm -D VBE_MODE=...` (same override pattern as
  ; KERNEL_SECTORS/KERNEL_START_SECTOR above); the fallback below only
  ; matters if this file is ever assembled directly, outside the
  ; Makefile. Bit 14 (0x4000) of the mode number tells VBE function
  ; 4F02h to use the linear framebuffer addressing model instead of
  ; legacy bank-switched addressing.
  %ifndef VBE_MODE
  VBE_MODE       equ 0x112
  %endif
  VBE_MODE_LFB   equ VBE_MODE | 0x4000
  ```

- [ ] **Step 2: Add the `VBE_MODE` Make variable and thread it through both `nasm` calls**

  Current (`boot/Makefile:1-4`):
  ```makefile
  AS := nasm
  QEMU := qemu-system-i386
  KERNEL_DIR := ../kernel
  KERNEL_BIN := $(KERNEL_DIR)/kernel.bin
  ```
  Replace with:
  ```makefile
  AS := nasm
  QEMU := qemu-system-i386
  KERNEL_DIR := ../kernel
  KERNEL_BIN := $(KERNEL_DIR)/kernel.bin

  # VBE mode stage2 sets at boot -- 0x112 (640x480x32, the long-standing
  # default) unless overridden, e.g. `make VBE_MODE=0x115` for 800x600.
  # disk.img's own prerequisites (stage1.asm/stage2.asm/$(KERNEL_BIN))
  # don't change when only this variable changes, so Make won't know to
  # rebuild on its own -- run `make clean` first when switching this
  # between builds.
  VBE_MODE := 0x112
  ```

  Current (`boot/Makefile:26-37`, the `disk.img` recipe):
  ```makefile
  disk.img: stage1.asm stage2.asm $(KERNEL_BIN)
  	@kernel_sectors=$$(( ($$(stat -c%s $(KERNEL_BIN)) + 511) / 512 )); \
  	$(AS) -f bin -D KERNEL_SECTORS=1 -D KERNEL_START_SECTOR=4 stage2.asm -o /tmp/rave_stage2_measure.bin; \
  	stage2_sectors=$$(( ($$(stat -c%s /tmp/rave_stage2_measure.bin) + 511) / 512 )); \
  	rm -f /tmp/rave_stage2_measure.bin; \
  	kernel_start_sector=$$(( 2 + stage2_sectors )); \
  	echo "disk layout: stage1=1 sector, stage2=$$stage2_sectors sector(s), kernel=$$kernel_sectors sector(s) starting at sector $$kernel_start_sector"; \
  	$(AS) -f bin -D KERNEL_SECTORS=$$kernel_sectors -D KERNEL_START_SECTOR=$$kernel_start_sector stage2.asm -o stage2.bin; \
  	truncate -s $$(( stage2_sectors * 512 )) stage2.bin; \
  	$(AS) -f bin -D STAGE2_SECTORS=$$stage2_sectors stage1.asm -o stage1.bin; \
  	truncate -s $$(( kernel_sectors * 512 )) $(KERNEL_BIN); \
  	cat stage1.bin stage2.bin $(KERNEL_BIN) > disk.img
  ```
  Replace with (both `nasm ... stage2.asm` lines gain `-D VBE_MODE=$(VBE_MODE)`; the stage1 line is untouched):
  ```makefile
  disk.img: stage1.asm stage2.asm $(KERNEL_BIN)
  	@kernel_sectors=$$(( ($$(stat -c%s $(KERNEL_BIN)) + 511) / 512 )); \
  	$(AS) -f bin -D KERNEL_SECTORS=1 -D KERNEL_START_SECTOR=4 -D VBE_MODE=$(VBE_MODE) stage2.asm -o /tmp/rave_stage2_measure.bin; \
  	stage2_sectors=$$(( ($$(stat -c%s /tmp/rave_stage2_measure.bin) + 511) / 512 )); \
  	rm -f /tmp/rave_stage2_measure.bin; \
  	kernel_start_sector=$$(( 2 + stage2_sectors )); \
  	echo "disk layout: stage1=1 sector, stage2=$$stage2_sectors sector(s), kernel=$$kernel_sectors sector(s) starting at sector $$kernel_start_sector, VBE_MODE=$(VBE_MODE)"; \
  	$(AS) -f bin -D KERNEL_SECTORS=$$kernel_sectors -D KERNEL_START_SECTOR=$$kernel_start_sector -D VBE_MODE=$(VBE_MODE) stage2.asm -o stage2.bin; \
  	truncate -s $$(( stage2_sectors * 512 )) stage2.bin; \
  	$(AS) -f bin -D STAGE2_SECTORS=$$stage2_sectors stage1.asm -o stage1.bin; \
  	truncate -s $$(( kernel_sectors * 512 )) $(KERNEL_BIN); \
  	cat stage1.bin stage2.bin $(KERNEL_BIN) > disk.img
  ```
  (The measurement pass's own `VBE_MODE` value doesn't affect stage2's assembled size — same reasoning the existing comment already gives for why the measurement pass's `KERNEL_SECTORS=1 -D KERNEL_START_SECTOR=4` placeholders are safe — but passing it through keeps both `nasm` invocations symmetric and avoids an "undefined `VBE_MODE`" surprise if anyone ever removes the `%ifndef` fallback later.)

- [ ] **Step 3: Build the default (640x480) and confirm no regression**

  ```bash
  cd boot && make clean && make
  ```
  Expected: builds clean, `disk layout: ... VBE_MODE=0x112` printed, no `nasm` errors.

- [ ] **Step 4: Build the override (800x600) and confirm it takes effect**

  ```bash
  cd boot && make clean && make VBE_MODE=0x115
  ```
  Expected: builds clean, `disk layout: ... VBE_MODE=0x115` printed.

- [ ] **Step 5: Rebuild the default again to leave the working tree in its normal state**

  ```bash
  cd boot && make clean && make
  ```

- [ ] **Step 6: Commit**

  ```bash
  git add boot/stage2.asm boot/Makefile
  git commit -m "boot: make the VBE mode a build-time override, default unchanged"
  ```

---

### Task 2: Scale the five hardcoded window positions

**Files:**
- Modify: `kernel/kernel.c:194` (add `BASELINE_W`/`BASELINE_H` after `TITLE_SCALE`)
- Modify: `kernel/kernel.c:1969-1970, 1992-1993, 2092-2093, 2107-2108, 2135-2136` (the five window `.x`/`.y` assignments)

**Interfaces:**
- Consumes: `w`/`h`, already assigned from `gfx_width()`/`gfx_height()` at `kernel/kernel.c:1964-1965`, before any window setup code runs (unchanged by this task).
- Produces: nothing consumed by a later task — this is the last code task.

- [ ] **Step 1: Add the baseline constants**

  Current (`kernel/kernel.c:191-194`):
  ```c
  #define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
  #define TITLE_TEXT "RAVE-OS"
  #define TITLE_Y 40
  #define TITLE_SCALE 4
  ```
  Replace with:
  ```c
  #define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
  #define TITLE_TEXT "RAVE-OS"
  #define TITLE_Y 40
  #define TITLE_SCALE 4

  /* The screen size every hardcoded window position below was originally
   * hand-placed against. kmain() scales each one by the real w/h (from
   * gfx_width()/gfx_height(), themselves read from boot_info at boot --
   * see boot/stage2.asm's VBE_MODE) against this baseline, so the same
   * relative layout holds at any built resolution. At the default
   * 640x480 this is an exact no-op: multiplying and dividing by the
   * same value never truncates. */
  #define BASELINE_W 640
  #define BASELINE_H 480
  ```

- [ ] **Step 2: Scale FORTH's position**

  Current (`kernel/kernel.c:1969-1970`):
  ```c
      windows[WIN_KIND_FORTH].x = 170;
      windows[WIN_KIND_FORTH].y = 230;
  ```
  Replace with:
  ```c
      windows[WIN_KIND_FORTH].x = 170 * w / BASELINE_W;
      windows[WIN_KIND_FORTH].y = 230 * h / BASELINE_H;
  ```

- [ ] **Step 3: Scale FILES' position**

  Current (`kernel/kernel.c:1992-1993`):
  ```c
      windows[WIN_KIND_FILES].x = 420;
      windows[WIN_KIND_FILES].y = 120;
  ```
  Replace with:
  ```c
      windows[WIN_KIND_FILES].x = 420 * w / BASELINE_W;
      windows[WIN_KIND_FILES].y = 120 * h / BASELINE_H;
  ```

- [ ] **Step 4: Scale SHELL's position**

  Current (`kernel/kernel.c:2092-2093`):
  ```c
      windows[WIN_KIND_SHELL].x = 200;
      windows[WIN_KIND_SHELL].y = 260;
  ```
  Replace with:
  ```c
      windows[WIN_KIND_SHELL].x = 200 * w / BASELINE_W;
      windows[WIN_KIND_SHELL].y = 260 * h / BASELINE_H;
  ```

- [ ] **Step 5: Scale EDITOR's position**

  Current (`kernel/kernel.c:2107-2108`):
  ```c
      windows[WIN_KIND_EDITOR].x = 240;
      windows[WIN_KIND_EDITOR].y = 240;
  ```
  Replace with:
  ```c
      windows[WIN_KIND_EDITOR].x = 240 * w / BASELINE_W;
      windows[WIN_KIND_EDITOR].y = 240 * h / BASELINE_H;
  ```

- [ ] **Step 6: Scale PAINT's position**

  Current (`kernel/kernel.c:2135-2136`):
  ```c
      windows[WIN_KIND_PAINT].x = 340;
      windows[WIN_KIND_PAINT].y = 80;
  ```
  Replace with:
  ```c
      windows[WIN_KIND_PAINT].x = 340 * w / BASELINE_W;
      windows[WIN_KIND_PAINT].y = 80 * h / BASELINE_H;
  ```

- [ ] **Step 7: Build and confirm zero new warnings**

  ```bash
  export PATH="$HOME/opt/cross/bin:$PATH"
  cd kernel && make clean && make
  ```
  Expected: builds clean; the only warning is the pre-existing `i686-elf-ld: warning: kernel.elf has a LOAD segment with RWX permissions` (present before this change too — confirm by comparing against a build from before Task 2's edits if unsure).

- [ ] **Step 8: Commit**

  ```bash
  git add kernel/kernel.c
  git commit -m "kernel: scale hardcoded window positions against the real screen size"
  ```

---

### Task 3: Verify both resolutions in headless QEMU

**Files:**
- None modified — this task only runs the already-built images and inspects screendumps. Uses the scratch dir already available in this session (`$CLAUDE_JOB_DIR/tmp` if run in this session; otherwise any scratch dir) for the `.ppm`/`.png` files and QEMU monitor socket.

**Interfaces:**
- Consumes: `boot/disk.img`/`boot/fs.img` as built by Task 1 (default `VBE_MODE`) and by Task 1 rebuilt with `VBE_MODE=0x115` (Task 2's `kernel.c` changes are already baked into whichever `kernel.bin` `boot/disk.img` embeds, since `boot/Makefile`'s `disk.img` target depends on `$(KERNEL_BIN)`).
- Produces: nothing consumed by a later task — this is the final verification task.

**Expected window positions at 800x600** (for reference while eyeballing screendumps; computed from Task 2's scaling expressions, `w=800, h=600`):

| Window | x | y |
|---|---|---|
| FORTH  | `170*800/640` = 212 | `230*600/480` = 287 |
| FILES  | `420*800/640` = 525 | `120*600/480` = 150 |
| SHELL  | `200*800/640` = 250 | `260*600/480` = 325 |
| EDITOR | `240*800/640` = 300 | `240*600/480` = 300 |
| PAINT  | `340*800/640` = 425 | `80*600/480`  = 100 |

(FORTH's pair is the one case that doesn't divide evenly — `170*800/640 = 212.5` truncates to 212, `230*600/480 = 287.5` truncates to 287 — everything else is exact. This makes FORTH the most useful one to spot-check.)

- [ ] **Step 1: Build the default (640x480) disk image**

  ```bash
  cd boot && make clean && make VBE_MODE=0x112 && make fs.img
  ```

- [ ] **Step 2: Boot it headless and screendump the desktop**

  ```bash
  SCRATCH=<your scratch dir>
  rm -f "$SCRATCH"/rave.sock "$SCRATCH"/qemu.pid "$SCRATCH"/default.ppm
  cd boot
  nohup qemu-system-i386 -accel kvm -display none -device sb16 \
    -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 \
    -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 \
    -monitor unix:"$SCRATCH"/rave.sock,server,nowait \
    -pidfile "$SCRATCH"/qemu.pid > "$SCRATCH"/qemu.log 2>&1 &
  disown
  sleep 1
  echo "screendump $SCRATCH/default.ppm" | socat -t1 - UNIX-CONNECT:"$SCRATCH"/rave.sock
  ```
  Expected: `$SCRATCH/default.ppm` is written. Check its header (`head -c 20 "$SCRATCH/default.ppm"`) reports `640 480`.

- [ ] **Step 3: Open FORTH via the start menu and screendump it**

  QEMU's monitor `mouse_move dx dy` moves the guest cursor by a *relative*
  delta, so instead of calibrating against screendump pixels (fragile --
  see `docs/BUILD_LOG.md`'s account of exactly that going wrong for an
  SDL-window-grab-based attempt), pin the cursor to a known origin first:
  the kernel clamps `mx`/`my` to `[0, w/h - CURSOR_SIZE]`
  (`kernel/kernel.c:328-338`), so any large enough negative move reliably
  parks the cursor at guest `(0, 0)` regardless of where it started or
  what the resolution is. From that known origin, every later move is an
  exact deterministic pixel delta.

  The MENU button (`kernel/gui/startmenu.h:4`'s `STARTMENU_BUTTON_WIDTH
  = 64`, `kernel/gui/taskbar.h:6`'s `TASKBAR_HEIGHT = 24`) spans
  `x ∈ [0, 64)`, `y ∈ [h-24, h)` — its center is `(32, h-12)`. The FORTH
  item (`kernel/gui/startmenu.c:41`: `y = menu->y - (STARTMENU_ITEM_COUNT
  - index) * STARTMENU_ITEM_HEIGHT` with `STARTMENU_ITEM_COUNT=7`,
  `STARTMENU_ITEM_HEIGHT=20`, `index=0` for FORTH, `menu->y = h-24`)
  centers at `(32, h-154)`. Both are expressed relative to `menu->y`, so
  the delta between clicking the button and clicking the FORTH item is a
  constant `(0, -142)` independent of resolution.

  At the default resolution (`h=480`): button center is `(32, 468)`.
  ```bash
  printf 'mouse_move -2000 -2000\nmouse_move -2000 -2000\nmouse_move 32 468\nmouse_button 1\nmouse_button 0\nmouse_move 0 -142\nmouse_button 1\nmouse_button 0\nscreendump %s/default_forth.ppm\n' "$SCRATCH" | socat -t1 - UNIX-CONNECT:"$SCRATCH"/rave.sock
  ```
  Confirm FORTH's titlebar sits at its unscaled position (x=170, y=230
  minus the titlebar height — same position it's always rendered at
  before this plan's changes), proving the scaling math is a true no-op
  at the default resolution.

  ```bash
  kill $(cat "$SCRATCH/qemu.pid")
  ```

- [ ] **Step 4: Build the 800x600 disk image**

  ```bash
  cd boot && make clean && make VBE_MODE=0x115 && make fs.img
  ```

- [ ] **Step 5: Boot it headless and screendump the desktop**

  ```bash
  rm -f "$SCRATCH"/rave.sock "$SCRATCH"/qemu.pid "$SCRATCH"/wide.ppm
  cd boot
  nohup qemu-system-i386 -accel kvm -display none -device sb16 \
    -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 \
    -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 \
    -monitor unix:"$SCRATCH"/rave.sock,server,nowait \
    -pidfile "$SCRATCH"/qemu.pid > "$SCRATCH"/qemu.log 2>&1 &
  disown
  sleep 1
  echo "screendump $SCRATCH/wide.ppm" | socat -t1 - UNIX-CONNECT:"$SCRATCH"/rave.sock
  ```
  Expected: `$SCRATCH/wide.ppm`'s header reports `800 600` (not `640 480` — proves stage2 actually applied the override, not just that the kernel booted). Visually confirm: taskbar spans the new full width and sits at the new bottom edge (`y = 600 - TASKBAR_HEIGHT`), cursor renders, no corrupted/garbage pixels.

- [ ] **Step 6: Open FORTH via the start menu at 800x600 and confirm its position**

  Same pin-to-origin technique as Step 3. The button/item pixel targets
  change with `h` (button center `(32, h-12)`, FORTH item center
  `(32, h-154)`) but the relative delta between clicking them stays the
  same `(0, -142)` computed in Step 3. At `h=600`: button center is
  `(32, 588)`.
  ```bash
  printf 'mouse_move -2000 -2000\nmouse_move -2000 -2000\nmouse_move 32 588\nmouse_button 1\nmouse_button 0\nmouse_move 0 -142\nmouse_button 1\nmouse_button 0\nscreendump %s/wide_forth.ppm\n' "$SCRATCH" | socat -t1 - UNIX-CONNECT:"$SCRATCH"/rave.sock
  ```
  Confirm FORTH's titlebar top-left lands at (212, 287) minus the
  titlebar height per the table above — compare the pixel offset between
  FORTH's titlebar and its known `windows[...].y` value in this
  screendump against the same offset measured in Step 3's
  `default_forth.ppm` (that offset, the titlebar-chrome height, is a
  fixed constant independent of resolution, so it must match exactly).
  Also confirm the whole window (extending `w=400, h=180` from that
  position) is fully within the 800x600 framebuffer with no clipping.

  ```bash
  kill $(cat "$SCRATCH/qemu.pid")
  ```

- [ ] **Step 7: Rebuild the default afterward, leaving the working tree in its normal state**

  ```bash
  cd boot && make clean && make
  ```

- [ ] **Step 8: Clean up scratch files**

  ```bash
  rm -f "$SCRATCH"/*.ppm "$SCRATCH"/rave.sock "$SCRATCH"/qemu.pid "$SCRATCH"/qemu.log
  ```

- [ ] **Step 9: Record the verification in `docs/BUILD_LOG.md`**

  Add a dated entry (following this project's existing `## YYYY-MM-DD -- <title>` convention — see any recent entry for the exact shape) summarizing: the spec this closes out, the `boot/stage2.asm`/`boot/Makefile`/`kernel.c` changes, and the headless-QEMU findings from Steps 2-6 (both resolutions' framebuffer size confirmed, FORTH's position confirmed unchanged at default and correctly scaled at 800x600). Update `docs/IDEAS.md`'s "Different resolution settings" entry to mark it done, in the same `~~**title**~~ Done, YYYY-MM-DD -- ...` style every other closed-out entry in that file uses.

  ```bash
  git add docs/BUILD_LOG.md docs/IDEAS.md
  git commit -m "docs: close out the build-time resolution selection follow-up"
  ```
