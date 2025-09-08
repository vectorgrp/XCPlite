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
#define EVENT_SYSCALL 2
#define EVENT_TIMER_TICK 3

struct event {
    uint64_t timestamp;  // Precise kernel timestamp from bpf_ktime_get_ns()
    uint32_t event_type; // Type of event (fork, syscall, timer)
    uint32_t cpu_id;     // CPU where event occurred

    // Union for event-specific data
    union {
        // Fork event data
        struct {
            uint32_t pid;
            uint32_t ppid;
            char comm[16];
            char parent_comm[16];
        } fork;

        // Syscall event data
        struct {
            uint32_t pid;
            uint32_t syscall_nr; // Syscall number
            char comm[16];
            uint32_t tgid; // Thread group ID
        } syscall;

        // Timer/IRQ event data
        struct {
            uint32_t irq_vec;      // IRQ vector number
            uint32_t softirq_type; // Softirq type
            uint32_t cpu_load;     // Simple CPU activity indicator
            uint32_t reserved;     // For future use
        } timer;
    } data;
};

static uint16_t static_counter = 0;                                      // Local counter variable for measurement
static demo_struct_t static_struct = {.byte_field = 1, .word_field = 2}; // Single instance of demo_struct_t

// Process monitoring variables
static uint32_t new_process_pid = 0; // Global variable to store new process PID

// Syscall monitoring variables
static uint32_t syscall_count = 0;       // Total number of syscalls
static uint32_t current_syscall_nr = 0;  // Current syscall number
static uint32_t current_syscall_pid = 0; // PID making the syscall
static uint32_t syscall_rate = 0;        // Syscalls per second (calculated)
static uint64_t last_syscall_time = 0;   // Timestamp of last syscall

// Timer/IRQ monitoring variables
static uint32_t timer_tick_count = 0;     // Total number of timer ticks
static uint32_t current_softirq_type = 0; // Current softirq type
static uint32_t current_irq_vec = 0;      // Current IRQ vector
static uint32_t timer_tick_rate = 0;      // Timer ticks per second (calculated)
static uint64_t last_timer_tick_time = 0; // Timestamp of last timer tick
#define MAX_CPU_COUNT 16
static uint32_t cpu_utilization[MAX_CPU_COUNT] = {0}; // Per-CPU activity counters

static struct bpf_object *obj = NULL;
static struct bpf_link *process_fork_link = NULL; // Link for process fork tracepoint
static struct bpf_link *syscall_link = NULL;      // Link for syscall tracepoint
static struct bpf_link *timer_tick_link = NULL;   // Link for timer tick tracepoint

static struct ring_buffer *rb = NULL; // BPF ring buffer for receiving events
static int map_fd = -1;               // BPF map file descriptor

//-----------------------------------------------------------------------------------------------------
// Signal handler for clean shutdown

static volatile bool running = true; // Control flag for main loop
static void sig_handler(int sig) { running = false; }

//-----------------------------------------------------------------------------------------------------
// BPF functions

#ifdef OPTION_CLOCK_TICKS_1US
#define TO_XCP_CLOCK_TICKS(timestamp) ((timestamp) / 1000)
#else
#define TO_XCP_CLOCK_TICKS(timestamp) (timestamp)
#endif

