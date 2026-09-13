#ifndef ACID_HAL_INPUT_H
#define ACID_HAL_INPUT_H

/* Returns nonzero once the platform wants the app to exit (e.g. the sim
 * window was closed). Bring-up doesn't do real keyboard/mouse/touch
 * events yet -- that's the windowing/GUI phase (roadmap item 2). */
int hal_input_should_quit( void );

#endif
