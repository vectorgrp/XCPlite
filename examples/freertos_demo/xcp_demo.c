// XCP FreeRTOS demo application

#include "assert.h"
#include <math.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#ifdef OPTION_CMSIS
#include "cmsis_os2.h"
#endif

#if !defined(FREE_RTOS_POSIX_SIM)
#include "clock64.h"
#endif

#include "xcp_demo.h"

//----------------------------------------------------------------------------------------------------
// XCP

// XCPlite headers
#ifdef __cplusplus
#include "xcplib.hpp" // for libxcplite C++ application programming interface
#else
#include "xcplib.h" // for libxcplite C application programming interface
#endif

// XCPlite parameters
#define XCP_PROJECT_NAME "freertos_demo"
#define XCP_PROJECT_VERSION "V102"
#define XCP_USE_TCP false
#define XCP_SERVER_PORT 5555
#define XCP_QUEUE_SIZE 0 // The queue size is derived from OPTION_QUEUE_32_SEGMENT_COUNT for the 32-bit FreeRTOS build; this parameter is ignored
#define XCP_LOG_LEVEL 4  // 3 - Info, 4 - Print XCP commands, 5 - Debug

bool startXcpServer() {

    XcpSetLogLevel(XCP_LOG_LEVEL);
    XcpCreateEpk(XCP_PROJECT_VERSION);

    // Initialize XCP protocol layer
    const uint8_t bindAny[4] = {0, 0, 0, 0};
    if (!XcpInit(XCP_PROJECT_NAME, XCP_PROJECT_VERSION, XCP_MODE_LOCAL)) {
        printf("XcpInit failed\n");
        return false;
    }

    // Register the high resolution 64-bit clock function implemented by Clock64_Get() in clock64.c as XCP DAQ clock
#if !defined(FREE_RTOS_POSIX_SIM)
    Clock64_Init();
    ApplXcpRegisterGetClockCallback(Clock64_Get);
    ApplXcpRegisterIdleCallback(Clock64_Update);
#endif

    // Initialize XCP Ethernet server
    if (!XcpEthServerInit(bindAny, XCP_SERVER_PORT, XCP_USE_TCP, XCP_QUEUE_SIZE)) {
        printf("XcpEthServerInit failed\n");
        return false;
    }

    return true;
}

//----------------------------------------------------------------------------------------------------
// Demo tasks

// Task helper functions

#ifdef OPTION_CMSIS

osThreadId_t fastTaskHandle;
const osThreadAttr_t fastTaskHandle_attributes = {
    .name = "fastTask",
    .stack_size = FASTTASK_STACKSIZE,
    .priority = (osPriority_t)FASTTASK_PRIORITY,
};

osThreadId_t slowTaskHandle;
const osThreadAttr_t slowTaskHandle_attributes = {
    .name = "fastTask",
    .stack_size = SLOWTASK_STACKSIZE,
    .priority = (osPriority_t)SLOWTASK_PRIORITY,
};

#else

TaskHandle_t fastTaskHandle;
TaskHandle_t slowTaskHandle;

#endif

