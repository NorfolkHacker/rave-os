# Minimal SB16 Driver + BEEP Word Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove this kernel can drive real hardware audio output: a Sound
Blaster 16 driver that resets/detects the card and plays one hardcoded PCM
test tone via DMA, triggered by a new bare Forth word `BEEP`.

**Architecture:** A new `kernel/drivers/sb16.c`/`.h` pair, following this
codebase's existing driver shape (`ata.c`'s plain-polling style): `sb16_init()`
does the DSP reset/detect handshake, `sb16_play_buffer()` programs the 8237
DMA controller's channel 1 for a single-cycle 8-bit transfer and starts
playback, and a new IRQ5 handler in `arch/isr.c` (mirroring `irq1_keyboard()`/
`irq12_mouse()`) acknowledges the card's completion interrupt. `BEEP` is a
bare Forth word wired exactly like `PAINT` (`forth_hooks.h` declares
`forth_hook_beep()`, `forth.c`'s `prim_beep()` calls it, `kernel.c` implements
it) that generates a short square wave into a `.bss` buffer on first use and
plays it.

**Tech Stack:** C (i686-elf-gcc cross-compiler), x86 port I/O (`inb`/`outb`),
8237 DMA controller, Sound Blaster 16 DSP protocol, this kernel's existing
Forth VM / IRQ / scheduler infrastructure.

**Spec:** `docs/superpowers/specs/2026-08-22-sb16-audio-driver-design.md`

## Global Constraints

