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

// Syscall monitoring variables
static uint32_t syscall_count = 0;       // Total number of tracked syscalls
static uint32_t current_syscall_nr = 0;  // Current syscall number
static uint32_t current_syscall_pid = 0; // PID making the syscall
static uint32_t syscall_rate = 0;        // Syscalls per second (calculated)
static uint64_t last_syscall_time = 0;   // Timestamp of last syscall

// Syscall category counters
static uint32_t timing_syscalls = 0; // Timing & scheduling syscalls
static uint32_t memory_syscalls = 0; // Memory management syscalls
static uint32_t thread_syscalls = 0; // Thread & process syscalls
static uint32_t sync_syscalls = 0;   // Synchronization syscalls

// Individual syscall counters for interesting ones
static uint32_t nanosleep_count = 0;
static uint32_t mmap_count = 0;
static uint32_t clone_count = 0;
static uint32_t futex_count = 0;

typedef struct {
    uint8_t byte_field;
    int16_t word_field;
} demo_struct_t;

// BPF event structure
// Event structure that matches the BPF program
#define EVENT_PROCESS_FORK 1
#define EVENT_SYSCALL 2
#define EVENT_TIMER_TICK 3

// Interesting syscalls for monitoring (ARM64 numbers) - must match BPF program
#define SYSCALL_FUTEX 98               // Fast userspace mutexes
#define SYSCALL_EXIT 93                // Process termination
#define SYSCALL_CLOCK_GETTIME 113      // Time queries
#define SYSCALL_NANOSLEEP 115          // Sleep operations
#define SYSCALL_SCHED_SETSCHEDULER 119 // Scheduler policy changes
#define SYSCALL_SCHED_YIELD 124        // Voluntary CPU yielding
#define SYSCALL_BRK 214                // Heap management
#define SYSCALL_MUNMAP 215             // Memory unmapping
#define SYSCALL_CLONE 220              // Thread/process creation
#define SYSCALL_MMAP 222               // Memory mapping
#define SYSCALL_MPROTECT 226           // Memory protection changes
#define SYSCALL_WAIT4 260              // Process waiting
#define SYSCALL_PIPE2 59               // Inter-process communication

// Syscall categories
#define CATEGORY_TIMING 1
#define CATEGORY_MEMORY 2
#define CATEGORY_THREAD 3
#define CATEGORY_SYNC 4

// Syscall lookup table
struct syscall_info {
    uint32_t number;
    const char *name;
    const char *category_name;
};

static const struct syscall_info syscall_lookup[] = {
    {SYSCALL_FUTEX, "futex", "sync"},
    {SYSCALL_EXIT, "exit", "thread"},
    {SYSCALL_CLOCK_GETTIME, "clock_gettime", "timing"},
    {SYSCALL_NANOSLEEP, "nanosleep", "timing"},
    {SYSCALL_SCHED_SETSCHEDULER, "sched_setscheduler", "timing"},
    {SYSCALL_SCHED_YIELD, "sched_yield", "timing"},
    {SYSCALL_BRK, "brk", "memory"},
    {SYSCALL_MUNMAP, "munmap", "memory"},
    {SYSCALL_CLONE, "clone", "thread"},
    {SYSCALL_MMAP, "mmap", "memory"},
    {SYSCALL_MPROTECT, "mprotect", "memory"},
    {SYSCALL_WAIT4, "wait4", "thread"},
    {SYSCALL_PIPE2, "pipe2", "sync"},
};

static const char *get_syscall_name(uint32_t syscall_nr) {
    for (int i = 0; i < sizeof(syscall_lookup) / sizeof(syscall_lookup[0]); i++) {
        if (syscall_lookup[i].number == syscall_nr) {
            return syscall_lookup[i].name;
        }
    }
    return "unknown";
}

