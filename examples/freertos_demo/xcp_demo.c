// XCP FreeRTOS demo application

// This example code supports runtime or link time event and calibration segment registration (OPTION_SECTION_REGISTRATION)
// Link time registration is strongly recommended for embedded targets,
// but it may be platform specific, to assure the memory sections with the static event and segment descriptors are kept by the linker.

// With section registration (the default of the rtos configuration):
// The macros put constant event and segment descriptors into the linker sections xcp_evts and xcp_cals. XcpInit() creates the events
// and segments from them, the event id is the position of the descriptor in the section, known at link time, so the A2L file can be
// generated offline from the ELF file.
// What has to be considered:
// - The linker must keep the sections and provide the symbols __start_xcp_evts/__stop_xcp_evts and __start_xcp_cals/__stop_xcp_cals.
//   GNU ld does this automatically (with lld it depends on -z start-stop-gc), a custom linker script must KEEP the sections and define
//   the symbols around them. The 'used' attribute of the descriptors does not prevent the linker from removing them.
//   The xcp_meta section (XCP_UNIT, XCP_LIMITS, ...) is not referenced by the program and needs KEEP as well.
//   freertos_esp32_demo/extra_linker_script.py shows this for ESP-IDF (and the ESP32 specific DROM segment pitfall).
// - Nothing else between the boundary symbols: the section sizes must be multiples of 16 bytes (xcp_evts) and 32 bytes (xcp_cals).
// - Create each event in one place. An event name created at two places shifts the ids of all following events, their triggers then
//   silently use wrong event ids. To trigger an event in several functions, use DaqDeclareEvent at file scope and DaqTriggerEvent.
// - The event ids and segment numbers belong to one build, generate the A2L file again for each build. The EPK check detects a wrong
//   A2L file only if the EPK changes with each build.
// How to check it works:
// - readelf -S <elf-file> | grep xcp_   (or nm <elf-file> | grep __start_xcp_, if the linker script merged the sections)
// - Target log at XcpInit (log level 3): "Preregistered N events ..." and "Preregistered N new calibration segments ...",
//   not "No xcp_evts section found", "No xcp_cals section found for pre-registration" or "Event 'x' is created at more than one place"
// - xcpclient log of the A2L generation: no "undefined event id" and no "Event 'x' is defined N times" warnings
// - Load the A2L file with xcpclient connected to the target: no "Event id of 'x' differs" or "Calibration segment index ... differs"
// Details: docs/OFFLINE_A2L.md, "With section registration: requirements and checks"

// Without section registration, the event ids and calibration segment numbers are the order of creation at runtime.
// They are not known at link time, the A2L file generated offline from the ELF file has to be corrected once from the target with
// `xcpclient --fix-a2l` in online mode, or generated with `xcpclient --elf ... --create-a2l` in online mode.
// This works, when the creation order is deterministic, so the numbers stay the same after each restart.
// In this demo, events and segments are created in createEventsAndCalSegs(), called by startXcpServer() after XcpInit() and before the
// tasks are started.

// Calibration segments:
// Without section registration, the macros CalSegDecl and CalSegDeclRef are not available (compile time error).
// The segments are created with CalSegCreate. The segment numbers are the creation order (0 is the EPK segment created in XcpInit).

// Measurement events:
// DaqCreateEvent, DaqCreateAndTriggerEvent and DaqCreateAndTriggerEventCapture create the event on their first execution, in the task.
// The tasks run concurrently, so the creation order and the event ids could change after each restart.
// The events are therefore created at startup with XcpCreateEvent in a fixed order, the macros in the tasks find them by name.
// The event names must be identical, a misspelled name creates an additional event.