static int handle_event(void *ctx, void *data, size_t data_sz) {
    struct event *e = data;
    static uint64_t last_rate_calculation_time = 0;
    static uint64_t syscall_count_last_second = 0;
    static uint64_t timer_count_last_second = 0;

    if (e->event_type == EVENT_PROCESS_FORK) {
        // Handle process fork events
        new_process_pid = e->data.fork.pid;
        printf("Process created: PID=%u, PPID=%u, comm=%s, parent_comm=%s, CPU=%u, timestamp=%llu ns\n", e->data.fork.pid, e->data.fork.ppid, e->data.fork.comm,
               e->data.fork.parent_comm, e->cpu_id, e->timestamp);

        // Trigger XCP measurement event with precise kernel timestamp
        uint64_t timestamp_us = e->timestamp / 1000;
        DaqEventAt(process_event, timestamp_us);

    } else if (e->event_type == EVENT_SYSCALL) {
        // Handle syscall events
        syscall_count++;
        current_syscall_nr = e->data.syscall.syscall_nr;
        current_syscall_pid = e->data.syscall.pid;
        last_syscall_time = e->timestamp;

        // Update per-CPU counters (protect against array bounds)
        if (e->cpu_id < MAX_CPU_COUNT) {
            cpu_utilization[e->cpu_id]++;
        }

        // Calculate syscall rate every second
        uint64_t current_time_ns = e->timestamp;
        if (last_rate_calculation_time == 0) {
            last_rate_calculation_time = current_time_ns;
            syscall_count_last_second = syscall_count;
        } else if ((current_time_ns - last_rate_calculation_time) >= 1000000000ULL) { // 1 second in ns
            syscall_rate = syscall_count - syscall_count_last_second;
            syscall_count_last_second = syscall_count;
            last_rate_calculation_time = current_time_ns;
            printf("Syscalls/sec: %u (Total: %u), Last syscall: %u from PID %u\n", syscall_rate, syscall_count, current_syscall_nr, current_syscall_pid);
        }

        // Optional: Print detailed syscall info (comment out for less verbose output)
        // printf("Syscall: %s[%u] called syscall %u on CPU%u, timestamp=%llu ns\n",
        //        e->data.syscall.comm, e->data.syscall.pid, e->data.syscall.syscall_nr, e->cpu_id, e->timestamp);

        // Trigger XCP measurement event for syscalls
        uint64_t timestamp_us = e->timestamp / 1000;
        DaqEventAt(ctx_switch, timestamp_us);

    } else if (e->event_type == EVENT_TIMER_TICK) {
        // Handle timer tick events (IRQ/softirq activity)
        timer_tick_count++;
        current_softirq_type = e->data.timer.softirq_type;
        current_irq_vec = e->data.timer.irq_vec;
        last_timer_tick_time = e->timestamp;

        // Update per-CPU counters
        if (e->cpu_id < MAX_CPU_COUNT) {
            cpu_utilization[e->cpu_id]++;
        }

        // Calculate timer tick rate every second
        uint64_t current_time_ns = e->timestamp;
        if (last_rate_calculation_time == 0) {
            last_rate_calculation_time = current_time_ns;
            timer_count_last_second = timer_tick_count;
        } else if ((current_time_ns - last_rate_calculation_time) >= 1000000000ULL) { // 1 second in ns
            timer_tick_rate = timer_tick_count - timer_count_last_second;
            timer_count_last_second = timer_tick_count;
            last_rate_calculation_time = current_time_ns;

            printf("Timer ticks/sec: %u (Total: %u), Last: softirq_type=%u, irq_vec=%u\n", timer_tick_rate, timer_tick_count, current_softirq_type, current_irq_vec);
        }

        // Optional: Print detailed timer tick info (comment out for less verbose output)
        // printf("Timer tick: softirq_type=%u, irq_vec=%u, cpu_load=%u on CPU%u, timestamp=%llu ns\n",
        //        e->data.timer.softirq_type, e->data.timer.irq_vec, e->data.timer.cpu_load, e->cpu_id, e->timestamp);

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

    // Try to open BPF object file from multiple possible paths
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

    // Load BPF object
    err = bpf_object__load(obj);
    if (err) {
        printf("Failed to load BPF object: %d\n", err);
        return -1;
    }

    // Attach process fork tracepoint
    prog = bpf_object__find_program_by_name(obj, "trace_process_fork");
    process_fork_link = NULL;
    if (!prog) {
        printf("Failed to find BPF program 'trace_process_fork'\n");
    } else {
        process_fork_link = bpf_program__attach(prog);
        if (libbpf_get_error(process_fork_link)) {
            printf("Failed to attach process fork BPF program: %ld\n", libbpf_get_error(process_fork_link));
        } else {
            printf("Process fork tracepoint attached successfully\n");
        }
    }

    // Attach syscall tracepoint
    prog = bpf_object__find_program_by_name(obj, "trace_syscall_enter");
    syscall_link = NULL;
    if (!prog) {
        printf("Failed to find BPF program 'trace_syscall_enter'\n");
    } else {
        syscall_link = bpf_program__attach(prog);
        if (libbpf_get_error(syscall_link)) {
            printf("Warning: Failed to attach syscall BPF program: %ld\n", libbpf_get_error(syscall_link));
        } else {
            printf("Syscall tracepoint attached successfully\n");
        }
    }

    // Attach timer tick tracepoint
    prog = bpf_object__find_program_by_name(obj, "trace_timer_tick");
    timer_tick_link = NULL;
    if (!prog) {
        printf("Failed to find BPF program 'trace_timer_tick'\n");
    } else {
        timer_tick_link = bpf_program__attach(prog);
        if (libbpf_get_error(timer_tick_link)) {
            printf("Warning: Failed to attach timer tick BPF program: %ld\n", libbpf_get_error(timer_tick_link));

        } else {
            printf("Timer tick tracepoint attached successfully (alternative high-frequency monitoring)\n");
        }
    }

    // Get BPF map file descriptor
    map_fd = bpf_object__find_map_fd_by_name(obj, "rb");
    if (map_fd < 0) {
        printf("Failed to find BPF map\n");
        return -1;
    }
    // Set up ring buffer to receive events on callback function handle_event
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
    if (syscall_link) {
        bpf_link__destroy(syscall_link);
        syscall_link = NULL;
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
    printf("\nXCP BPF demo\n");

    // Install signal handler
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Try to initialize BPF program
    if (load_bpf_program() != 0) {
        printf("Warning: Failed to initialize BPF program\n");
        return 1;
    }

    // Init XCP
    XcpInit(true);
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        return 1;
    }

    // Enable inline A2L generation
    if (!A2lInit(OPTION_PROJECT_NAME, NULL, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        return 1;
    }

    // Create events for DAQ (Data Acquisition)
    DaqCreateEvent(mainloop_event);
    DaqCreateEvent(process_event);
    DaqCreateEvent(ctx_switch);

    // Register measurement variables
    A2lSetAbsoluteAddrMode(mainloop_event);
    A2lCreateMeasurement(static_counter, "Counter value"); // Measurement variable for static_counter

    // New process PID creation measurement
    A2lSetAbsoluteAddrMode(process_event);
    A2lCreateMeasurement(new_process_pid, "New process PID");

    // Context switch monitoring measurements
    A2lSetAbsoluteAddrMode(ctx_switch);
    A2lCreateMeasurement(syscall_count, "Total syscalls");              // Total syscall count
    A2lCreateMeasurement(current_syscall_nr, "Current syscall number"); // Current syscall number
    A2lCreateMeasurement(current_syscall_pid, "Syscall PID");           // PID making syscalls
    A2lCreateMeasurement(syscall_rate, "Syscalls per second");          // Syscall rate calculation
    A2lCreateMeasurement(last_syscall_time, "Last syscall time");       // Syscall timestamp

    // Timer tick monitoring measurements
    A2lCreateMeasurement(timer_tick_count, "Total timer ticks");        // Total timer tick count
    A2lCreateMeasurement(current_softirq_type, "Current softirq type"); // Current softirq type
    A2lCreateMeasurement(current_irq_vec, "Current IRQ vector");        // Current IRQ vector
    A2lCreateMeasurement(timer_tick_rate, "Timer ticks per second");    // Timer tick rate
    A2lCreateMeasurement(last_timer_tick_time, "Last timer tick time"); // Timer tick timestamp

    // Per-CPU activity counters (now tracks all event types)
    A2lCreateMeasurement(cpu_utilization[0], "CPU0 activity");
    A2lCreateMeasurement(cpu_utilization[1], "CPU1 activity");
    A2lCreateMeasurement(cpu_utilization[2], "CPU2 activity");
    A2lCreateMeasurement(cpu_utilization[3], "CPU3 activity");
    A2lCreateMeasurement(cpu_utilization[4], "CPU4 activity");
    A2lCreateMeasurement(cpu_utilization[5], "CPU5 activity");
    A2lCreateMeasurement(cpu_utilization[6], "CPU6 activity");
    A2lCreateMeasurement(cpu_utilization[7], "CPU7 activity");
    A2lCreateMeasurement(cpu_utilization[8], "CPU8 activity");
    A2lCreateMeasurement(cpu_utilization[9], "CPU9 activity");
    A2lCreateMeasurement(cpu_utilization[10], "CPU10 activity");
    A2lCreateMeasurement(cpu_utilization[11], "CPU11 activity");
    A2lCreateMeasurement(cpu_utilization[12], "CPU12 activity");
    A2lCreateMeasurement(cpu_utilization[13], "CPU13 activity");
    A2lCreateMeasurement(cpu_utilization[14], "CPU14 activity");
    A2lCreateMeasurement(cpu_utilization[15], "CPU15 activity");

    A2lFinalize(); // Finalize A2L file now, do not wait for XCP connect

    // Start main loop
    printf("Start main loop...\n");
    while (running) {

        // Update counter
        static_counter++;

        // Poll BPF events
        ring_buffer__poll(rb, 10); // 10ms timeout

        // Trigger DAQ event for periodic measurements
        DaqEvent(mainloop_event);

        // Sleep for a short period
        sleepUs(100000); // 100ms
    }

    printf("Shutting down ...\n");
    cleanup_bpf();
    XcpEthServerShutdown();
    return 0;
}