static const char *get_category_name(uint32_t category) {
    switch (category) {
    case CATEGORY_TIMING:
        return "timing";
    case CATEGORY_MEMORY:
        return "memory";
    case CATEGORY_THREAD:
        return "thread";
    case CATEGORY_SYNC:
        return "sync";
    default:
        return "unknown";
    }
}

// Userspace version of classify_syscall function (must match BPF version)
static uint32_t classify_syscall(uint32_t syscall_nr) {
    switch (syscall_nr) {
    // Timing & Scheduling (category 1)
    case SYSCALL_NANOSLEEP:
    case SYSCALL_CLOCK_GETTIME:
    case SYSCALL_SCHED_YIELD:
    case SYSCALL_SCHED_SETSCHEDULER:
        return CATEGORY_TIMING;

    // Memory Management (category 2)
    case SYSCALL_MMAP:
    case SYSCALL_MUNMAP:
    case SYSCALL_BRK:
    case SYSCALL_MPROTECT:
        return CATEGORY_MEMORY;

    // Thread & Process Management (category 3)
    case SYSCALL_CLONE:
    case SYSCALL_EXIT:
    case SYSCALL_WAIT4:
        return CATEGORY_THREAD;

    // Synchronization (category 4)
    case SYSCALL_FUTEX:
    case SYSCALL_PIPE2:
        return CATEGORY_SYNC;

    default:
        return 0; // Not tracked
    }
}

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
            uint32_t tgid;             // Thread group ID
            uint32_t is_tracked;       // 1 if this is a tracked syscall, 0 otherwise
            uint32_t syscall_category; // Category: 1=timing, 2=memory, 3=thread, 4=sync
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
static int map_fd = -1;               // BPF ring buffer map file descriptor
static int syscall_counters_fd = -1;  // BPF syscall counters map file descriptor

//-----------------------------------------------------------------------------------------------------
// XCP Events

DAQ_CREATE_EVENT_VARIABLE(mainloop_event);
DAQ_CREATE_EVENT_VARIABLE(process_event);
DAQ_CREATE_EVENT_VARIABLE(ctx_switch);

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

//-----------------------------------------------------------------------------------------------------
// Print comprehensive syscall statistics from BPF map

