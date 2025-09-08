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
#define EVENT_PROCESS_FORK 1
#define EVENT_CONTEXT_SWITCH 2
#define EVENT_TIMER_TICK 3

struct event {
    uint64_t timestamp;  // Precise kernel timestamp from bpf_ktime_get_ns()
    uint32_t event_type; // Type of event (fork, context switch, etc.)
    uint32_t pid;
    uint32_t ppid;      // For fork events: parent PID, for sched: previous state
    uint32_t next_pid;  // For context switch: next PID to be scheduled
    uint32_t cpu_id;    // CPU where event occurred
    char comm[16];      // Process/thread name
    char next_comm[16]; // For context switch: next process name
};

static uint16_t static_counter = 0;                                      // Local counter variable for measurement
static demo_struct_t static_struct = {.byte_field = 1, .word_field = 2}; // Single instance of demo_struct_t

// Process monitoring variables
static uint32_t new_process_pid = 0; // Global variable to store new process PID

// Context switch monitoring variables (high frequency data)
static uint32_t context_switch_count = 0;     // Total number of context switches
static uint32_t current_prev_pid = 0;         // Previous task PID
static uint32_t current_next_pid = 0;         // Next task PID
static uint32_t current_cpu_id = 0;           // CPU where context switch occurred
static uint32_t context_switch_rate = 0;      // Context switches per second (calculated)
static uint64_t last_context_switch_time = 0; // Timestamp of last context switch
static uint32_t cpu_utilization[8] = {0};     // Per-CPU context switch counters (up to 8 CPUs)

static volatile bool running = true; // Control flag for main loop

