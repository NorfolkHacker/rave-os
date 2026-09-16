#ifndef ACID_HAL_INPUT_H
#define ACID_HAL_INPUT_H

#include <stdbool.h>

/* Returns nonzero once the platform wants the app to exit (e.g. the sim
 * window was closed). Bring-up doesn't do real keyboard/mouse/touch
 * events yet -- that's the windowing/GUI phase (roadmap item 2). */
int hal_input_should_quit( void );

/* Polls the platform's single touch point. *pressed is set true/false;
 * *x/*y are only meaningful when *pressed is true. One touch point only --
 * this project's whole input vocabulary for the windowing phase (see the
 * windowing design spec's "input model" decision). */
void hal_input_poll_touch( int * x, int * y, bool * pressed );

/* Polls the keyboard for at most one fresh key-press transition since the
 * last call. Unlike hal_input_poll_touch's level-triggered "is it down
 * right now" contract, this is edge-triggered: *pressed is true exactly
 * when a new key was pressed since the last poll (never for a release, and
 * never for a key that was already held down last poll -- no repeat-while-
 * held in this first pass, see the phase 5 keyboard design spec).
 * *keycode is only meaningful when *pressed is true, same convention as
 * hal_input_poll_touch. A keycode is either a plain printable ASCII value
 * (32-126) or one of the KERNEL_KEY_* named constants in hal_keycode.h for
 * non-printable keys. */
void hal_input_poll_key( int * keycode, bool * pressed );

#endif