static void print_all_syscall_stats(int map_fd) {
    printf("\n=== Complete Syscall Statistics ===\n");

    uint64_t total_syscalls = 0;
    uint32_t active_syscalls = 0;
    uint64_t top_syscalls[10] = {0}; // Track top 10 syscall counts
    uint32_t top_numbers[10] = {0};  // Track corresponding syscall numbers

    // Read all syscall counters from the BPF map
    for (uint32_t syscall_nr = 0; syscall_nr < 463; syscall_nr++) {
        uint64_t count = 0;
        if (bpf_map_lookup_elem(map_fd, &syscall_nr, &count) == 0 && count > 0) {
            total_syscalls += count;
            active_syscalls++;

            // Track top 10 syscalls
            for (int i = 0; i < 10; i++) {
                if (count > top_syscalls[i]) {
                    // Shift everything down
                    for (int j = 9; j > i; j--) {
                        top_syscalls[j] = top_syscalls[j - 1];
                        top_numbers[j] = top_numbers[j - 1];
                    }
                    // Insert new top syscall
                    top_syscalls[i] = count;
                    top_numbers[i] = syscall_nr;
                    break;
                }
            }
        }
    }

    printf("Total syscalls captured: %llu\n", total_syscalls);
    printf("Active syscall types: %u / 463\n", active_syscalls);
    printf("\nTop 10 Most Frequent Syscalls:\n");

    for (int i = 0; i < 10 && top_syscalls[i] > 0; i++) {
        const char *name = get_syscall_name(top_numbers[i]);
        const char *category = get_category_name(classify_syscall(top_numbers[i]));
        double percentage = (double)top_syscalls[i] / total_syscalls * 100.0;

        printf("  %2d. %s(%u) [%s]: %llu calls (%.1f%%)\n", i + 1, name, top_numbers[i], category, top_syscalls[i], percentage);
    }
    printf("\n");
}

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

        // Update category counters
        switch (e->data.syscall.syscall_category) {
        case CATEGORY_TIMING:
            timing_syscalls++;
            break;
        case CATEGORY_MEMORY:
            memory_syscalls++;
            break;
        case CATEGORY_THREAD:
            thread_syscalls++;
            break;
        case CATEGORY_SYNC:
            sync_syscalls++;
            break;
        }

        // Update individual syscall counters
        switch (current_syscall_nr) {
        case SYSCALL_NANOSLEEP:
            nanosleep_count++;
            break;
        case SYSCALL_MMAP:
            mmap_count++;
            break;
        case SYSCALL_CLONE:
            clone_count++;
            break;
        case SYSCALL_FUTEX:
            futex_count++;
            break;
        }

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

            const char *syscall_name = get_syscall_name(current_syscall_nr);
            const char *category_name = get_category_name(e->data.syscall.syscall_category);
            printf("Tracked syscalls/sec: %u (Total: %u), Last: %s(%u) [%s] from PID %u\n", syscall_rate, syscall_count, syscall_name, current_syscall_nr, category_name,
                   current_syscall_pid);
            printf("  Categories - timing: %u, memory: %u, thread: %u, sync: %u\n", timing_syscalls, memory_syscalls, thread_syscalls, sync_syscalls);

            // Show comprehensive syscall statistics every 10 seconds
            static uint32_t stats_counter = 0;
            stats_counter++;
            if (stats_counter >= 10) {
                print_all_syscall_stats(syscall_counters_fd);
                stats_counter = 0;
            }
        }

        // Optional: Print detailed syscall info (comment out for less verbose output)
        // const char* syscall_name = get_syscall_name(e->data.syscall.syscall_nr);
        // printf("Syscall: %s[%u] called %s(%u) [%s] on CPU%u, timestamp=%llu ns\n",
        //        e->data.syscall.comm, e->data.syscall.pid, syscall_name,
        //        e->data.syscall.syscall_nr, get_category_name(e->data.syscall.syscall_category),
        //        e->cpu_id, e->timestamp);

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
        printf("Failed to find BPF ring buffer map\n");
        return -1;
    }

    // Get syscall counters map file descriptor
    syscall_counters_fd = bpf_object__find_map_fd_by_name(obj, "syscall_counters");
    if (syscall_counters_fd < 0) {
        printf("Failed to find BPF syscall_counters map\n");
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
    A2lCreateMeasurement(syscall_count, "Total tracked syscalls");      // Total syscall count
    A2lCreateMeasurement(current_syscall_nr, "Current syscall number"); // Current syscall number
    A2lCreateMeasurement(current_syscall_pid, "Syscall PID");           // PID making syscalls
    A2lCreateMeasurement(syscall_rate, "Tracked syscalls per second");  // Syscall rate calculation
    A2lCreateMeasurement(last_syscall_time, "Last syscall time");       // Syscall timestamp

    // Syscall category counters
    A2lCreateMeasurement(timing_syscalls, "Timing/scheduling syscalls"); // Timing syscalls
    A2lCreateMeasurement(memory_syscalls, "Memory management syscalls"); // Memory syscalls
    A2lCreateMeasurement(thread_syscalls, "Thread/process syscalls");    // Thread syscalls
    A2lCreateMeasurement(sync_syscalls, "Synchronization syscalls");     // Sync syscalls

    // Individual interesting syscall counters
    A2lCreateMeasurement(nanosleep_count, "nanosleep syscall count"); // Sleep calls
    A2lCreateMeasurement(mmap_count, "mmap syscall count");           // Memory mapping
    A2lCreateMeasurement(clone_count, "clone syscall count");         // Thread creation
    A2lCreateMeasurement(futex_count, "futex syscall count");         // Fast mutex ops

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