// Actual event ids can be obtained via the XCP protocol (GET_DAQ_EVENT_INFO), but this is not supported by all XCP tools.
// There is no way to query the calibration segment names over standard XCP.
// XCPlite has a small proprietary XCP extension which makes this possible (GET_SEGMENT_INFO mode 0 info 2).
// This is the way, how xcpclient is able to fix both, event and segment number to name associations.
// xcpclient reads the events and segments from the target on each connect and compares them with the A2L file, with a mismatch the
// DAQ measurement fails. It corrects them with --fix-a2l, or takes them from the target when it runs with --elf instead of an A2L file.

#include "assert.h"
#include <inttypes.h>
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
#define XCP_PROJECT_VERSION "V201"
#define XCP_USE_TCP false
#define XCP_SERVER_PORT 5555
#define XCP_QUEUE_SIZE 0 // The queue size is fixed by OPTION_QUEUE_32_SIZE for the 32-bit FreeRTOS build; this parameter must be 0
#define XCP_LOG_LEVEL 4  // 3 - Info, 4 - Print XCP commands, 5 - Debug

// @@@@ TODO: Experimental A2L generation without the need for the XCP linker sections
#ifndef OPTION_SECTION_REGISTRATION

static void createEventsAndCalSegs(void); // Create the events and calibration segments at runtime, see below

// Linker section based metadata annotations are currently not supported without OPTION_SECTION_REGISTRATION
#define XCP_COMMENT(name, string)
#define XCP_UNIT(name, string)
#define XCP_LIMITS(name, min, max)
#define XCP_READ_WRITE(name)

#endif

// Start XCPlite
bool startXcpServer() {

    XcpSetLogLevel(XCP_LOG_LEVEL);

    // Create the EPK string with linker file markers, so the xcpclient A2L generator can find it
    XcpCreateEpk(XCP_PROJECT_VERSION);

    // Initialize XCP protocol layer
    const uint8_t bindAny[4] = {0, 0, 0, 0};
    if (!XcpInit(XCP_PROJECT_NAME, XCP_PROJECT_VERSION, XCP_MODE_LOCAL)) {
        printf("XcpInit failed\n");
        return false;
    }

#ifndef OPTION_SECTION_REGISTRATION
    // Without section registration, the events and calibration segments are created at runtime in a fixed order, after XcpInit() and
    // before the tasks are started
    createEventsAndCalSegs();
#endif

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
// Global measurement values

uint16_t global_counter = 0;
XCP_COMMENT(global_counter, "Global measurement variable, writable, incremented in fastTask");
XCP_READ_WRITE(global_counter);

// Platform analog input when available
float channel1 = 0.0f;
XCP_COMMENT(channel1, "Generated sine wave, updated in slowTask");
XCP_UNIT(channel1, "Volt");

// Test
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
    float period;                 // Period of the sine signal generator in slowTask
};

// Default calibration parameters (default/reference page)
// &parameters is the A2l file address of the calibration parameter segment 'parameters'
// Typename and variable name must be identical
const struct parameters parameters = {
    .fast_task_period_ms = 1,  // 1 ms = 1000 Hz
    .slow_task_period_ms = 10, // 10 ms = 100 Hz
    .counter_max = 1000,
    .amplitude = 1.0f,
    .period = 1.0f,
};

XCP_COMMENT(parameters__counter_max, "Maximum value for the global counter");
XCP_LIMITS(parameters__counter_max, 1, 10000);
XCP_COMMENT(parameters__fast_task_period_ms, "Period of the fast task in ms");
XCP_UNIT(parameters__fast_task_period_ms, "ms");
XCP_LIMITS(parameters__fast_task_period_ms, 1, 100);
XCP_COMMENT(parameters__slow_task_period_ms, "Period of the slow task in ms");
XCP_UNIT(parameters__slow_task_period_ms, "ms");
XCP_LIMITS(parameters__slow_task_period_ms, 2, 1000);
XCP_COMMENT(parameters__amplitude, "Amplitude of the sine signal generator");
XCP_UNIT(parameters__amplitude, "Volt");
XCP_LIMITS(parameters__amplitude, 0.0f, 10.0f);
XCP_COMMENT(parameters__period, "Period of the sine signal generator");
XCP_UNIT(parameters__period, "s");
XCP_LIMITS(parameters__period, 0.001f, 10.0f);

