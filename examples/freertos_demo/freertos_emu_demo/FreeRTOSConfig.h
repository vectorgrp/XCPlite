#pragma once

/*----------------------------------------------------------------------------
| File:
|   FreeRTOSConfig.h
|
| Description:
|   FreeRTOS kernel configuration for the XCPlite FreeRTOS POSIX simulator demo.
|   Tuned for macOS/Linux development and testing.
|   See https://www.freertos.org/a00110.html for full documentation.
|
 ----------------------------------------------------------------------------*/

/* ----- Scheduler settings ----- */
#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE 0
#define configCPU_CLOCK_HZ ((unsigned long)1000000000) /* Not used on POSIX port */
#define configTICK_RATE_HZ ((TickType_t)1000)          /* 1ms tick */
#define configMAX_PRIORITIES (7)
#define configMINIMAL_STACK_SIZE ((unsigned short)4096)
#define configMAX_TASK_NAME_LEN (16)
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 3
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 10
#define configUSE_QUEUE_SETS 0
#define configUSE_TIME_SLICING 1
#define configUSE_NEWLIB_REENTRANT 0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5
#define configSTACK_DEPTH_TYPE uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE size_t

/* ----- Memory allocation ----- */
#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
/* heap_3 delegates to system malloc/free, so size is unlimited */
#define configTOTAL_HEAP_SIZE ((size_t)(1024UL * 1024UL))
#define configAPPLICATION_ALLOCATED_HEAP 0

/* ----- Hook functions (all disabled for simplicity) ----- */
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

/* ----- Run-time stats / trace ----- */
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* ----- Co-routines (not used) ----- */
#define configUSE_CO_ROUTINES 0
#define configMAX_CO_ROUTINE_PRIORITIES 2

/* ----- Software timers ----- */
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2)

/* ----- Optional API functions ----- */
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xResumeFromISR 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle 0
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xEventGroupSetBitFromISR 1
#define INCLUDE_xTimerPendFunctionCall 0
#define INCLUDE_xTaskAbortDelay 1
#define INCLUDE_xTaskGetHandle 1
#define INCLUDE_xTaskResumeFromISR 1

/* ----- POSIX simulator specific ----- */
/* The POSIX port suspends/resumes tasks by sending SIGUSR1/SIGUSR2 to specific
   threads via pthread_kill. No process-wide signals are used for scheduling, so
   SIGINT (Ctrl-C) still reaches the main process and terminates it cleanly. */
#define configSIGNAL_INTERRUPT_MASK 0
