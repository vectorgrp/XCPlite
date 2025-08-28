// multi_thread_demo xcplib example

#include <assert.h>  // for assert
#include <math.h>    // for M_PI, sin
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_2PI
#define M_2PI (M_PI * 2)
#endif

// Threads
#if defined(_WIN32) // Windows
#include <windows.h>
typedef HANDLE THREAD;
#define create_thread(h, t) *h = CreateThread(0, 0, t, NULL, 0, NULL)
#define join_thread(h) WaitForSingleObject(h, INFINITE);
#else
#include <pthread.h>
typedef pthread_t THREAD;
#define create_thread(h, t) pthread_create(h, NULL, t, NULL)
#define join_thread(h) pthread_join(h, NULL)
#endif

//-----------------------------------------------------------------------------------------------------

#define XCP_MAX_EVENT_NAME 15
#define THREAD_COUNT 8              // Number of threads to create
#define THREAD_DELAY_US 10000       // Delay in microseconds for the thread loops
#define MAX_THREAD_NAME_LENGTH 32   // Maximum length of thread name
#define EXPERIMENTAL_THREAD_CONTEXT // Enable demonstration of tracking thread context and span of the clip and filter function

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_PROJECT_NAME "multi_thread_demo" // A2L project name
#define OPTION_USE_TCP false                    // TCP or UDP
#define OPTION_SERVER_PORT 5555                 // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}         // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 1024)         // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3                      // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct params {
    uint16_t counter_max; // Maximum value of the counter
    double ampl;          // Amplitude
    double period;        // Period
    double filter;        // Filter coefficient for the filter function 0.0-1.0
    double clip_max;      // Maximum value for clipping function
    double clip_min;      // Minimum value for clipping function
    uint32_t delay_us;    // Delay in microseconds for the thread loops
    bool run;             // Stop flag for the task
} params_t;

// Default parameters
static const params_t params = {.counter_max = 1000, .ampl = 100.0, .period = 3.0, .filter = 0.07, .clip_max = 80.0, .clip_min = -100.0, .delay_us = THREAD_DELAY_US, .run = true};

// Global calibration segment handle
static tXcpCalSegIndex calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------
// Thread context
// Experimental

#ifdef EXPERIMENTAL_THREAD_CONTEXT

// Thread context structure
typedef struct {
    char name[MAX_THREAD_NAME_LENGTH + 1]; // Human-readable thread name
    tXcpEventId id;                        // XCP event ID for this thread
    tXcpEventId span_id;                   // XCP event ID for the current span
    uint32_t level;                        // Current span level
} tXcpContext;

// Global thread-local context
THREAD_LOCAL tXcpContext gXcpContext = {0};

// Inline accessors for maximum performance
static inline tXcpContext *XcpGetContext(void) { return &gXcpContext; }
static inline const char *XcpGetContextName(void) { return gXcpContext.name; }

// Begin a span, create a span event once
// Trigger the context event on entry
#define BeginSpan(name)                                                                                                                                                            \
    uint64_t span_t1 = ApplXcpGetClock64();                                                                                                                                        \
    uint64_t span_dt;                                                                                                                                                              \
    static tXcpEventId span_id = XCP_UNDEFINED_EVENT_ID;                                                                                                                           \
    if (span_id == XCP_UNDEFINED_EVENT_ID) {                                                                                                                                       \
        A2lLock();                                                                                                                                                                 \
        span_id = XcpCreateEvent(name, 0, 0);                                                                                                                                      \
        A2lSetStackAddrMode_i(span_id);                                                                                                                                            \
        A2lCreatePhysMeasurementInstance(name, span_dt, "Span runtime", "ns", 0.0, 0.1000000);                                                                                     \
        A2lUnlock();                                                                                                                                                               \
    }                                                                                                                                                                              \
    tXcpContext *ctx = XcpGetContext();                                                                                                                                            \
    tXcpEventId previous_span_id = ctx->span_id;                                                                                                                                   \
    ctx->span_id = span_id;                                                                                                                                                        \
    ctx->level++;                                                                                                                                                                  \
    XcpEventDynRelAt(ctx->id, (const uint8_t *)ctx, get_stack_frame_pointer(), span_t1);

