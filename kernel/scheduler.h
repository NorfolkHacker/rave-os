#ifndef RAVEOS_SCHEDULER_H
#define RAVEOS_SCHEDULER_H

/* Cooperative fiber scheduler: a fixed pool of program slots, each
 * with its own static stack, switched via context_switch.asm's
 * context_switch(). No malloc, no paging, no ring 3, no preemption --
 * see docs/superpowers/specs/2026-08-19-concurrency-design.md for the
 * full design and what's deliberately out of scope. Self-contained,
 * same isolation discipline forth.c already holds itself to: no
 * graphics.h/window.h/etc. here, so this stays reusable by any future
 * non-Forth program and host-testable without a kernel build. */

#define MAX_PROGRAMS 4

/* If a program hasn't finished on its own within this many more
 * scheduler_tick() calls after scheduler_request_close(), it's
 * force-freed -- see scheduler_request_close()'s own comment. */
#define SCHEDULER_CLOSE_GRACE_FRAMES 60

/* Reserves a free slot without starting it -- lets a caller build up
 * an entry function's argument (typically into its own slot-indexed
 * array) using the returned index before the slot can possibly be
 * switched into, closing the gap between "which slot did I get" and
 * "what should it run". Returns the slot index, or -1 if all
 * MAX_PROGRAMS slots are in use. */
int scheduler_reserve(const char *name);

/* Primes slot's stack so it starts at entry(arg) the next time
 * scheduler_tick() switches into it, and marks it runnable. slot must
 * be a value scheduler_reserve() just returned, not yet activated. */
void scheduler_activate(int slot, void (*entry)(void *arg), void *arg);

/* Callable only from inside a running program (scheduler_current_slot()
 * >= 0) -- switches back to whichever context called into this
 * program, resuming here on this program's next scheduler_tick()
 * slice. */
void scheduler_yield(void);

/* Called once per kmain() frame. Gives every runnable slot one slice:
 * switches in, runs until that program calls scheduler_yield() or its
 * entry function returns, then moves to the next slot. Also advances
 * close-request timeouts (see scheduler_request_close()). */
void scheduler_tick(void);

/* -1 outside any program; otherwise the slot index currently
 * mid-switch. Lets a hook (e.g. forth_hook_paint_open()) learn which
 * program it's running inside of without any string-matching on how
 * that program was launched. */
int scheduler_current_slot(void);

/* Starts the close-then-timeout sequence for slot: if the program is
 * still running SCHEDULER_CLOSE_GRACE_FRAMES scheduler_tick() calls
 * from now, it's force-freed outright (its stack abandoned -- safe
 * here specifically because nothing in this kernel holds an open
 * resource across calls). A slot that isn't currently running is
 * unaffected. Idempotent: calling this again on a slot whose timeout
 * is already counting down does not restart the count. */
void scheduler_request_close(int slot);

/* 1 if any program slot is reserved or running, 0 if the whole table
 * is empty. This kernel has no timer interrupt (isr.c/pic.c only wire
 * keyboard/mouse IRQs) -- kmain()'s idle path halts the CPU until the
 * next hardware interrupt when nothing changed on screen, which would
 * starve a running program of scheduler_tick() calls until the user
 * next touches the mouse or keyboard. kmain() checks this before
 * halting. */
int scheduler_any_active(void);

#endif
