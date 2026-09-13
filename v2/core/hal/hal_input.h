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

#endif