// End span
// Trigger the span event and the context event on exit
// Measure execution time of the span
#define EndSpan()                                                                                                                                                                  \
    uint64_t span_t2 = ApplXcpGetClock64();                                                                                                                                        \
    span_dt = span_t2 - span_t1;                                                                                                                                                   \
    XcpEventDynRelAt(ctx->span_id, NULL, get_stack_frame_pointer(), span_t2);                                                                                                      \
    ctx->span_id = previous_span_id;                                                                                                                                               \
    ctx->level--;                                                                                                                                                                  \
    XcpEventDynRelAt(ctx->id, (const uint8_t *)ctx, get_stack_frame_pointer(), span_t2);

// Create a named context
// Create the context event (name is 'context_name'_'context_index')
// Register the context struct  as A2L typedef instance tied to the context event
static uint16_t XcpCreateContext(const char *context_name, uint16_t context_index) {

    if (!XcpIsActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Return undefined event ID if XCP is not activated
    }

    // Once:
    // Create a typedef for the thread context
    // Uses the predefined enum conversion rule for the event names
    A2lOnce() {
        A2lLock();
        A2lTypedefBegin(tXcpContext, "A2L typedef for tXcpContext");
        A2lTypedefPhysMeasurementComponent(span_id, tXcpContext, "function span id", "conv.events", 0, 32);
        A2lTypedefMeasurementComponent(level, tXcpContext);
        A2lTypedefEnd();
        A2lUnlock();
    }

    // Init thread local context
    // Create a unique name from index
    // Create an XCP event for this context
    snprintf(gXcpContext.name, sizeof(gXcpContext.name), "%s_%u", context_name, context_index);
    gXcpContext.id = XcpCreateEvent(gXcpContext.name, 0, 0);
    gXcpContext.span_id = gXcpContext.id;
    gXcpContext.level = 0;

    // Create a measurement typedef instance with the context name
    A2lLock();
    A2lSetRelativeAddrMode_i(gXcpContext.id, &gXcpContext); // Set relative addressing mode for the thread context
    A2lCreateTypedefNamedInstance(gXcpContext.name, gXcpContext, tXcpContext, "Thread local context");
    A2lUnlock();

    printf("Thread context created: name = %s, id = %u\n", gXcpContext.name, gXcpContext.id);
    return gXcpContext.id;
}

#else
#define BeginSpan(name)
#define EndSpan()
#endif

//-----------------------------------------------------------------------------------------------------

// Clip a value to a range defined in the calibration segment
double clip(double input) {

    // Instrumentation: Begin span for clip function
    BeginSpan("clip");

    // Simulate some more expensive work
    sleepUs(50);

    params_t *params = (params_t *)XcpLockCalSeg(calseg);

    // Clip the input value to a range defined in the calibration segment
    double output = input;
    if (output > params->clip_max) {
        output = params->clip_max;
    } else if (output < params->clip_min) {
        output = params->clip_min;
    }

    XcpUnlockCalSeg(calseg);

    // Instrumentation: End span for filter function
    EndSpan();

    return output;
}

// Filter function that applies a simple low-pass filter to the input signal
double filter(double input) {

    double filtered_input = 0;
    double clipped_output = 0;
    static THREAD_LOCAL double last = 0.0; // Thread-local state for the filter, simplified example, one filter instance per thread

    // Instrumentation: Begin span for filter function
    BeginSpan("filter");

    // Instrumentation: Register local variable filtered_input for measurement (once global, use the span event id)
    A2lOnce() { // Ensure this is only done once globally
        A2lLock();
        A2lSetStackAddrMode_i(XcpGetContext()->span_id); // Set stack addressing mode
        A2lCreateMeasurement(filtered_input, "Filter result");
        A2lUnlock();
    }

    // Simulate some more expensive work
    sleepUs(100);

    params_t *params = (params_t *)XcpLockCalSeg(calseg);

    // Filter the input signal using a simple low-pass filter
    filtered_input = input * params->filter + last * (1.0 - params->filter);
    last = filtered_input;

    XcpUnlockCalSeg(calseg);

    // Clip the filter output
    clipped_output = clip(filtered_input);

    // Instrumentation: End span for filter function
    EndSpan();

    return clipped_output;
}