static struct bpf_object *obj = NULL;
static struct bpf_link *process_fork_link = NULL;   // Link for process fork tracepoint
static struct bpf_link *context_switch_link = NULL; // Link for context switch tracepoint
static struct bpf_link *timer_tick_link = NULL;     // Link for timer tick tracepoint (alternative)
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
    static uint64_t context_switch_count_last_second = 0;
    static uint64_t last_rate_calculation_time = 0;

    if (e->event_type == EVENT_PROCESS_FORK) {
        // Handle process fork events
        new_process_pid = e->pid;
        printf("Process created: PID=%u, PPID=%u, comm=%s, CPU=%u, timestamp=%llu ns\n", e->pid, e->ppid, e->comm, e->cpu_id, e->timestamp);

        // Trigger XCP measurement event with precise kernel timestamp
        uint64_t timestamp_us = e->timestamp / 1000;
        DaqEventAt(process_event, timestamp_us);

    } else if (e->event_type == EVENT_CONTEXT_SWITCH) {
        // Handle context switch events (high frequency!)
        context_switch_count++;
        current_prev_pid = e->pid;
        current_next_pid = e->next_pid;
        current_cpu_id = e->cpu_id;
        last_context_switch_time = e->timestamp;

        // Update per-CPU counters (protect against array bounds)
        if (e->cpu_id < 8) {
            cpu_utilization[e->cpu_id]++;
        }

        // Calculate context switch rate every second
        uint64_t current_time_ns = e->timestamp;
        if (last_rate_calculation_time == 0) {
            last_rate_calculation_time = current_time_ns;
            context_switch_count_last_second = context_switch_count;
        } else if ((current_time_ns - last_rate_calculation_time) >= 1000000000ULL) { // 1 second in ns
            context_switch_rate = context_switch_count - context_switch_count_last_second;
            context_switch_count_last_second = context_switch_count;
            last_rate_calculation_time = current_time_ns;

            printf("Context switches/sec: %u (Total: %u)\n", context_switch_rate, context_switch_count);
        }

        // Optional: Print detailed context switch info (comment out for less verbose output)
        // printf("Context switch: %s[%u] -> %s[%u] on CPU%u, state=%u, timestamp=%llu ns\n",
        //        e->comm, e->pid, e->next_comm, e->next_pid, e->cpu_id, e->ppid, e->timestamp);

        // Trigger XCP measurement event for context switches (high frequency data!)
        uint64_t timestamp_us = e->timestamp / 1000;
        DaqEventAt(ctx_switch, timestamp_us);

    } else if (e->event_type == EVENT_TIMER_TICK) {
        // Handle timer tick events (alternative high frequency data)
        context_switch_count++; // Reuse counter for timer ticks
        current_prev_pid = e->pid;
        current_cpu_id = e->cpu_id;
        last_context_switch_time = e->timestamp;

        // Update per-CPU counters
        if (e->cpu_id < 8) {
            cpu_utilization[e->cpu_id]++;
        }

        // Calculate timer tick rate every second
        uint64_t current_time_ns = e->timestamp;
        if (last_rate_calculation_time == 0) {
            last_rate_calculation_time = current_time_ns;
            context_switch_count_last_second = context_switch_count;
        } else if ((current_time_ns - last_rate_calculation_time) >= 1000000000ULL) { // 1 second in ns
            context_switch_rate = context_switch_count - context_switch_count_last_second;
            context_switch_count_last_second = context_switch_count;
            last_rate_calculation_time = current_time_ns;

            printf("Timer ticks/sec: %u (Total: %u)\n", context_switch_rate, context_switch_count);
        }

        // Trigger XCP measurement event for timer ticks (high frequency data!)
        uint64_t timestamp_us = e->timestamp / 1000;
        DaqEventAt(ctx_switch, timestamp_us);
    }

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

    // Attach process fork tracepoint
    prog = bpf_object__find_program_by_name(obj, "trace_process_fork");
    if (!prog) {
        printf("Failed to find BPF program 'trace_process_fork'\n");
        return -1;
    }

    process_fork_link = bpf_program__attach(prog);
    if (libbpf_get_error(process_fork_link)) {
        printf("Failed to attach process fork BPF program: %ld\n", libbpf_get_error(process_fork_link));
        return -1;
    }
    printf("Process fork tracepoint attached successfully\n");

    // Attach context switch tracepoint (may fail due to permissions)
    prog = bpf_object__find_program_by_name(obj, "trace_context_switch");
    if (!prog) {
        printf("Failed to find BPF program 'trace_context_switch'\n");
        // Don't return error - continue with just process monitoring
    } else {
        context_switch_link = bpf_program__attach(prog);
        if (libbpf_get_error(context_switch_link)) {
            printf("Warning: Failed to attach context switch BPF program: %ld\n", libbpf_get_error(context_switch_link));
            printf("This usually requires additional kernel permissions. Trying alternative tracepoint...\n");
            context_switch_link = NULL;
        } else {
            printf("Context switch tracepoint attached successfully\n");
        }
    }

    // If context switch failed, try timer tick tracepoint as alternative
    if (!context_switch_link) {
        prog = bpf_object__find_program_by_name(obj, "trace_timer_tick");
        if (!prog) {
            printf("Failed to find BPF program 'trace_timer_tick'\n");
        } else {
            timer_tick_link = bpf_program__attach(prog);
            if (libbpf_get_error(timer_tick_link)) {
                printf("Warning: Failed to attach timer tick BPF program: %ld\n", libbpf_get_error(timer_tick_link));
                printf("Continuing with process monitoring only.\n");
                timer_tick_link = NULL;
            } else {
                printf("Timer tick tracepoint attached successfully (alternative high-frequency monitoring)\n");
            }
        }
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
    if (process_fork_link) {
        bpf_link__destroy(process_fork_link);
        process_fork_link = NULL;
    }
    if (context_switch_link) {
        bpf_link__destroy(context_switch_link);
        context_switch_link = NULL;
    }
    if (timer_tick_link) {
        bpf_link__destroy(timer_tick_link);
        timer_tick_link = NULL;
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
    DaqCreateEvent(ctx_switch); // High frequency context switch events

    // A2L file generation - register measurement variables
    A2lSetAbsoluteAddrMode(mainloop_event);

    A2lCreateMeasurement(static_counter, "Counter value");    // Measurement variable for static_counter
    A2lCreateMeasurement(new_process_pid, "New process PID"); // Measurement variable for new process PID
    // A2lCreateMeasurement(static_struct, "Demo struct");    // Skip struct for now to avoid A2L issues

    // Context switch monitoring measurements (high frequency data)
    A2lCreateMeasurement(context_switch_count, "Total context switches");       // Total count
    A2lCreateMeasurement(current_prev_pid, "Previous task PID");                // Previous task PID
    A2lCreateMeasurement(current_next_pid, "Next task PID");                    // Next task PID
    A2lCreateMeasurement(current_cpu_id, "Context switch CPU ID");              // CPU ID
    A2lCreateMeasurement(context_switch_rate, "Context switches per second");   // Rate calculation
    A2lCreateMeasurement(last_context_switch_time, "Last context switch time"); // Timestamp

    // Per-CPU context switch counters (great for load balancing visualization)
    A2lCreateMeasurement(cpu_utilization[0], "CPU0 context switches");
    A2lCreateMeasurement(cpu_utilization[1], "CPU1 context switches");
    A2lCreateMeasurement(cpu_utilization[2], "CPU2 context switches");
    A2lCreateMeasurement(cpu_utilization[3], "CPU3 context switches");
    A2lCreateMeasurement(cpu_utilization[4], "CPU4 context switches");
    A2lCreateMeasurement(cpu_utilization[5], "CPU5 context switches");
    A2lCreateMeasurement(cpu_utilization[6], "CPU6 context switches");
    A2lCreateMeasurement(cpu_utilization[7], "CPU7 context switches");

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
