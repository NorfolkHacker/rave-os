#ifndef ACID_KERNEL_EVENT_H
#define ACID_KERNEL_EVENT_H

enum kernel_event_type
{
    KERNEL_EVENT_TOUCH = 0,
    KERNEL_EVENT_CLOSE = 1,
    KERNEL_EVENT_MOVED = 2
};

struct kernel_event
{
    int type;
    int x;
    int y;
    int pressed;
};

#endif