#ifdef OPTION_CMSIS
static bool createDemoTask(TaskFunction_t taskCode, const char *name, const uint32_t stackDepth, UBaseType_t priority, osThreadId_t *taskHandle) {
    if (priority > SLOWTASK_PRIORITY)
        *taskHandle = osThreadNew(taskCode, NULL, &fastTaskHandle_attributes);
    else
        *taskHandle = osThreadNew(taskCode, NULL, &slowTaskHandle_attributes);
    return *taskHandle != NULL;
#else
static bool createDemoTask(TaskFunction_t taskCode, const char *name, const uint32_t stackDepth, UBaseType_t priority, TaskHandle_t *taskHandle) {
#ifdef DEMO_TASK_CORE
    return (pdPASS == xTaskCreatePinnedToCore(taskCode, name, stackDepth, NULL, priority, taskHandle, DEMO_TASK_CORE));
#else
    return (pdPASS == xTaskCreate(taskCode, name, stackDepth, NULL, priority, taskHandle));
#endif
#endif
}

//----------------------------------------------------------------------------------------------------
// Measurement variables

// Global measurement values
uint16_t global_counter = 0;

// Sine signal on variable channel1
#define SLOWTASK_PHASE_STEP_RAD 0.001f
#define SINE_PERIOD_RAD 6.28318530717958647692f
float channel1 = 0.0f;

static uint32_t fastTaskOverruns = 0;
static uint32_t slowTaskOverruns = 0;

//----------------------------------------------------------------------------------------------------
// Calibration parameters

// Global calibration parameter constants
struct parameters {
    uint32_t fast_task_period_ms; // Period of measurement task 1 in milliseconds
    uint32_t slow_task_period_ms; // Period of measurement task 2 in milliseconds
    uint16_t counter_max;         // Counter wrap-around value for the global_counter incremented in fastTask
    float amplitude;              // Amplitude for the sine signal generator in slowTask
};

// Default calibration parameters (default/reference page)
// &parameters is the A2l file address of the calibration parameter segment 'parameters'
// Typename and variable name must be identical
const struct parameters parameters = {
    .fast_task_period_ms = 1, // 1 ms = 1000 Hz
    .slow_task_period_ms = 2, // 2 ms = 500 Hz
    .counter_max = 1000,
    .amplitude = 1.0f,
};

// Declare a calibration segment that wraps 'parameters' for thread-safe and consistent access.
// This creates:
//  - a linker-section 'xcp_cals' descriptor used by XcpInit() for registration
//  - an internal calibration segment index initialized by XcpInit()
//  - the typed C++ handle 'parameters_calseg' used by the tasks below
// The offline A2L generator currently assumes that the struct type name and default-parameter variable name are identical.
#ifdef __cplusplus
CalSegDeclRef(parameters, parameters_calseg);
#else
CalSegDecl(parameters);
#endif

// clamp_parameter calibration parameters to a given value range
#define clamp_parameter(x, y, min, max)                                                                                                                                            \
    do {                                                                                                                                                                           \
        if ((y) < (min))                                                                                                                                                           \
            (x) = (min);                                                                                                                                                           \
        else if ((y) > (max))                                                                                                                                                      \
            (x) = (max);                                                                                                                                                           \
        else                                                                                                                                                                       \
            (x) = (y);                                                                                                                                                             \
    } while (0)

//----------------------------------------------------------------------------------------------------
// Tasks

// High priority fast task
static void fastTask(void *parameter) {
    (void)parameter;

    // Volatile keeps this local measurement variable visible in optimized builds,
    // The offline A2L generator can discover it in the ELF file and associate it to the functions DAQ event trigger.
    volatile uint16_t counter = 0;
    static volatile uint16_t static_counter = 0; 

    printf("fastTask started\n");
    printf("  frameaddr = %p\n", xcp_get_frame_addr());
    printf("  &counter = %p\n", &counter);
    printf("  &static_counter = %p\n", &static_counter);

    // Create a DAQ event named 'fastTask'
    DaqCreateEvent(fastTask);

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

        uint32_t period_ms;

        // Flash IO PIN
#ifdef OPTION_IO
        setPin1();
#endif

        // Lock the calibration segment 'parameters' for thread-safe and consistent access
        // There is no blocking mutex hold during the lock, only atomics used.
        {
#ifdef __cplusplus
            auto params = parameters_calseg.lock();
#else
            struct parameters *params = (struct parameters *)CalSegLock(parameters);
#endif

            // Save the task period parameter, don't delay during the lock to give XCP a chance to modify the parameters.
            clamp_parameter(period_ms, params->fast_task_period_ms, FASTTASK_PERIOD_MIN_MS, FASTTASK_PERIOD_MAX_MS);

            counter++;
            static_counter++;
            if (counter > params->counter_max) {
                counter = 0;
                static_counter = 0;
            }
            global_counter++;
            if (global_counter > params->counter_max) {
                global_counter = 0;
            }

#ifndef __cplusplus
            CalSegUnlock(parameters);
#endif
        }

        // Trigger the DAQ event 'fastTask'
        DaqTriggerEvent(fastTask);
        // Trigger the event a second time to measure runtime of the DaqTriggerEvent function
        // XcpEventExt_Var(trg__AAS__fastTask, 1, xcp_get_frame_addr()); 

#ifdef OPTION_IO
        rstPin1();
#endif

        // Sleep until next wakeup time, check for overruns
        const BaseType_t delayed = xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(period_ms));
        if (delayed == pdFALSE) {
            fastTaskOverruns++;
        }
    }
}