- Hardcode the classic SB16 defaults and never probe for alternates: DSP I/O
  base `0x220`, IRQ `5`, 8-bit DMA channel `1` (these are QEMU `-device sb16`'s
  own defaults and every real ISA SB16's defaults).
- 8-bit unsigned PCM only. No 16-bit audio.
- Single-cycle (one-shot) DMA only. No auto-init/looping playback.
- No oscillators, mixing, multiple voices, envelopes, or filters — exactly
  one hardcoded test tone, played once per `BEEP` call. Synthesis is a later,
  separate spec.
- No control surface beyond the one bare `BEEP` word — no SHELL/EDITOR
  integration, no note-sequencing.
- No runtime hardware autodetection beyond the reset/ack handshake itself
  (`sb16_init()`'s job) — if that fails, `BEEP` is a silent no-op.

---

## Task 1: SB16 driver skeleton — reset, detect

**Files:**
- Create: `kernel/drivers/sb16.h`
- Create: `kernel/drivers/sb16.c`
- Modify: `kernel/Makefile`
- Modify: `kernel/kernel.c` (include + one `kmain()` call)
- Modify: `boot/Makefile` (add `-device sb16` to the `run` target)

**Interfaces:**
- Produces: `int sb16_init(void)` — 0 if a card responded to the reset
  handshake, -1 otherwise. Later tasks call this exactly once, from
  `kmain()`.
- Produces (internal, used by Task 2 too): the port-address `#define`s and
  the static `sb16_present` flag inside `sb16.c` — Task 2 adds more
  functions to this same file and reads `sb16_present`.

### Steps

- [ ] **Step 1: Write `kernel/drivers/sb16.h`**

```c
#ifndef RAVEOS_SB16_H
#define RAVEOS_SB16_H

/* Sound Blaster 16 driver: DSP reset/detect, and (Task 2) one-shot 8-bit
 * PCM playback via the 8237 DMA controller's channel 1. Hardcodes the
 * classic SB16 defaults (I/O base 0x220, IRQ 5, 8-bit DMA channel 1) that
 * QEMU's -device sb16 and every real ISA SB16 assume -- see
 * docs/superpowers/specs/2026-08-22-sb16-audio-driver-design.md. */

/* Resets and detects the card (DSP reset handshake, expects the card to
 * hand back 0xAA). Returns 0 if a card responded, -1 otherwise. Call once,
 * from kmain(), after interrupts_init()/interrupts_enable() -- mirrors
 * ata_selftest()'s own placement. Nothing else in this driver does
 * anything useful until this has been called and returned 0. */
int sb16_init(void);

#endif
```

- [ ] **Step 2: Write `kernel/drivers/sb16.c`**

```c
/* Sound Blaster 16 driver. See sb16.h. Hardcodes I/O base 0x220 (QEMU
 * -device sb16 and every real ISA SB16's default) -- no PnP/autodetection,
 * same "this kernel assumes fixed hardware" precedent as the one VBE mode
 * and PS/2-only input elsewhere in this codebase. */

#include "sb16.h"
#include "io.h"
#include "serial.h"

#define SB16_BASE 0x220

#define SB16_DSP_RESET 0x226        /* SB16_BASE + 0x6 */
#define SB16_DSP_READ 0x22A         /* SB16_BASE + 0xA */
#define SB16_DSP_WRITE 0x22C        /* SB16_BASE + 0xC -- also the write-buffer-status port when read */
#define SB16_DSP_READ_STATUS 0x22E  /* SB16_BASE + 0xE -- bit 7 = data available; reading it also acks the 8-bit IRQ */

#define SB16_TIMEOUT_ITERS 100000 /* same "fail fast, don't hang forever" convention as ata.c's ATA_TIMEOUT_ITERS */

static int sb16_present = 0;

/* Same 4-dummy-reads settle trick ata.c's ata_delay_400ns() uses, and the
 * same single outb(0x80, 0) trick pic.c's io_wait() uses (an unused port,
 * so the write has no side effect beyond costing a few microseconds on
 * real hardware) -- the DSP reset pulse needs to be held for a few
 * microseconds, not released immediately. */
static void sb16_io_wait(void) {
    outb(0x80, 0);
}

static int sb16_wait_read_ready(void) {
    int i;
    for (i = 0; i < SB16_TIMEOUT_ITERS; i++) {
        if (inb(SB16_DSP_READ_STATUS) & 0x80) {
            return 0;
        }
    }
    return -1;
}

int sb16_init(void) {
    int i;

    outb(SB16_DSP_RESET, 1);
    for (i = 0; i < 4; i++) {
        sb16_io_wait();
    }
    outb(SB16_DSP_RESET, 0);

    if (sb16_wait_read_ready() != 0) {
        serial_write_str("SB16: NOT FOUND\n");
        sb16_present = 0;
        return -1;
    }
    if (inb(SB16_DSP_READ) != 0xAA) {
        serial_write_str("SB16: NOT FOUND\n");
        sb16_present = 0;
        return -1;
    }

    serial_write_str("SB16: OK\n");
    sb16_present = 1;
    return 0;
}
```

- [ ] **Step 3: Wire `sb16.o` into `kernel/Makefile`**

Add `sb16.o` to the `C_OBJS` list (after `ata.o`):

```
C_OBJS := kernel.o keyboard.o mouse.o graphics.o font.o text.o idt.o pic.o isr.o window.o button.o textfield.o checkbox.o taskbar.o startmenu.o shell.o console_input.o console_history.o console_output.o forth.o ata.o sb16.o fs.o editor.o serial.o scheduler.o
```

Add a build rule (after the `ata.o` rule):

```
sb16.o: drivers/sb16.c drivers/sb16.h arch/io.h drivers/serial.h
	$(CC) $(CFLAGS) -c drivers/sb16.c -o sb16.o
```

Add `drivers/sb16.h` to `kernel.o`'s own dependency list (it will `#include`
it in Step 4) — find this line:

```
kernel.o: kernel.c gfx/graphics.h gfx/text.h drivers/mouse.h drivers/keyboard.h arch/interrupts.h gui/window.h gui/button.h gui/taskbar.h gui/startmenu.h gui/console_input.h gui/console_history.h gui/console_output.h forth/forth.h gui/shell.h arch/io.h drivers/ata.h fs/fs.h gui/editor.h forth/forth_hooks.h drivers/serial.h sched/scheduler.h
```

and append `drivers/sb16.h` to the end of it.

- [ ] **Step 4: Call `sb16_init()` from `kmain()`**

In `kernel/kernel.c`, add the include next to `ata.h`:

```c
#include "ata.h"
#include "sb16.h"
#include "fs.h"
```

Then, right after the existing `ata_status = ata_selftest();` line inside
`kmain()`, add:

```c
    ata_status = ata_selftest();
    sb16_init();
    fs_status = fs_selftest();
```

(`sb16_init()`'s own two `serial_write_str()` calls are the only feedback
this task needs — no return value is consulted yet; Task 2's
`forth_hook_beep()` is what actually checks whether a card was found.)

- [ ] **Step 5: Add `-device sb16` to `boot/Makefile`'s `run` target**

Find:

```
run: disk.img fs.img
	$(QEMU) -accel kvm -display sdl -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1
```

Replace with:

```
# -device sb16 with no audiodev property: QEMU still fully answers the
# card's I/O ports/DSP handshake/DMA/IRQ5 with no audiodev attached (a real
# card doesn't need a live speaker plugged in to respond to programming
# either) -- it just means no host audio comes out. To actually hear BEEP,
# append your own backend by hand, e.g.:
#   -audiodev pa,id=snd0 -device sb16,audiodev=snd0
# (swap "pa" for "pipewire"/"alsa"/whatever your host provides -- left out
# of the default target so `make run` never depends on a specific host
# audio server being up).
run: disk.img fs.img
	$(QEMU) -accel kvm -display sdl -device sb16 -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1
```

- [ ] **Step 6: Build**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd /home/norfolkh/os/kernel && make clean && make
```

Expected: `kernel.bin` builds with no warnings from `sb16.c`/`kernel.c`.

```bash
cd /home/norfolkh/os/boot && make
```

Expected: `disk.img` builds successfully.

- [ ] **Step 7: Headless test — card detected when present, not when absent**

Before touching QEMU, check nothing else is already using the disk images:
`ps aux | grep qemu-system`. Then copy both images to scratch rather than
testing against the live files directly:

```bash
mkdir -p /tmp/sb16-test
cp /home/norfolkh/os/boot/disk.img /home/norfolkh/os/boot/fs.img /tmp/sb16-test/
```

Positive case — boot with `-device sb16` and confirm the log shows detection:

```bash
timeout 5 qemu-system-i386 -display none -m 32 \
  -drive file=/tmp/sb16-test/disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=/tmp/sb16-test/fs.img,format=raw,if=ide,bus=0,unit=1 \
  -device sb16 -serial file:/tmp/sb16-test/present.log
grep -F "SB16: OK" /tmp/sb16-test/present.log
```

Expected: the `grep` finds the line (exit status 0).

Negative case — boot with no `-device sb16` at all, confirm the log shows
the not-found path instead (proves the detection genuinely depends on the
hardware being there, not a hardcoded success):

```bash
timeout 5 qemu-system-i386 -display none -m 32 \
  -drive file=/tmp/sb16-test/disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=/tmp/sb16-test/fs.img,format=raw,if=ide,bus=0,unit=1 \
  -serial file:/tmp/sb16-test/absent.log
grep -F "SB16: NOT FOUND" /tmp/sb16-test/absent.log
```

Expected: the `grep` finds the line (exit status 0).

- [ ] **Step 8: Commit**

```bash
git add kernel/drivers/sb16.h kernel/drivers/sb16.c kernel/Makefile kernel/kernel.c boot/Makefile
git commit -m "$(cat <<'EOF'
kernel: add SB16 DSP reset/detect driver

sb16_init() does the classic DSP reset handshake (write/clear the
reset bit, poll for the card's own 0xAA acknowledgement) against the
hardcoded 0x220/IRQ5/DMA1 defaults every real SB16 and QEMU's -device
sb16 use. Logged to serial only for now -- Task 2 wires this into an
actual playback path and a BEEP Forth word.
EOF
)"
```

---

## Task 2: DMA playback, IRQ5 handler, and the `BEEP` word

**Files:**
- Modify: `kernel/drivers/sb16.h`
- Modify: `kernel/drivers/sb16.c`
- Modify: `kernel/arch/isr.c`
- Modify: `kernel/Makefile` (isr.o's dependency line)
- Modify: `kernel/forth/forth_hooks.h`
- Modify: `kernel/forth/forth.c`
- Modify: `kernel/kernel.c`

**Interfaces:**
- Consumes: `int sb16_init(void)` (Task 1, unchanged), `void
  serial_write_str(const char *s)` (existing).
- Produces: `void sb16_play_buffer(const unsigned char *buf, unsigned int
  len, unsigned int sample_rate)` and `void sb16_irq_ack(void)` from
  `sb16.c`/`.h`; `void forth_hook_beep(void)` from `kernel.c`, declared in
  `forth_hooks.h`; the `BEEP` Forth word.

### Steps

- [ ] **Step 1: Add `sb16_play_buffer()`/`sb16_irq_ack()` to `sb16.h`**

Append to `kernel/drivers/sb16.h` (before the closing `#endif`):

```c
/* Plays buf (len bytes of 8-bit unsigned PCM, 128 = silence) once via a
 * single-cycle DMA transfer at the given sample_rate. buf must not cross
 * a 64KB physical address boundary -- give it `__attribute__((aligned(N)))`
 * for some power-of-two N >= len (an N-aligned address plus a run of at
 * most N bytes can never straddle a 64KB line, since 65536 is itself a
 * multiple of every power-of-two N used here). No-op if sb16_init() never
 * found a card, or if len is 0. Fire-and-forget: returns immediately:
 * playback continues in the background and the card raises IRQ5 on
 * completion (arch/isr.c's handler calls sb16_irq_ack()). */
void sb16_play_buffer(const unsigned char *buf, unsigned int len, unsigned int sample_rate);

/* Called by arch/isr.c's IRQ5 handler once playback finishes -- reads the
 * DSP's own interrupt-acknowledge port so the card can raise IRQ5 again on
 * the next sb16_play_buffer() call. Nothing else needs calling this. */
void sb16_irq_ack(void);
```

- [ ] **Step 2: Implement both in `sb16.c`**

Add near the top of `kernel/drivers/sb16.c`, after the existing `#define`s:

```c
/* 8237 DMA controller #1 (channels 0-3, 8-bit). Channel 1 is what QEMU's
 * -device sb16 and every real SB16 default to for 8-bit playback. */
#define DMA1_MASK_REG 0x0A
#define DMA1_CLEAR_FF_REG 0x0C
#define DMA1_MODE_REG 0x0B
#define DMA1_CHAN1_ADDR_REG 0x02
#define DMA1_CHAN1_COUNT_REG 0x03
#define DMA1_CHAN1_PAGE_REG 0x83

#define DMA1_CHAN1_MASK_SET 0x05   /* bit2 (mask) | channel 1 */
#define DMA1_CHAN1_MASK_CLEAR 0x01 /* channel 1, mask bit clear */
/* Mode byte 0x49 = 0100_1001: bits6-7 = 01 (single-cycle mode),
 * bit4 = 0 (no auto-init), bit5 = 0 (address increment), bits2-3 = 10
 * ("read" transfer -- the DMA controller reads memory and writes the
 * device, i.e. playback), bits0-1 = 01 (channel 1). */
#define DMA1_CHAN1_MODE_SINGLE_READ 0x49

#define SB16_CMD_SPEAKER_ON 0xD1
#define SB16_CMD_SET_TIME_CONSTANT 0x40
#define SB16_CMD_8BIT_SINGLE_CYCLE_OUTPUT 0x14
```

Add these functions after `sb16_init()`:

```c
static int sb16_wait_write_ready(void) {
    int i;
    for (i = 0; i < SB16_TIMEOUT_ITERS; i++) {
        if (!(inb(SB16_DSP_WRITE) & 0x80)) {
            return 0;
        }
    }
    return -1;
}

/* Bounded wait, then write regardless -- same "report failure, don't hang
 * forever" discipline as ata.c, but there's no caller-visible error path
 * once sb16_init() has already succeeded, so a stuck DSP just gets a
 * best-effort write instead of freezing the kernel. */
static void sb16_dsp_write(unsigned char val) {
    sb16_wait_write_ready();
    outb(SB16_DSP_WRITE, val);
}

static void dma_program_channel1(unsigned int phys_addr, unsigned int len) {
    unsigned int count = len - 1;

    outb(DMA1_MASK_REG, DMA1_CHAN1_MASK_SET);
    outb(DMA1_CLEAR_FF_REG, 0);
    outb(DMA1_MODE_REG, DMA1_CHAN1_MODE_SINGLE_READ);

    outb(DMA1_CHAN1_ADDR_REG, (unsigned char)(phys_addr & 0xFF));
    outb(DMA1_CHAN1_ADDR_REG, (unsigned char)((phys_addr >> 8) & 0xFF));
    outb(DMA1_CHAN1_PAGE_REG, (unsigned char)((phys_addr >> 16) & 0xFF));

    outb(DMA1_CLEAR_FF_REG, 0);
    outb(DMA1_CHAN1_COUNT_REG, (unsigned char)(count & 0xFF));
    outb(DMA1_CHAN1_COUNT_REG, (unsigned char)((count >> 8) & 0xFF));

    outb(DMA1_MASK_REG, DMA1_CHAN1_MASK_CLEAR);
}

void sb16_play_buffer(const unsigned char *buf, unsigned int len, unsigned int sample_rate) {
    unsigned int count;
    unsigned char time_constant;

    if (!sb16_present || len == 0) {
        return;
    }

    dma_program_channel1((unsigned int)(unsigned long)buf, len);

    sb16_dsp_write(SB16_CMD_SPEAKER_ON);

    /* Classic DSP 1.xx-compatible time-constant formula -- every SB16
     * still honors it. Valid for the mono 8-bit rates this driver uses. */
    time_constant = (unsigned char)(256 - (1000000 / sample_rate));
    sb16_dsp_write(SB16_CMD_SET_TIME_CONSTANT);
    sb16_dsp_write(time_constant);

    count = len - 1;
    sb16_dsp_write(SB16_CMD_8BIT_SINGLE_CYCLE_OUTPUT);
    sb16_dsp_write((unsigned char)(count & 0xFF));
    sb16_dsp_write((unsigned char)((count >> 8) & 0xFF));
}

void sb16_irq_ack(void) {
    inb(SB16_DSP_READ_STATUS); /* reading this port is what acks the 8-bit IRQ */
}
```

- [ ] **Step 3: Add the IRQ5 handler to `kernel/arch/isr.c`**

Add the include near the top, next to the other driver includes:

```c
#include "keyboard.h"
#include "mouse.h"
#include "sb16.h"
```

Add the handler right after `irq12_mouse()`:

```c
__attribute__((interrupt)) static void irq5_sb16(struct interrupt_frame *frame) {
    (void)frame;
    sb16_irq_ack();
    pic_send_eoi_master();
}
```

In `interrupts_init()`'s IRQ vector loop, add an `else if` arm for vector 5
(IRQ5 is `< 8`, so it belongs on the master PIC exactly like IRQ1):

```c
    for (vector = 0; vector < 16; vector++) {
        void *handler;
        if (vector == 1) {
            handler = (void *)irq1_keyboard;
        } else if (vector == 5) {
            handler = (void *)irq5_sb16;
        } else if (vector == 12) {
            handler = (void *)irq12_mouse;
        } else if (vector < 8) {
            handler = (void *)irq_master_default;
        } else {
            handler = (void *)irq_slave_default;
        }
        idt_set_gate(0x20 + vector, handler, IDT_TYPE_INTERRUPT_GATE_32);
    }
```

In `interrupts_enable()`, unmask IRQ5 alongside the existing two:

```c
void interrupts_enable(void) {
    pic_clear_mask(1);  /* keyboard */
    pic_clear_mask(2);  /* cascade line -- required for IRQ8-15 to reach the CPU */
    pic_clear_mask(5);  /* sb16 */
    pic_clear_mask(12); /* mouse */
```

- [ ] **Step 4: Update `kernel/Makefile`'s `isr.o` dependency line**

Find:

```
isr.o: arch/isr.c arch/idt.h arch/pic.h arch/io.h arch/interrupts.h drivers/keyboard.h drivers/mouse.h gfx/graphics.h gfx/text.h
```

Append `drivers/sb16.h` to the end of it.

- [ ] **Step 5: Add the `BEEP` hook declaration**

In `kernel/forth/forth_hooks.h`, add next to `forth_hook_paint_open()`:

```c
void forth_hook_paint_open(void);
void forth_hook_beep(void);
```

- [ ] **Step 6: Add the `BEEP` primitive**

In `kernel/forth/forth.c`, add right after `prim_paint()`:

```c
static void prim_paint(struct forth_vm *vm) {
    (void)vm;
    forth_hook_paint_open();
}

static void prim_beep(struct forth_vm *vm) {
    (void)vm;
    forth_hook_beep();
}
```

Add it to the primitive table, next to `"PAINT"`:

```c
    {"PAINT", prim_paint}, {"BEEP", prim_beep}, {"PIXEL", prim_pixel}, {"MOUSE-X", prim_mouse_x}, {"MOUSE-Y", prim_mouse_y},
```

- [ ] **Step 7: Implement `forth_hook_beep()` in `kernel.c`**

Add the include next to `sb16.h` (already added in Task 1):

```c
#include "ata.h"
#include "sb16.h"
#include "fs.h"
```

(no change needed here — `sb16.h` is already included from Task 1; this
step just confirms it's there before Step 7 uses it.)

Add near `forth_hook_paint_open()` (same file, same area — both are
hook implementations grouped together):

```c
/* A short, fixed 400Hz square wave -- just enough to prove the hardware
 * path works, not a synth voice (see docs/superpowers/specs/2026-08-22-
 * sb16-audio-driver-design.md's explicit scope cut). Generated once, into
 * a .bss buffer, with pure integer arithmetic -- no floats needed for a
 * square wave, and .bss costs zero bytes in kernel.bin (objcopy drops it;
 * see arch/linker.ld's own comment on that), unlike a giant literal array
 * would have. aligned(4096): a 4096-aligned buffer of at most 4096 bytes
 * can never straddle the 64KB physical boundary ISA DMA can't cross,
 * since 65536 is itself a multiple of 4096. */
#define BEEP_SAMPLE_RATE 8000u
#define BEEP_FREQ_HZ 400u
#define BEEP_SAMPLES 2000u /* 250ms at 8000Hz */

static unsigned char beep_tone[BEEP_SAMPLES] __attribute__((aligned(4096)));
static int beep_tone_ready = 0;

static void beep_tone_generate(void) {
    unsigned int half_period = BEEP_SAMPLE_RATE / (2u * BEEP_FREQ_HZ);
    unsigned int i;
    for (i = 0; i < BEEP_SAMPLES; i++) {
        beep_tone[i] = ((i / half_period) % 2u == 0u) ? 160 : 96;
    }
    beep_tone_ready = 1;
}

void forth_hook_beep(void) {
    if (!beep_tone_ready) {
        beep_tone_generate();
    }
    sb16_play_buffer(beep_tone, BEEP_SAMPLES, BEEP_SAMPLE_RATE);
}
```

- [ ] **Step 8: Build**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd /home/norfolkh/os/kernel && make clean && make
cd /home/norfolkh/os/boot && make
```

Expected: both build cleanly, no warnings.

- [ ] **Step 9: Headless test — BEEP produces real, non-silent audio**

Check `ps aux | grep qemu-system` first, then refresh the scratch copies:

```bash
mkdir -p /tmp/sb16-test
cp /home/norfolkh/os/boot/disk.img /home/norfolkh/os/boot/fs.img /tmp/sb16-test/
rm -f /tmp/sb16-test/beep.wav /tmp/sb16-test/mon.sock
```

Launch headless with the `wav` audio backend and a monitor socket:

```bash
qemu-system-i386 -display none -m 32 \
  -drive file=/tmp/sb16-test/disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=/tmp/sb16-test/fs.img,format=raw,if=ide,bus=0,unit=1 \
  -device sb16,audiodev=snd0 -audiodev wav,id=snd0,path=/tmp/sb16-test/beep.wav \
  -monitor unix:/tmp/sb16-test/mon.sock,server,nowait &
sleep 1
```

Drive it via the monitor socket, one command per round-trip (per this
project's own established QEMU-testing convention — batching commands
drops keystrokes). Open the FORTH console via the start menu (same mouse
path this repo's own prior sessions have already verified against this
exact 640x480 layout), type `BEEP`, wait for the 250ms tone to finish, then
shut down cleanly (a WAV file's header only finalizes on a clean quit):

```python
import socket, time

def connect():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect("/tmp/sb16-test/mon.sock")
    time.sleep(0.3)
    s.recv(65536)
    return s

def cmd(s, text):
    s.sendall((text + "\n").encode())
    time.sleep(0.25)
    return s.recv(65536).decode(errors="replace")

def mouse_move(s, dx, dy):
    while dx != 0 or dy != 0:
        sx = max(-100, min(100, dx))
        sy = max(-100, min(100, dy))
        cmd(s, f"mouse_move {sx} {sy}")
        dx -= sx; dy -= sy
        time.sleep(0.15)

def click(s):
    cmd(s, "mouse_button 1"); time.sleep(0.15)
    cmd(s, "mouse_button 0"); time.sleep(0.15)

s = connect()
mouse_move(s, -288, 88)   # start button
click(s)
mouse_move(s, 23, -142)   # FORTH menu item
click(s)
mouse_move(s, 195, 64)    # FORTH console input field
click(s)
for ch in ["b", "e", "e", "p", "ret"]:
    cmd(s, f"sendkey {ch}")
    time.sleep(0.1)
time.sleep(0.5)  # let the 250ms tone finish playing
cmd(s, "quit")
print("DONE")
```

Inspect the resulting WAV file for real, non-silent audio:

```python
import wave, array

w = wave.open("/tmp/sb16-test/beep.wav", "rb")
n = w.getnframes()
sampwidth = w.getsampwidth()
raw = w.readframes(n)
typecode = {1: "B", 2: "h"}[sampwidth]
samples = array.array(typecode, raw)
silence = 128 if sampwidth == 1 else 0
max_dev = max(abs(s - silence) for s in samples) if samples else 0
print(f"frames={n} sampwidth={sampwidth} max_deviation_from_silence={max_dev}")
assert n > 0, "WAV file has no frames -- nothing was played"
assert max_dev > 5, "WAV file is silent -- BEEP produced no real audio"
print("PASS")
```

Expected: `PASS`, with `frames` > 0 and `max_deviation_from_silence`
clearly above the silence threshold.

- [ ] **Step 10: Commit**

```bash
git add kernel/drivers/sb16.h kernel/drivers/sb16.c kernel/arch/isr.c kernel/Makefile kernel/forth/forth_hooks.h kernel/forth/forth.c kernel/kernel.c
git commit -m "$(cat <<'EOF'
kernel: SB16 DMA playback + BEEP word, proving real audio output

sb16_play_buffer() programs the 8237 DMA controller's channel 1 for a
single-cycle 8-bit transfer and starts DSP playback; a new IRQ5
handler acks the card's completion interrupt. BEEP (mirroring PAINT's
own bare-word shape) generates a short fixed 400Hz square wave into a
.bss buffer on first use and plays it -- proof the hardware output
path works, with no oscillators/mixing/synthesis yet (that's the next
spec). Verified headlessly via QEMU's wav audiodev backend: real
non-silent samples land in the resulting file.
EOF
)"
```

---

## Follow-up (not part of this plan)

Once both tasks are verified, update `docs/IDEAS.md`'s Audio entry to note
this milestone is done and describe what's left (the software synthesizer,
sub-project B), and append a `docs/BUILD_LOG.md` entry. Sub-project B (an
actual multi-voice SID-like synthesizer) and C (a Forth/language control
surface) are separate future specs, not part of this plan.
