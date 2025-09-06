// bpf_demo xcplib example

// #define _GNU_SOURCE

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
#else
#error "This example only works on Linux systems with BPF support"
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

static struct bpf_object *obj = NULL;
static struct bpf_link *bpf_link = NULL; // Rename to avoid conflict with system link()
static struct ring_buffer *rb = NULL;
static int map_fd = -1;
static bool bpf_enabled = false;

//-----------------------------------------------------------------------------------------------------
// Signal handler for clean shutdown

static void sig_handler(int sig) { running = false; }

//-----------------------------------------------------------------------------------------------------
// BPF functions

static int handle_event(void *ctx, void *data, size_t data_sz) {
    struct event *e = data;

    // Update the global PID variable with the new process ID
    new_process_pid = e->pid;

    printf("Process created: PID=%u, PPID=%u, comm=%s, timestamp=%llu ns\n", e->pid, e->ppid, e->comm, e->timestamp);

    // Trigger XCP measurement event with precise kernel timestamp
    // Convert nanoseconds to microseconds for XCP (assuming XCP expects microseconds)
    uint64_t timestamp_us = e->timestamp / 1000;
    DaqEventAt(process_event, timestamp_us);

    return 0;
}

// Try to load BPF program, continue without it if it fails
static int load_bpf_program() {
    struct bpf_program *prog;
    int err;

    const char *bpf_paths[] = {"process_monitor.bpf.o", "examples/bpf_demo/src/process_monitor.bpf.o", "../examples/bpf_demo/src/process_monitor.bpf.o", NULL};

    for (int i = 0; bpf_paths[i]; i++) {
        obj = bpf_object__open_file(bpf_paths[i], NULL);
        if (obj && !libbpf_get_error(obj)) {
            printf("Found BPF object file at: %s\n", bpf_paths[i]);
            break;
        }
    }

    if (!obj || libbpf_get_error(obj)) {
        printf("Failed to open BPF object file: %ld\n", libbpf_get_error(obj));
        return -1;
    }

    err = bpf_object__load(obj);
    if (err) {
        printf("Failed to load BPF object: %d\n", err);
        return -1;
    }

    prog = bpf_object__find_program_by_name(obj, "trace_process_fork");
    if (!prog) {
        printf("Failed to find BPF program\n");
        return -1;
    }

    bpf_link = bpf_program__attach(prog);
    if (libbpf_get_error(bpf_link)) {
        printf("Failed to attach BPF program: %ld\n", libbpf_get_error(bpf_link));
        return -1;
    }

    map_fd = bpf_object__find_map_fd_by_name(obj, "rb");
    if (map_fd < 0) {
        printf("Failed to find BPF map\n");
        return -1;
    }

    rb = ring_buffer__new(map_fd, handle_event, NULL, NULL);
    if (!rb) {
        printf("Failed to create ring buffer\n");
        return -1;
    }

    printf("BPF program loaded and attached successfully\n");
    return 0;
}

static void cleanup_bpf() {
    if (rb) {
        ring_buffer__free(rb);
        rb = NULL;
    }
    if (bpf_link) {
        bpf_link__destroy(bpf_link);
        bpf_link = NULL;
    }
    if (obj) {
        bpf_object__close(obj);
        obj = NULL;
    }
}

//-----------------------------------------------------------------------------------------------------
// Main

int main(int argc, char *argv[]) {
    printf("\nXCP on %s %s C xcplib demo\n", OPTION_USE_TCP ? "TCP" : "Ethernet", OPTION_PROJECT_NAME);

    // Install signal handler
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Try to initialize BPF program
    if (load_bpf_program() != 0) {
        printf("Warning: Failed to initialize BPF program. Running without BPF monitoring.\n");
        bpf_enabled = false;
    } else {
        bpf_enabled = true;
    }

    // Init XCP
    XcpInit(true);

    // XCP: Initialize the XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // XCP: Enable inline A2L generation
    if (!A2lInit(OPTION_PROJECT_NAME, NULL, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create events for DAQ (Data Acquisition)
    DaqCreateEvent(mainloop_event);
    DaqCreateEvent(process_event);

    // A2L file generation - register measurement variables
    A2lSetAbsoluteAddrMode(mainloop_event);

    A2lCreateMeasurement(static_counter, "Counter value");    // Measurement variable for static_counter
    A2lCreateMeasurement(new_process_pid, "New process PID"); // Measurement variable for new process PID
    // A2lCreateMeasurement(static_struct, "Demo struct");    // Skip struct for now to avoid A2L issues

    // Start main loop
    printf("Start main loop...\n");
    while (running) {

        // Update counter
        static_counter++;

        // Poll BPF events if enabled
        if (bpf_enabled) {
            ring_buffer__poll(rb, 10); // 10ms timeout
        }

        // Trigger DAQ event for periodic measurements
        DaqEvent(mainloop_event);

        // Sleep for a short period
        sleepUs(100000); // 100ms
    }

    printf("Shutting down...\n");

#ifdef __linux__
    cleanup_bpf();
#endif

    // Disconnect XCP
    XcpEthServerShutdown();

    return 0;
}
