#ifndef ACID_KERNEL_EVENT_H
#define ACID_KERNEL_EVENT_H

enum kernel_event_type
{
    KERNEL_EVENT_TOUCH = 0,
    KERNEL_EVENT_CLOSE = 1,
    KERNEL_EVENT_MOVED = 2,
    /* x carries the keycode (hal_keycode.h), pressed is always 1 (only
     * presses are ever generated this phase, see hal_input_poll_key's
     * edge-triggered contract), y is unused. */
    KERNEL_EVENT_KEY = 3
};

struct kernel_event
{
    int type;
    int x;
    int y;
    int pressed;
};

#endif
