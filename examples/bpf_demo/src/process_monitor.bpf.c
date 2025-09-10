// Define types first to avoid header conflicts
typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;

// Define minimal BPF helpers to avoid problematic headers
#define SEC(name) __attribute__((section(name), used))
#define BPF_MAP_TYPE_RINGBUF 27
#define BPF_MAP_TYPE_ARRAY 2
#define __uint(name, val) int (*name)[val]
#define __always_inline inline __attribute__((always_inline))

// BPF helper function declarations (minimal set)
static void *(*bpf_ringbuf_reserve)(void *ringbuf, __u64 size, __u64 flags) = (void *)131;
static void (*bpf_ringbuf_submit)(void *data, __u64 flags) = (void *)132;
static __u64 (*bpf_ktime_get_ns)(void) = (void *)5;
static __u64 (*bpf_get_current_pid_tgid)(void) = (void *)14;
static long (*bpf_get_current_comm)(void *buf, __u32 size_of_buf) = (void *)16;
static __u32 (*bpf_get_smp_processor_id)(void) = (void *)8;
static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, __u64 flags) = (void *)2;

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

// Event types to distinguish different tracepoints
#define EVENT_PROCESS_FORK 1
#define EVENT_SYSCALL 2
#define EVENT_TIMER_TICK 3

// Maximum syscall number on ARM64 (based on __NR_syscalls)
#define MAX_SYSCALL_NR 463

// BPF map to count all syscalls
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, MAX_SYSCALL_NR);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u64));
} syscall_counters SEC(".maps");

struct event {
    __u64 timestamp;  // Precise kernel timestamp from bpf_ktime_get_ns()
    __u32 event_type; // Type of event (fork, syscall, timer)
    __u32 cpu_id;     // CPU where event occurred

    // Union for event-specific data
    union {
        // Fork event data
        struct {
            __u32 pid;
            __u32 ppid;
            char comm[16];
            char parent_comm[16];
        } fork;

        // Syscall event data
        struct {
            __u32 pid;
            __u32 syscall_nr; // Syscall number
            char comm[16];
            __u32 tgid;             // Thread group ID
            __u32 is_tracked;       // 1 if this is a tracked syscall, 0 otherwise
            __u32 syscall_category; // Category: 1=timing, 2=memory, 3=thread, 4=sync
        } syscall;

        // Timer/IRQ event data
        struct {
            __u32 irq_vec;      // IRQ vector number
            __u32 softirq_type; // Softirq type
            __u32 cpu_load;     // Simple CPU activity indicator
            __u32 reserved;     // For future use
        } timer;
    } data;
};

//--------------------------------------------------------------------------------------

SEC("tp/sched/sched_process_fork")
int trace_process_fork(void *ctx) {
    struct event *e;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    // Clear the entire structure first
    __builtin_memset(e, 0, sizeof(*e));

    // Capture precise kernel timestamp first
    e->timestamp = bpf_ktime_get_ns();
    e->event_type = EVENT_PROCESS_FORK;
    e->cpu_id = bpf_get_smp_processor_id();

    // For tracepoint context, we need to read the data differently
    // This is a simplified version that gets the current process info
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    e->data.fork.pid = pid_tgid & 0xFFFFFFFF;          // Lower 32 bits = TID
    e->data.fork.ppid = (pid_tgid >> 32) & 0xFFFFFFFF; // Upper 32 bits = PID

    bpf_get_current_comm(&e->data.fork.comm, sizeof(e->data.fork.comm));
    // For fork events, we could get parent comm too, but this is simplified
    // Just copy manually to avoid __builtin_memcpy which can cause BPF verifier issues
    for (int i = 0; i < 16; i++) {
        e->data.fork.parent_comm[i] = e->data.fork.comm[i];
    }

    bpf_ringbuf_submit(e, 0);
    return 0;
}

//--------------------------------------------------------------------------------------

// Structure for raw_syscalls/sys_enter tracepoint
struct syscall_enter_args {
    __u64 __unused__;
    long id;       // syscall number
    __u64 args[6]; // syscall arguments
};

