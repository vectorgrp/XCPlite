// freertos_demo - XCPlite example for FreeRTOS POSIX simulator
//
// Demonstrates XCPlite (XCP measurement and calibration) running inside FreeRTOS tasks.
// Uses the FreeRTOS POSIX simulator port so the demo builds and runs on macOS / Linux
// before switching to the real embedded target.
//
// Relationship to no_a2l_demo:
//   - Same measurement/calibration variables and XCP server setup without on-target A2L generation
//   - Threads are replaced by FreeRTOS tasks (xTaskCreate / vTaskDelayUntil)
//   - Timing uses pdMS_TO_TICKS() / vTaskDelayUntil() instead of sleepUs()
//   - The xcplite library itself still uses POSIX sockets and POSIX threads internally;
//     these coexist with FreeRTOS tasks without interference in the POSIX simulator.
//
// How the POSIX simulator works:
//   Each FreeRTOS task runs as a separate pthread managed by the FreeRTOS scheduler.
//   The scheduler uses SIGUSR1/SIGUSR2 sent to specific threads (not the whole process),
//   so normal POSIX APIs (sockets, clocks) work as expected inside FreeRTOS tasks.
//
// No A2L generation:
//   The demo does not generate an A2L file at runtime.
//   Use the pre-supplied freertos_demo.a2l or generate one with the tool xcpclient
//
// Build:
//   cmake -B build -S . -DXCPLITE_BUILD_FREERTOS_DEMO=ON
//   cmake --build build --target freertos_demo
//
// Connect with CANape or any XCP tool to UDP port 5555.

#include <assert.h>  // for assert (used by DaqTriggerEvent macro)
#include <signal.h>  // for signal, SIGINT, SIGTERM
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for malloc, free
#include <string.h>  // for memset

// FreeRTOS kernel headers
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// XCPlite headers
#include "platform.h" // for platform abstraction (sleepUs, get_thread_id, ...)
#include "xcplib.h"   // for libxcplite application programming interface

//-----------------------------------------------------------------------------
// XCP server configuration

#define OPTION_PROJECT_NAME "freertos_demo"
#define OPTION_PROJECT_VERSION "V100"
#define OPTION_USE_TCP false
#define OPTION_SERVER_PORT 5555
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // bind to any interface
#define OPTION_QUEUE_SIZE (1024 * 8)    // transmit queue size in bytes
#define OPTION_LOG_LEVEL 5              // 1=Error 2=Warn 3=Info 4=Debug

//-----------------------------------------------------------------------------
// Calibration parameters

// Task timing calibration
struct task_cal {
    uint32_t task1_period_ms; // Period of measurement task 1 in milliseconds
    uint32_t task2_period_ms; // Period of measurement task 2 in milliseconds
    uint32_t counter_max;     // Counter wrap-around value for task 1
    float amplitude;          // Amplitude for the signal generator in task 2
};

const struct task_cal task_cal = {
    .task1_period_ms = 1,  // 1 ms = 1 kHz
    .task2_period_ms = 10, // 10 ms = 100 Hz
    .counter_max = 1000,
    .amplitude = 1.0f,
};

// Declare a calibration segment that wraps 'task_cal'
CalSegDecl(task_cal);

//-----------------------------------------------------------------------------
// Measurement variables – task 1 (1 ms)

uint16_t task1_counter = 0;  // Ramp counter, resets at task_cal.counter_max
float task1_value = 0.0f;    // Derived float measurement
uint32_t task1_overruns = 0; // Missed deadlines (xTaskDelayUntil returned pdFALSE)

//-----------------------------------------------------------------------------
// Measurement variables – task 2 (10 ms)

uint16_t task2_counter = 0;
double task2_value = 0.0; // Slower ramp as a double

//-----------------------------------------------------------------------------
// FreeRTOS task: fast measurement task (default 1 ms period)
//
// Demonstrates:
//  - xTaskDelayUntil() for precise periodic activation
//  - CalSegLock/Unlock for thread-safe calibration parameter access
//  - DaqCreateEvent / DaqTriggerEvent for XCP data acquisition

