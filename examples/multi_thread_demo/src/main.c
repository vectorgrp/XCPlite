// multi_thread_demo xcplib example

#include <assert.h>  // for assert
#include <math.h>    // for M_PI, sin
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"      // for xcplib A2l generation
#include "platform.h" // for sleepMs
#include "xcplib.h"   // for xcplib application programming interface

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_2PI
#define M_2PI (M_PI * 2)
#endif

#define THREAD_COUNT 8              // Number of threads to create
#define MAX_THREAD_NAME_LENGTH 32   // Maximum length of thread name
#define EXPERIMENTAL_THREAD_CONTEXT // Enable demonstration of tracking thread context and span of the clip and filter function

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_ENABLE_A2L_GENERATOR                  // Enable A2L file generation
#define OPTION_A2L_PROJECT_NAME "multi_thread_demo"  // A2L project name
#define OPTION_A2L_FILE_NAME "multi_thread_demo.a2l" // A2L file name
#define OPTION_USE_TCP false                         // TCP or UDP
#define OPTION_SERVER_PORT 5555                      // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}              // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 32                  // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 4

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct params {
    uint16_t counter_max; // Maximum value of the counter
    double ampl;          // Amplitude of the sine wave
    double period;        // Period of the sine wave in seconds
    double filter;        // Filter coefficient for the filter function 0.0-1.0
    double clip_max;      // Maximum value for clipping function
    double clip_min;      // Minimum value for clipping function
    uint32_t delay_us;    // Delay in microseconds for the main loop
    bool run;             // Stop flag for the task
} params_t;

// Default parameters
static const params_t params = {.counter_max = 1000, .ampl = 100.0, .period = 3.0, .filter = 0.07, .clip_max = 80.0, .clip_min = -100.0, .delay_us = 10000, .run = true};

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
#define BeginSpan(name)                                                                                                                                                            \
    static tXcpEventId span_id = XCP_UNDEFINED_EVENT_ID;                                                                                                                           \
    if (span_id == XCP_UNDEFINED_EVENT_ID) {                                                                                                                                       \
        span_id = XcpCreateEvent(name, 0, 0);                                                                                                                                      \
    }                                                                                                                                                                              \
    tXcpContext *ctx = XcpGetContext();                                                                                                                                            \
    tXcpEventId previous_span_id = ctx->span_id;                                                                                                                                   \
    ctx->span_id = span_id;                                                                                                                                                        \
    ctx->level++;                                                                                                                                                                  \
    XcpEventDynRelAt(ctx->id, (const uint8_t *)ctx, get_stack_frame_pointer(), 0);

// End span
#define EndSpan()                                                                                                                                                                  \
    ctx->span_id = previous_span_id;                                                                                                                                               \
    ctx->level--;                                                                                                                                                                  \
    XcpEventDynRelAt(ctx->id, (const uint8_t *)ctx, get_stack_frame_pointer(), 0);