#ifdef OPTION_SECTION_REGISTRATION

// Declare a calibration segment that wraps 'parameters' for thread-safe and consistent access.
// This creates:
//  - the calibration segment index calseg_id_parameters, used by CalSegLock(parameters) in C
//  - the typed C++ handle 'parameters_calseg' used by the tasks below
// The offline A2L generator currently assumes that the struct type name and default-parameter variable name are identical.

// With section registration, XcpInit() creates the segment from the descriptor in the xcp_cals section.
// The segment number is the position of the descriptor in the section, known at link time, so the A2L file can be generated
// offline from the ELF file
#ifdef __cplusplus
CalSegDeclRef(parameters, parameters_calseg);
#else
CalSegDecl(parameters);
#endif

// Optional helper to clamp calibration parameters during runtime (for safety reasons) to the value range given by XCP_LIMIT
#define clamp_parameter(x, p, default, name)                                                                                                                                       \
    do {                                                                                                                                                                           \
        if (((p)->name) < (xcp_meta__min__##default##__##name))                                                                                                                    \
            (x) = xcp_meta__min__##default##__##name;                                                                                                                              \
        else if (((p)->name) > (xcp_meta__max__##default##__##name))                                                                                                               \
            (x) = xcp_meta__max__##default##__##name;                                                                                                                              \
        else                                                                                                                                                                       \
            (x) = (p)->name;                                                                                                                                                       \
    } while (0)

#else // !OPTION_SECTION_REGISTRATION

#ifndef __cplusplus
// The C macro CalSegCreate defines the index variable calseg_id_parameters in the scope of the function, it is returned here and
// stored in the index variable at file scope below, which CalSegLock(parameters) in the tasks uses
static tXcpCalSegIndex createParametersCalSeg(void) {
    CalSegCreate(parameters);
    return calseg_id_parameters;
}
#endif
static tXcpCalSegIndex calseg_id_parameters = XCP_UNDEFINED_CALSEG;
#ifdef __cplusplus
// The typed C++ handle over the index
static const xcp::CalSegRef<decltype(parameters)> parameters_calseg(&calseg_id_parameters, &parameters);
#endif

static void createEventsAndCalSegs(void) {

    // Events, the event ids are the creation order: fastTask 0, slowTask 1, foo 2
    // DaqCreateEvent(fastTask), DaqCreateAndTriggerEvent(slowTask) and DaqCreateAndTriggerEventCapture(foo, ...) in the tasks find them by name.
    // XcpCreateEvent is used here, not DaqCreateEvent, which would emit a second event descriptor marker for the offline A2L generator
    XcpCreateEvent("fastTask", 0, 0);
    XcpCreateEvent("slowTask", 0, 0);
    XcpCreateEvent("foo", 0, 0);

    // Calibration segments, the segment numbers are the creation order: epk 0 (created in XcpInit), parameters 1
#ifdef __cplusplus
    calseg_id_parameters = CalSegCreate(parameters).getIndex();
#else
    calseg_id_parameters = createParametersCalSeg();
#endif
}

// No XCP_LIMITS metadata available without section registration, no clamping
#define clamp_parameter(x, p, default, name) ((x) = (p)->name)

#endif // !OPTION_SECTION_REGISTRATION

//----------------------------------------------------------------------------------------------------
// Functions

XCP_NOINLINE void foo(void) {

    // Static local  variable
    XCP_COMMENT(static_counter, "Local static measurement variable in function `foo`");
    static uint16_t static_counter = 0;

    // Local variable measured via direct stack access
    // The offline A2L generator can discover it in the ELF file and associate it to the functions DAQ event trigger
    // This is for convinience, but has limitations in optimized builds, use explicit capture for reliable visibility in optimized builds
    //  - XCP_MEAS keeps this local measurement variable visible in optimized builds (spilled to stack)
    //  - With clang compiler, some local measurement variables may not be visible
    //  - Varibales in inlined functions are not visible
    XCP_COMMENT(counter, "Local captured measurement variable in function `foo`");
    XCP_MEAS uint32_t counter = 0;

    static_counter = static_counter + 1;
    counter = static_counter;

    XCP_MEAS int8_t test_int8 = static_counter - 1;
    XCP_MEAS int16_t test_int16 = static_counter - 2;
    XCP_MEAS int32_t test_int32 = static_counter - 3;
    XCP_MEAS uint64_t test_int64 = static_counter - 4;

    // Local variables measured via capture
    float test_float = 0.001f * static_counter;
    double test_double = 0.002 * static_counter;
    uint8_t test_uint8 = 1;
    uint16_t test_uint16 = static_counter + 2;
    uint32_t test_uint32 = static_counter + 3;
    uint64_t test_uint64 = static_counter + 4;
    struct test_struct {
        uint16_t a;
        int16_t b;
        float f;
        uint8_t d[3];
    } test_struct = {1, -2, 0.003f * static_counter, {1, 2, 3}};
    uint8_t test_array[3] = {1, 2, (uint8_t)(static_counter & 0xff)};

    // Create and trigger the DAQ event 'foo' with captured local variables
    // Capturing local variables comes with the overhead of additionally space used for the copy on stack
    // But copying is usually cheaper than generally spilling registers to stack with XCP_MEAS
    DaqCreateAndTriggerEventCapture(foo, counter, test_float, test_double, test_uint8, test_uint16, test_uint32, test_uint64, test_struct, test_array);
}

//----------------------------------------------------------------------------------------------------
// Tasks

// High priority fast task
static void fastTask(void *parameter) {
    (void)parameter;

    XCP_COMMENT(counter, "Local measurement variable in `fastTask`");
    XCP_MEAS uint16_t counter = 0;

    XCP_COMMENT(static_counter, "Local static measurement variable in `fastTask`");
    static uint16_t static_counter = 0;

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
            const struct parameters *params = CalSegLock(parameters);
#endif

            // Save the task period parameter, don't delay during the lock to give XCP a chance to modify the parameters.
            clamp_parameter(period_ms, params, parameters, fast_task_period_ms);

            counter = counter + 1;
            static_counter = static_counter + 1;
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
        // Test: Trigger the event a second time to measure runtime of the DaqTriggerEvent function
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

    XCP_COMMENT(counter, "Local measurement variable in `slowTask`");
    XCP_MEAS uint16_t counter = 0;

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
            const struct parameters *params = CalSegLock(parameters);
#endif

            clamp_parameter(slow_task_period_ms, params, parameters, slow_task_period_ms);
            fast_task_period_ms = params->fast_task_period_ms;

            counter = counter + 1;
            if (counter > params->counter_max) {
                counter = 0;
            }

#define PI2 6.28318530717958647692f
            channel1 = params->amplitude * sinf(phase);
            phase += PI2 / (params->period * 1000.0 / slow_task_period_ms);
            if (phase >= PI2) {
                phase -= PI2;
            }

#ifndef __cplusplus
            CalSegUnlock(parameters);
#endif
        }

        // Call the demo function foo
        foo();

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

/*
static void test(void) {

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {

        vTaskDelay(pdMS_TO_TICKS(200));

        // Check timestamp resolution and XCP clock
        for (int i = 0; i < 20; i++) {
#if !defined(FREE_RTOS_POSIX_SIM)
            Clock64_Update();
#endif

            uint64_t t1 = ApplXcpGetClock64();
            vTaskDelay(pdMS_TO_TICKS(1000));
            uint64_t t2 = ApplXcpGetClock64();
            printf("XCP clock resolution check: t1=%" PRIu64 ", t2=%" PRIu64 ", dt=%" PRIu64 "\n", t1, t2, t2 - t1);
        }
    } else {
        printf("Scheduler not running, skipping XCP clock check\n");
    }
}
*/

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