// Interesting syscalls for monitoring (ARM64 numbers)
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

// Helper function to classify syscalls
static __always_inline __u32 classify_syscall(__u32 syscall_nr) {
    switch (syscall_nr) {
    // Timing & Scheduling (category 1)
    case SYSCALL_NANOSLEEP:
    case SYSCALL_CLOCK_GETTIME:
    case SYSCALL_SCHED_YIELD:
    case SYSCALL_SCHED_SETSCHEDULER:
        return 1;

    // Memory Management (category 2)
    case SYSCALL_MMAP:
    case SYSCALL_MUNMAP:
    case SYSCALL_BRK:
    case SYSCALL_MPROTECT:
        return 2;

    // Thread & Process Management (category 3)
    case SYSCALL_CLONE:
    case SYSCALL_EXIT:
    case SYSCALL_WAIT4:
        return 3;

    // Synchronization (category 4)
    case SYSCALL_FUTEX:
    case SYSCALL_PIPE2:
        return 4;

    default:
        return 0; // Not tracked
    }
}

// High-frequency syscall tracepoint - now tracks ALL syscalls
SEC("tp/raw_syscalls/sys_enter")
int trace_syscall_enter(void *ctx) {
    struct event *e;
    struct syscall_enter_args *args = (struct syscall_enter_args *)ctx;

    // Get the syscall number
    __u32 syscall_nr = (__u32)args->id;

    // Bounds check for BPF verifier
    if (syscall_nr >= MAX_SYSCALL_NR) {
        return 0;
    }

    // Update syscall counter in BPF map
    __u64 *counter = bpf_map_lookup_elem(&syscall_counters, &syscall_nr);
    if (counter) {
        __sync_fetch_and_add(counter, 1);
    } else {
        __u64 initial_count = 1;
        bpf_map_update_elem(&syscall_counters, &syscall_nr, &initial_count, 0);
    }

    // Only send detailed events for interesting syscalls to reduce overhead
    __u32 category = classify_syscall(syscall_nr);
    if (category == 0) {
        return 0; // Skip sending event for uninteresting syscalls
    }

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    // Clear the entire structure first
    __builtin_memset(e, 0, sizeof(*e));

    // Capture precise kernel timestamp
    e->timestamp = bpf_ktime_get_ns();
    e->event_type = EVENT_SYSCALL;
    e->cpu_id = bpf_get_smp_processor_id();

    // Get current task info
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    e->data.syscall.pid = pid_tgid & 0xFFFFFFFF;
    e->data.syscall.tgid = (pid_tgid >> 32) & 0xFFFFFFFF;

    // Get the syscall number and classification
    e->data.syscall.syscall_nr = syscall_nr;
    e->data.syscall.is_tracked = 1;
    e->data.syscall.syscall_category = category;

    bpf_get_current_comm(&e->data.syscall.comm, sizeof(e->data.syscall.comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

//--------------------------------------------------------------------------------------

// Alternative high-frequency tracepoint: timer interrupts
// This tracepoint typically requires fewer permissions than sched_switch
SEC("tp/irq/softirq_entry")
int trace_timer_tick(void *ctx) {
    struct event *e;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    // Clear the entire structure first
    __builtin_memset(e, 0, sizeof(*e));

    // Capture precise kernel timestamp
    e->timestamp = bpf_ktime_get_ns();
    e->event_type = EVENT_TIMER_TICK;
    e->cpu_id = bpf_get_smp_processor_id();

    // For softirq_entry, we can extract interrupt-specific information
    // For now, let's use simple values instead of parsing context directly
    e->data.timer.softirq_type = 1; // Dummy value for now
    e->data.timer.irq_vec = 0;      // Dummy value for now

    // Use timestamp variation as a simple CPU activity indicator
    e->data.timer.cpu_load = (__u32)(e->timestamp & 0xFFFFFFFF);
    e->data.timer.reserved = 0;

    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