// Task function that runs in a separate thread
// Calculates a sine wave, square wave, and sawtooth wave signal
#ifdef _WIN32
DWORD WINAPI task(LPVOID p)
#else
void *task(void *p)
#endif
{
    (void)p;

    bool run = true;
    uint32_t delay_us = 1000;
    uint64_t start_time = clockGetUs(); // Get the start time

    // Task local measurement variables on stack
    uint16_t counter = 0;
    double time = 0;
    double channel1 = 0;
    double channel2 = 0;
    double channel3 = 0;
    uint32_t array[256] = {0};

    // Instrumentation: Events and measurement variables
    // Register task local variables counter and channelx with stack addressing mode
    tXcpEventId task_event_id = DaqCreateEventInstance_s("task");

    // Build the task name from the event index
    uint16_t task_index = XcpGetEventIndex(task_event_id); // Get the event index of this event instance
    char task_name[XCP_MAX_EVENT_NAME + 1];
    snprintf(task_name, sizeof(task_name), "task_%u", task_index);

    // Create measurement variables for this task instance
    A2lLock();
    A2lSetStackAddrMode_i(task_event_id);
    A2lCreateMeasurementInstance(task_name, counter, "task loop counter");
    A2lCreateMeasurementInstance(task_name, channel1, "task sine wave signal");
    A2lCreateMeasurementInstance(task_name, channel2, "task square wave signal");
    A2lCreateMeasurementInstance(task_name, channel3, "task sawtooth signal");
    A2lCreateMeasurementArrayInstance(task_name, array, "task array (to increase measurement workload)");
    A2lUnlock();

    // Instrumentation: Context
    // Create context for this task
#ifdef EXPERIMENTAL_THREAD_CONTEXT
    XcpCreateContext("ctx", task_index);
#endif

    while (run) {

        {
            params_t *params = (params_t *)XcpLockCalSeg(calseg);

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }

            time = (double)(clockGetUs() - start_time) / 1000000;                         // Calculate elapsed time in seconds
            double normalized_time = M_2PI * fmod(time, params->period) / params->period; // Normalize time ([0.0..M_2PI[ to the period

            channel1 = params->ampl * sin(normalized_time);                    // Sine wave
            channel2 = params->ampl * ((normalized_time < M_PI) ? 1.0 : -1.0); // Square wave
            channel3 = params->ampl * (normalized_time - M_PI) / M_PI;         // Sawtooth wave

            // Sleep time
            delay_us = params->delay_us;

            // Stop
            run = params->run;

            XcpUnlockCalSeg(calseg);
        }

        // Filter or clip the signals
        channel1 = clip(channel1);   // Sine
        channel2 = filter(channel2); // Square
        channel3 = clip(channel3);   // Sawtooth

        // Instrumentation: Measurement event
        DaqEvent_i(task_event_id);

        // Sleep for the specified delay parameter in microseconds, defines the approximate sampling rate
        sleepUs(delay_us);
    }

    return 0; // Exit the thread
}

// Demo main
int main(void) {

    printf("\nXCP on Ethernet multi thread xcplib demo\n");

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, activate XCP, must be called before starting the server
    // If XCP is not activated, the server will not start and all XCP instrumentation will be passive with minimal overhead
    XcpInit(true);

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect, auto grouping
    if (!A2lInit(OPTION_PROJECT_NAME, NULL, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create a calibration segment for the calibration parameter struct
    // This segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independant page switching, checksum calculation and reinitialization (copy reference page to working page)
    calseg = XcpCreateCalSeg("Parameters", &params, sizeof(params));
    assert(calseg != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully

    // Register calibration parameters in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params.counter_max, "Max counter value, wrap around", "", 0, 10000.0);
    A2lCreateParameter(params.ampl, "Amplitude", "Volt", 0, 100.0);
    A2lCreateParameter(params.period, "Period", "s", 0.1, 10.0);
    A2lCreateParameter(params.filter, "Filter coefficient", "", 0.0, 1.0);
    A2lCreateParameter(params.clip_max, "Maximum value for clipping function", "Volt", -100.0, 100.0);
    A2lCreateParameter(params.clip_min, "Minimum value for clipping function", "Volt", -100.0, 100.0);
    A2lCreateParameter(params.delay_us, "task delay time in us", "us", 0, 1000000);
    A2lCreateParameter(params.run, "stop task", "", 0, 1);

    // Create multiple instances of task
    THREAD t[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        t[i] = 0;
        create_thread(&t[i], task);
    }

    // Optional: Finalize the A2L file generation early, to write the A2L now, not when the client connects
    sleepUs(200000);
    A2lFinalize();

    for (int i = 0; i < THREAD_COUNT; i++) {
        if (t[i])
            join_thread(t[i]);
    }

    // Force disconnect the XCP client
    XcpDisconnect();

    // Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