static void measurementTask1(void *pvParameters) {
    (void)pvParameters;
    printf("[task1] FreeRTOS demo task started\n");

    DaqCreateEvent(task1); // Register an XCP DAQ event named "task1"

    uint16_t counter = 0; // Local variable

    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        // Read calibration parameters (wait-free, RCU-style)
        const struct task_cal *p = CalSegLock(task_cal);
        uint32_t period_ms = p->task1_period_ms;
        uint32_t cmax = p->counter_max;
        CalSegUnlock(task_cal);

        // Update measurement variables
        counter++;
        if (counter > cmax)
            counter = 0;
        task1_counter++;
        if (task1_counter > cmax)
            task1_counter = 0;
        task1_value = (float)task1_counter * 0.001f;

        // Trigger the DAQ event
        DaqTriggerEvent(task1);

        // Precise periodic delay (absolute timing, avoids drift)
        BaseType_t delayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(period_ms));
        if (delayed == pdFALSE)
            task1_overruns++;
    }
}

//-----------------------------------------------------------------------------
// FreeRTOS task: slow measurement task (default 10 ms period)

static void measurementTask2(void *pvParameters) {
    (void)pvParameters;
    printf("[task2] FreeRTOS demo task started\n");

    DaqCreateEvent(task2);

    uint16_t counter = 0; // Local variable

    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        const struct task_cal *p = CalSegLock(task_cal);
        uint32_t period_ms = p->task2_period_ms;
        float amp = p->amplitude;
        CalSegUnlock(task_cal);

        counter++;
        if (counter > 100)
            counter = 0;
        task2_counter++;
        if (task2_counter > 100)
            task2_counter = 0;
        task2_value = (double)amp * (double)task2_counter * 0.01;

        DaqTriggerEvent(task2);

        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(period_ms));
    }
}

//-----------------------------------------------------------------------------
// Signal handler – clean shutdown on Ctrl-C or SIGTERM
//
// The FreeRTOS POSIX port explicitly keeps SIGINT unblocked in all task
// threads (port.c: sigdelset(&xAllSignals, SIGINT)) so the handler is always
// reachable. vTaskEndScheduler() leaves the calling task's pthread blocked
// in event_wait() forever, so we exit directly instead.

static void sig_handler(int sig) {
    (void)sig;

    printf("\nShutting down XCP server...\n");
    XcpDisconnect();
    XcpEthServerShutdown();

    printf("exit\n");
    exit(0);
}

//-----------------------------------------------------------------------------
// main

int main(int argc, char *argv[]) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("XCPlite FreeRTOS POSIX Simulator Demo\n");
    printf("Project : %s  EPK: %s\n", OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION);
    printf("XCP     : %s port %d\n\n", OPTION_USE_TCP ? "TCP" : "UDP", OPTION_SERVER_PORT);

    // ------------------------------------------------------------------
    // Initialise XCPlite *before* the FreeRTOS scheduler starts.
    // XcpEthServerInit() creates two POSIX threads (RX and TX) which
    // coexist with FreeRTOS tasks in the POSIX simulator.
    // ------------------------------------------------------------------

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Create the EPK software version string in an initialized memory section for offline A2L generation
    XcpCreateEpk(OPTION_PROJECT_VERSION);

    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);
    if (argc > 0)
        XcpSetElfName(argv[0]); // Enable ELF upload for address resolution

    const uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        fprintf(stderr, "Failed to initialise XCP Ethernet server\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Create FreeRTOS application tasks
    // ------------------------------------------------------------------

    // Measurement task 1: high-priority, 1 ms
    xTaskCreate(measurementTask1, "task1", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 3, NULL);

    // Measurement task 2: lower priority, 10 ms
    xTaskCreate(measurementTask2, "task2", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 2, NULL);

    // ------------------------------------------------------------------
    // Start the FreeRTOS scheduler (blocks until vTaskEndScheduler() is called)
    // ------------------------------------------------------------------
    printf("Starting FreeRTOS scheduler...\n");
    vTaskStartScheduler();

    // ------------------------------------------------------------------
    // Post-scheduler cleanup (reached after vTaskEndScheduler())
    // ------------------------------------------------------------------
    printf("\nShutting down XCP server...\n");
    XcpDisconnect();
    XcpEthServerShutdown();

    printf("Done.\n");
    return 0;
}