// Low priority slow task
static void slowTask(void *parameter) {
    (void)parameter;

    volatile uint16_t counter = 0;
    float phase = 0.0f;
    uint32_t slow_task_period_ms;
    uint32_t fast_task_period_ms;
    (void)fast_task_period_ms;

    printf("slowTask started\n");
    printf("  frameaddr = %p\n", xcp_get_frame_addr());
    printf("  &counter = %p\n", &counter);

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

#ifdef OPTION_IO
        setPin2();
#endif

        {
#ifdef __cplusplus
            auto params = parameters_calseg.lock();
#else
            struct parameters *params = (struct parameters *)CalSegLock(parameters);
#endif

            clamp_parameter(slow_task_period_ms, params->slow_task_period_ms, SLOWTASK_PERIOD_MIN_MS, SLOWTASK_PERIOD_MAX_MS);
            fast_task_period_ms = params->fast_task_period_ms;

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }

            channel1 = params->amplitude * sinf(phase);
            phase += SLOWTASK_PHASE_STEP_RAD;
            if (phase >= SINE_PERIOD_RAD) {
                phase -= SINE_PERIOD_RAD;
            }

#ifndef __cplusplus
            CalSegUnlock(parameters);
#endif
        }

        DaqCreateAndTriggerEvent(slowTask);

        // printf("slowTask: counter = %u, period = %u ms, channel1 = %f\n", counter, slow_task_period_ms, channel1);
#ifdef OPTION_DISPLAY
        displayUpdate(slow_task_period_ms, counter, fast_task_period_ms, global_counter);
#endif

#ifdef OPTION_IO
        rstPin2();
#endif

        const BaseType_t delayed = xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(slow_task_period_ms));
        if (delayed == pdFALSE) {
            slowTaskOverruns++;
        }
    }
}

//----------------------------------------------------------------------------------------------------
// Init and start demo tasks

static void test(void) {

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {

        vTaskDelay(pdMS_TO_TICKS(200));

        // Check timestamp resolution and XCP clock
        for (int i = 0; i < 20; i++) {
            void Clock64_Update(void);
            Clock64_Update();

            uint64_t t1 = ApplXcpGetClock64();
            vTaskDelay(pdMS_TO_TICKS(1000));
            uint64_t t2 = ApplXcpGetClock64();
            printf("XCP clock resolution check: t1=%llu, t2=%llu, dt=%llu\n", t1, t2, t2 - t1);
        }
    } else {
        printf("Scheduler not running, skipping XCP clock check\n");
    }
}

static bool startXcpDemoTasks() {

    printf("Start demo tasks\n");
    printf("&global_counter = %p\n", &global_counter);
    printf("&parameters = %p\n", &parameters);

    if (!createDemoTask(fastTask, "fastTask", FASTTASK_STACKSIZE, FASTTASK_PRIORITY, &fastTaskHandle)) {
        printf("Failed to create fastTask\n");
        return false;
    }

    if (!createDemoTask(slowTask, "slowTask", SLOWTASK_STACKSIZE, SLOWTASK_PRIORITY, &slowTaskHandle)) {
        printf("Failed to create slowTask\n");
        return false;
    }

    return true;
}

bool xcp_demo_init(void) {

    printf("xcp_demo_init\r\n");
    printf("FreeRTOS XCP on ETH Demo\r\n");
    printf("  Scheduler running = %u\r\n", xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    printf("  Timebase 1 ms = %u ticks\r\n", (unsigned int)pdMS_TO_TICKS(1));

    // Start XCP server
    if (!startXcpServer()) {
        printf("XCP server startup failed\r\n");
        return false;
    }

    // test();

    // Start the demo
    if (!startXcpDemoTasks()) {
        printf("XCP demo start failed\r\n");
        return false;
    }

    return true;
}