// Create a named context with an event of the same name and register it as A2L typedef instance tied to the event
static uint16_t XcpCreateContext(const char *context_name, uint16_t context_index) {

    // Once:
    // Create a typedef for the thread context
    // Uses the predefined enum conversion rule for the event names
    A2lOnce(tXcpContext) {
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
    SNPRINTF(gXcpContext.name, XCP_MAX_EVENT_NAME, "%s_%u", context_name, context_index);
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

    BeginSpan("clip");

    // Simulate some more expensive work
    sleepNs(400000);

    // Clip the input value to a range defined in the calibration segment
    params_t *params = (params_t *)XcpLockCalSeg(calseg);
    double output = input;
    if (output > params->clip_max) {
        output = params->clip_max; // Clip to maximum value
    } else if (output < params->clip_min) {
        output = params->clip_min; // Clip to minimum value
    }
    XcpUnlockCalSeg(calseg); // Unlock the calibration segment

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

    // Instrumentation: Register local variables (once global, just use the span event id
    A2lOnce(filter_local_vars) { // Ensure this is only done once globally
        A2lLock();
        A2lSetStackAddrMode_i(XcpGetContext()->span_id); // Set stack addressing mode
        A2lCreateMeasurement(filtered_input, "Filter result", "");
        A2lUnlock();
    }

    // Simulate some more expensive work
    sleepNs(1000000);

    params_t *params = (params_t *)XcpLockCalSeg(calseg);
    filtered_input = input * params->filter + last * (1.0 - params->filter);
    XcpUnlockCalSeg(calseg); // Unlock the calibration segment
    last = filtered_input;   // Update the last output for the next call in this thread

    // Clip the output to a range, for example
    clipped_output = clip(filtered_input);

    // Instrumentation: Measure local variables
    DaqEvent_i(XcpGetContext()->span_id);

    // Instrumentation: End span for filter function
    EndSpan();

    return clipped_output;
}

// Task function that runs in a separate thread
// Calculates a sine wave, square wave, and sawtooth wave signal
#ifdef _WIN
DWORD WINAPI task(LPVOID p)
#else
void *task(void *p)
#endif
{
    (void)p;

    bool run = true;
    uint32_t delay_us = 1000;
    uint64_t start_time = ApplXcpGetClock64(); // Get the start time in clock ticks CLOCK_TICKS_PER_S

    // Task local measurement variables on stack
    uint16_t counter = 0;
    double time = 0;
    double channel1 = 0;
    double channel2 = 0;
    double channel3 = 0;

    // Instrumentation: Events and measurement variables
    // Register task local variables counter and channelx with stack addressing mode
    tXcpEventId task_event_id = XcpCreateEventInstance("task", 0, 0);

    // Build the task name from the event index
    uint16_t task_index = XcpGetEventIndex(task_event_id); // Get the event index of this event instance
    char task_name[XCP_MAX_EVENT_NAME + 1];
    sprintf(task_name, "task_%u", task_index);

    // Create measurement variables for this task instance
    A2lLock();
    A2lSetStackAddrMode_i(task_event_id);
    A2lCreateMeasurementInstance(task_name, counter, "task loop counter", "");
    A2lCreateMeasurementInstance(task_name, channel1, "task sine wave signal", "Volt");
    A2lCreateMeasurementInstance(task_name, channel2, "task square wave signal", "Volt");
    A2lCreateMeasurementInstance(task_name, channel3, "task sawtooth signal", "Volt");
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

            time = (double)(ApplXcpGetClock64() - start_time) / CLOCK_TICKS_PER_S;        // Calculate elapsed time in seconds
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
        sleepNs(delay_us * 1000);
    }

    return NULL; // Exit the thread
}

// Demo main
int main(void) {

    printf("\nXCP on Ethernet multi thread xcplib demo\n");

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton, must be called before starting the server
    XcpInit();

    // Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable A2L generation and prepare the A2L file, finalize the A2L file on XCP connect, auto grouping
#ifdef OPTION_ENABLE_A2L_GENERATOR
    if (!A2lInit(OPTION_A2L_FILE_NAME, OPTION_A2L_PROJECT_NAME, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, true, true)) {
        return 1;
    }
#else
    // Set the A2L filename for upload, assuming the A2L file exists
    ApplXcpSetA2lName(OPTION_A2L_FILE_NAME);
#endif

    // Create a calibration segment for the calibration parameter struct
    // This segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independant page switching, checksum calculation and reinitialization (copy reference page to working page)
    // Note that it can be used in only one ECU thread (in Rust terminology, it is Send, but not Sync)
    calseg = XcpCreateCalSeg("Parameters", &params, sizeof(params));

    // Register calibration parameters in the calibration segment
    A2lSetSegmentAddrMode(calseg, params);
    A2lCreateParameter(params, counter_max, "Max counter value, wrap around", "", 0, 10000.0);
    A2lCreateParameter(params, ampl, "Amplitude", "Volt", 0, 100.0);
    A2lCreateParameter(params, period, "Period", "s", 0.1, 10.0);
    A2lCreateParameter(params, filter, "Filter coefficient", "", 0.0, 1.0);
    A2lCreateParameter(params, clip_max, "Maximum value for clipping function", "Volt", -100.0, 100.0);
    A2lCreateParameter(params, clip_min, "Minimum value for clipping function", "Volt", -100.0, 100.0);
    A2lCreateParameter(params, delay_us, "task delay time in us", "us", 0, 1000000);
    A2lCreateParameter(params, run, "stop task", "", 0, 1);

    // Create multiple instances of task
    THREAD t[10];
    for (int i = 0; i < THREAD_COUNT; i++) {
        create_thread(&t[i], task);
    }

    // Optional: Finalize the A2L file generation early, to write the A2L now, not when the client connects
    sleepMs(200);
    A2lFinalize();

    for (int i = 0; i < THREAD_COUNT; i++) {
        join_thread(t[i]);
    }

    // Force disconnect the XCP client
    XcpDisconnect();

    // Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
