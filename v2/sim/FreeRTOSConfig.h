#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <pthread.h>

#define configUSE_PREEMPTION                       1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    0
#define configUSE_TICKLESS_IDLE                    0
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configTICK_RATE_HZ                         ( 1000 )
#define configMINIMAL_STACK_SIZE                   ( PTHREAD_STACK_MIN )
#define configMAX_TASK_NAME_LEN                    ( 16 )
#define configUSE_16_BIT_TICKS                     0
#define configIDLE_SHOULD_YIELD                    1
#define configUSE_MUTEXES                          1
#define configUSE_RECURSIVE_MUTEXES                1
#define configUSE_COUNTING_SEMAPHORES              1
#define configQUEUE_REGISTRY_SIZE                  10
#define configUSE_QUEUE_SETS                       0
#define configUSE_TASK_NOTIFICATIONS               1
/* Both needed for uxTaskGetSystemState (kernel_task_binding.c) to return
 * real data -- TRACE_FACILITY for TaskStatus_t/the API to exist at all,
 * GENERATE_RUN_TIME_STATS for its ulRunTimeCounter field to be anything
 * but permanently 0. */
#define configUSE_TRACE_FACILITY                   1
#define configGENERATE_RUN_TIME_STATS              1
#define configUSE_STATS_FORMATTING_FUNCTIONS       0
#define configCHECK_FOR_STACK_OVERFLOW             0
#define configUSE_MALLOC_FAILED_HOOK               0
#define configTOTAL_HEAP_SIZE                      ( ( size_t ) ( 4 * 1024 * 1024 ) )
#define configMAX_PRIORITIES                       ( 7 )
#define configSUPPORT_STATIC_ALLOCATION            0
#define configSUPPORT_DYNAMIC_ALLOCATION           1
#define configRECORD_STACK_HIGH_ADDRESS            1

#define configUSE_TIMERS                           1
#define configTIMER_TASK_PRIORITY                  ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                   10
#define configTIMER_TASK_STACK_DEPTH               ( configMINIMAL_STACK_SIZE * 2 )

#define configUSE_CO_ROUTINES                      0
#define configMAX_CO_ROUTINE_PRIORITIES            ( 2 )

#define INCLUDE_vTaskPrioritySet                   1
#define INCLUDE_uxTaskPriorityGet                  1
#define INCLUDE_vTaskDelete                        1
#define INCLUDE_vTaskSuspend                       1
#define INCLUDE_vTaskDelayUntil                    1
#define INCLUDE_vTaskDelay                         1
#define INCLUDE_xTaskGetSchedulerState             1
#define INCLUDE_xTaskGetIdleTaskHandle             1
#define INCLUDE_xTaskGetHandle                     1
#define INCLUDE_eTaskGetState                      1

void vAssertCalled( const char * pcFile, unsigned long ulLine );
#define configASSERT( x )    if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )

#endif /* FREERTOS_CONFIG_H */
