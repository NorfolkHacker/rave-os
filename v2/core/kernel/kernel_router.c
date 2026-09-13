#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "kernel_router.h"
#include "kernel_window.h"
#include "kernel_event.h"
#include "kernel_layout.h"
#include "../hal/hal_input.h"

enum drag_mode
{
    DRAG_NONE = 0,
    DRAG_MOVE = 1
};

static enum drag_mode g_drag_mode = DRAG_NONE;
static void * g_drag_task = NULL;
static int g_drag_offset_x = 0;
static int g_drag_offset_y = 0;
static bool g_was_pressed = false;

static void
send_event( struct kernel_window * win, int type, int x, int y, int pressed )
{
    if( win == NULL || win->queue == NULL )
    {
        return;
    }
    struct kernel_event ev;
    ev.type = type;
    ev.x = x;
    ev.y = y;
    ev.pressed = pressed;
    xQueueSendToBack( ( QueueHandle_t ) win->queue, &ev, 0 );
}

static void
kernel_router_poll( void )
{
    int x, y;
    bool pressed;
    hal_input_poll_touch( &x, &y, &pressed );

    bool fresh_press = pressed && !g_was_pressed;
    bool fresh_release = !pressed && g_was_pressed;
    g_was_pressed = pressed;

    if( g_drag_mode == DRAG_MOVE )
    {
        struct kernel_window * win = kernel_window_by_task( g_drag_task );
        if( win == NULL || !pressed )
        {
            g_drag_mode = DRAG_NONE;
            g_drag_task = NULL;
        }
        else
        {
            win->x = x - g_drag_offset_x;
            win->y = y - g_drag_offset_y;
        }
        return;
    }

    if( fresh_press )
    {
        struct kernel_window * win = kernel_window_find_at( x, y );
        if( win == NULL )
        {
            return;
        }

        kernel_window_bring_to_front( win->task );

        int rel_x = x - win->x;
        int rel_y = y - win->y;

        if( rel_y < KERNEL_TITLE_BAR_H )
        {
            g_drag_mode = DRAG_MOVE;
            g_drag_task = win->task;
            g_drag_offset_x = rel_x;
            g_drag_offset_y = rel_y;
            return;
        }

        send_event( win, KERNEL_EVENT_TOUCH, rel_x, rel_y, 1 );
        return;
    }

    if( pressed )
    {
        struct kernel_window * win = kernel_window_find_at( x, y );
        if( win != NULL )
        {
            send_event( win, KERNEL_EVENT_TOUCH, x - win->x, y - win->y, 1 );
        }
        return;
    }

    if( fresh_release )
    {
        struct kernel_window * win = kernel_window_find_at( x, y );
        if( win != NULL )
        {
            send_event( win, KERNEL_EVENT_TOUCH, x - win->x, y - win->y, 0 );
        }
    }
}

void
kernel_router_task( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        if( hal_input_should_quit() )
        {
            exit( 0 );
        }
        kernel_router_poll();
        vTaskDelay( pdMS_TO_TICKS( 16 ) );
    }
}
