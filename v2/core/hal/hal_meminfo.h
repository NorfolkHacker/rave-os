#ifndef ACID_HAL_MEMINFO_H
#define ACID_HAL_MEMINFO_H

/* This process's/device's own real memory footprint, in KB, or -1 if this
 * target has no way to report it. Deliberately not FreeRTOS heap free/used
 * (this project's sim build uses heap_3.c, a thin malloc/free wrapper with
 * no free-space tracking at all -- confirmed by its own source having no
 * xPortGetFreeHeapSize implementation), so this goes straight to whatever
 * each real platform actually offers instead. */
long hal_meminfo_used_kb( void );

#endif
