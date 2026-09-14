#ifndef ACID_GFX_H
#define ACID_GFX_H

#include "FreeRTOS.h"
#include "semphr.h"

void gfx_init( void );
void gfx_fill_rect( int x, int y, int w, int h, unsigned int color );
void gfx_fill_circle( int x, int y, int r, unsigned int color );

/* Accessor for the mutex created inside gfx_init() that serializes access
 * to the underlying HAL display object, which is shared (unsynchronized at
 * the HAL layer) across every app task and the router task. Callers that
 * touch the same underlying object outside of gfx_fill_rect/gfx_fill_circle
 * (currently: the sim's hal_input_poll_touch, which reads touch state off
 * the same LGFX object) must take/give this same lock around their access. */
SemaphoreHandle_t gfx_get_lock( void );

#endif
