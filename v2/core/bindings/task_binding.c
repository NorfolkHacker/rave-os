#include "task_binding.h"
#include "../hal/hal_meminfo.h"

#include "FreeRTOS.h"
#include "task.h"

#include "mruby/array.h"

/* Real FreeRTOS task list + per-task CPU% for a system-monitor app --
 * every task the scheduler actually knows about (the router, the timer
 * service, IDLE, and one per open app window), not just the windowed
 * apps kernel_window.c already tracks. CPU% needs two samples to mean
 * anything (it's a share of elapsed time, not an instantaneous value),
 * so acid_refresh_tasks takes one fresh snapshot, diffs it against
 * whatever the previous call captured (matched by task handle, since
 * tasks can come and go between calls), and caches the result for
 * acid_task_count/acid_task_info to read back by index -- the same
 * refresh-then-index-by-row shape acid_window_info's caller already
 * uses, so a monitor app's own polling code looks the same either way. */

#define MAX_TRACKED_TASKS 16

struct task_sample
{
    TaskHandle_t handle;
    char name[ configMAX_TASK_NAME_LEN ];
    eTaskState state;
    configRUN_TIME_COUNTER_TYPE runtime;
    int cpu_percent;
};

static struct task_sample g_prev[ MAX_TRACKED_TASKS ];
static int g_prev_count = 0;
static configRUN_TIME_COUNTER_TYPE g_prev_total = 0;

static struct task_sample g_cur[ MAX_TRACKED_TASKS ];
static int g_cur_count = 0;

static const char *
state_name( eTaskState state )
{
    switch( state )
    {
        case eRunning:   return "running";
        case eReady:     return "ready";
        case eBlocked:   return "blocked";
        case eSuspended: return "suspended";
        case eDeleted:   return "deleted";
        default:         return "?";
    }
}

/* Finds this handle's runtime from the PREVIOUS sample, or -1 if it
 * wasn't there (a task that's new since the last refresh) -- a caller
 * with no previous sample for a handle can't compute a delta, so it
 * reports 0% for that task just this once rather than a nonsense value. */
static int
prev_runtime_for( TaskHandle_t handle, configRUN_TIME_COUNTER_TYPE * out )
{
    int i;
    for( i = 0; i < g_prev_count; i++ )
    {
        if( g_prev[ i ].handle == handle )
        {
            *out = g_prev[ i ].runtime;
            return 1;
        }
    }
    return 0;
}

static mrb_value
acid_refresh_tasks( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;

    TaskStatus_t status[ MAX_TRACKED_TASKS ];
    configRUN_TIME_COUNTER_TYPE total_runtime = 0;
    UBaseType_t n = uxTaskGetSystemState( status, MAX_TRACKED_TASKS, &total_runtime );

    configRUN_TIME_COUNTER_TYPE total_delta = total_runtime - g_prev_total;

    UBaseType_t i;
    for( i = 0; i < n; i++ )
    {
        struct task_sample * dst = &g_cur[ i ];
        dst->handle = status[ i ].xHandle;
        /* pcTaskName may be shorter than configMAX_TASK_NAME_LEN -- this
         * codebase's own script paths (e.g. "v2/apps/piano.rb") can also
         * be LONGER than it, in which case FreeRTOS already truncated it
         * at task-creation time; nothing more to do here either way. A
         * plain byte copy with an explicit NUL, not strncpy (which
         * leaves the destination unterminated if the source fills the
         * whole buffer with no room for one). */
        int j = 0;
        while( j < configMAX_TASK_NAME_LEN - 1 && status[ i ].pcTaskName[ j ] != '\0' )
        {
            dst->name[ j ] = status[ i ].pcTaskName[ j ];
            j++;
        }
        dst->name[ j ] = '\0';
        dst->state = status[ i ].eCurrentState;
        dst->runtime = status[ i ].ulRunTimeCounter;

        configRUN_TIME_COUNTER_TYPE prev_runtime;
        if( total_delta > 0 && prev_runtime_for( dst->handle, &prev_runtime ) )
        {
            configRUN_TIME_COUNTER_TYPE task_delta = dst->runtime - prev_runtime;
            dst->cpu_percent = ( int ) ( ( task_delta * 100 ) / total_delta );
        }
        else
        {
            dst->cpu_percent = 0;
        }
    }
    g_cur_count = ( int ) n;

    /* This sample becomes "previous" for the next call. */
    for( i = 0; i < n; i++ )
    {
        g_prev[ i ] = g_cur[ i ];
    }
    g_prev_count = ( int ) n;
    g_prev_total = total_runtime;

    return mrb_fixnum_value( g_cur_count );
}

static mrb_value
acid_task_count( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( g_cur_count );
}

static mrb_value
acid_task_info( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );
    if( index < 0 || index >= g_cur_count )
    {
        return mrb_nil_value();
    }
    struct task_sample * s = &g_cur[ index ];
    mrb_value values[ 3 ];
    values[ 0 ] = mrb_str_new_cstr( mrb, s->name );
    values[ 1 ] = mrb_str_new_cstr( mrb, state_name( s->state ) );
    values[ 2 ] = mrb_fixnum_value( s->cpu_percent );
    return mrb_ary_new_from_values( mrb, 3, values );
}

static mrb_value
acid_mem_used_kb( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( ( mrb_int ) hal_meminfo_used_kb() );
}

void
acid_task_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_refresh_tasks",
                                 acid_refresh_tasks, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_task_count",
                                 acid_task_count, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_task_info",
                                 acid_task_info, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_mem_used_kb",
                                 acid_mem_used_kb, MRB_ARGS_NONE() );
}
