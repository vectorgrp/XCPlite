// bpf_demo xcplib example

#include <assert.h>  // for assert
#include <errno.h>   // for errno
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf
#include <unistd.h>  // for sleep

#ifdef __linux__
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#endif

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "bpf_demo"  // Project name, used to build the A2L and BIN file name
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

//-----------------------------------------------------------------------------------------------------
// Global variables

typedef struct {
    uint8_t byte_field;
    int16_t word_field;
} demo_struct_t;

// BPF event structure
// Event structure that matches the BPF program
struct event {
    uint64_t timestamp; // Precise kernel timestamp from bpf_ktime_get_ns()
    uint32_t pid;
    uint32_t ppid;
    char comm[16];
};

static uint16_t static_counter = 0;                                      // Local counter variable for measurement
static demo_struct_t static_struct = {.byte_field = 1, .word_field = 2}; // Single instance of demo_struct_t
static uint32_t new_process_pid = 0;                                     // Global variable to store new process PID
static volatile bool running = true;                                     // Control flag for main loop

#ifdef __linux__
static struct bpf_object *obj = NULL;
static struct bpf_link *bpf_link = NULL;
static int map_fd = -1;
#endif

//-----------------------------------------------------------------------------------------------------
// Signal handler for clean shutdown

static void sig_handler(int sig) { running = false; }

//-----------------------------------------------------------------------------------------------------
// BPF functions

#ifdef __linux__
static int handle_event(void *ctx, void *data, size_t data_sz) {
    struct event *e = data;

    // Update the global PID variable with the new process ID
    new_process_pid = e->pid;

    printf("Process created: PID=%u, PPID=%u, comm=%s, timestamp=%llu ns
           ", 
           e->pid,
           e->ppid, e->comm, e->timestamp);

    // Trigger XCP measurement event with precise kernel timestamp
    // Convert nanoseconds to microseconds for XCP (assuming XCP expects microseconds)
    uint64_t timestamp_us = e->timestamp / 1000;
    DaqEventAt("new_process", timestamp_us);

    return 0;
}

static int init_bpf(void) {
    struct bpf_program *prog;
    struct ring_buffer *rb = NULL;
    int err;

    // Load BPF object from file
    obj = bpf_object__open_file("process_monitor.bpf.o", NULL);
    err = libbpf_get_error(obj);
    if (err) {
        printf("Failed to open BPF object file: %d\n", err);
        return -1;
    }

    // Load BPF program into kernel
    err = bpf_object__load(obj);
    if (err) {
        printf("Failed to load BPF object: %d\n", err);
        goto cleanup;
    }

    // Find the tracepoint program
    prog = bpf_object__find_program_by_name(obj, "trace_process_fork");
    if (!prog) {
        printf("Failed to find BPF program\n");
        err = -1;
        goto cleanup;
    }

    // Attach the program
    bpf_link = bpf_program__attach(prog);
    err = libbpf_get_error(bpf_link);
    if (err) {
        printf("Failed to attach BPF program: %d\n", err);
        goto cleanup;
    }

    // Get ring buffer map FD
    map_fd = bpf_object__find_map_fd_by_name(obj, "rb");
    if (map_fd < 0) {
        printf("Failed to find ring buffer map\n");
        err = map_fd;
        goto cleanup;
    }

    // Set up ring buffer polling
    rb = ring_buffer__new(map_fd, handle_event, NULL, NULL);
    if (!rb) {
        printf("Failed to create ring buffer\n");
        err = -1;
        goto cleanup;
    }

    printf("BPF program loaded and attached successfully\n");
    return 0;

cleanup:
    if (bpf_link) {
        bpf_link__destroy(bpf_link);
        bpf_link = NULL;
    }
    if (obj) {
        bpf_object__close(obj);
        obj = NULL;
    }
    return err;
}

static void cleanup_bpf(void) {
    if (bpf_link) {
        bpf_link__destroy(bpf_link);
        bpf_link = NULL;
    }
    if (obj) {
        bpf_object__close(obj);
        obj = NULL;
    }
}

static int poll_bpf_events(void) {
    struct ring_buffer *rb;
    int err;

    if (map_fd < 0) {
        return -1;
    }

    rb = ring_buffer__new(map_fd, handle_event, NULL, NULL);
    if (!rb) {
        return -1;
    }

    // Poll for events with timeout
    err = ring_buffer__poll(rb, 10); // 10ms timeout

    ring_buffer__free(rb);
    return err;
}
#endif

//-----------------------------------------------------------------------------------------------------
// Demo main

int main(void) {

    printf("\nXCP on Ethernet bpf_demo C xcplib demo\n");

    // Set up signal handler for clean shutdown
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

#ifdef __linux__
    // Initialize BPF program
    if (init_bpf() != 0) {
        printf("Warning: Failed to initialize BPF program. Running without BPF monitoring.\n");
    }
#else
    printf("Warning: BPF is only supported on Linux. Running without BPF monitoring.\n");
#endif

    // Init XCP
    XcpSetLogLevel(OPTION_LOG_LEVEL);
    XcpInit(true);
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }
    if (!A2lInit(OPTION_PROJECT_NAME, NULL, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create a measurement event named "mainloop_event"
    DaqCreateEvent(mainloop_event);

    // Create a measurement event for process monitoring
    DaqCreateEvent(process_event);

    // Create a A2L typedef for demo_struct_t
    A2lTypedefBegin(demo_struct_t, "A2L typedef for demo_struct_t");
    A2lTypedefMeasurementComponent(byte_field, demo_struct_t);
    A2lTypedefMeasurementComponent(word_field, demo_struct_t);
    A2lTypedefEnd();

    // Create and register a static/global measurement variables (static_counter, static_struct)
    A2lSetAbsoluteAddrMode(mainloop_event); // absolute addressing mode
    A2lCreateMeasurement(static_counter, "Global measurement variable ");
    A2lCreateTypedefInstance(static_struct, demo_struct_t, "Instance of demo_struct_t");

    // Create measurement for new process PID
    A2lCreateMeasurement(new_process_pid, "PID of newly created process");

    // Create and register a local measurement variables (loop_counter)
    uint16_t loop_counter = 0;
    A2lSetStackAddrMode(mainloop_event); // Set stack relative addressing mode with fixed event mainloop_event
    A2lCreateMeasurement(loop_counter, "Local measurement variable on stack");

    A2lFinalize(); // @@@@ Test: Manually finalize the A2L file to make it visible without XCP tool connect

    // Mainloop
    printf("Start main loop...\n");
    while (running) {

        // Local variables
        loop_counter++;
        static_counter++;

#ifdef __linux__
        // Poll for BPF events
        int ret = poll_bpf_events();
        if (ret > 0) {
            // New process detected, trigger process event
            DaqEvent(process_event);
        }
#endif

        // XCP: Trigger the measurement event "mainloop_event" to timestamp and send measurement to the XCP client
        DaqEvent(mainloop_event);

        // Sleep for the specified delay parameter in microseconds, don't sleep with the XCP lock held to give the XCP client a chance to update params
        sleepUs(1000);

    } // while (running)

    printf("Shutting down...\n");

#ifdef __linux__
    // Cleanup BPF resources
    cleanup_bpf();
#endif

    // XCP: Force disconnect the XCP client
    XcpDisconnect();

    // XCP: Stop the XCP server
    XcpEthServerShutdown();

    return 0;
}
