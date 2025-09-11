
#include "process_monitor.bpf.h"

/*
#include <linux/bpf.h>        // Core BPF definitions
#include <bpf/bpf_helpers.h>  // Helper function declarations
#include <bpf/bpf_tracing.h>  // Tracing-specific helpers
*/

// BPF helper definitions (compatible with standard headers but self-contained)
#define SEC(name) __attribute__((section(name), used))
#define __uint(name, val) int (*name)[val]
#define __type(name, val) typeof(val) *name

// BPF map types (from linux/bpf.h)
#define BPF_MAP_TYPE_RINGBUF 27
#define BPF_MAP_TYPE_ARRAY 2

// BPF helper function prototypes (using standard libbpf signatures)
static void *(*bpf_ringbuf_reserve)(void *ringbuf, u64 size, u64 flags) = (void *)131;
static void (*bpf_ringbuf_submit)(void *data, u64 flags) = (void *)132;
static u64 (*bpf_ktime_get_ns)(void) = (void *)5;
static u64 (*bpf_get_current_pid_tgid)(void) = (void *)14;
static long (*bpf_get_current_comm)(void *buf, u32 size_of_buf) = (void *)16;
static u32 (*bpf_get_smp_processor_id)(void) = (void *)8;
static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, u64 flags) = (void *)2;

// BPF ring buffer for sending events to userspace
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

// BPF map to count all syscalls
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, MAX_SYSCALL_NR);
    __type(key, u32);
    __type(value, u64);
} syscall_counters SEC(".maps");

//--------------------------------------------------------------------------------------

SEC("tp/sched/sched_process_fork")
int trace_process_fork(void *ctx) {
    struct event *e;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    // Capture precise kernel timestamp first
    e->timestamp = bpf_ktime_get_ns();
    e->event_type = EVENT_PROCESS_FORK;
    e->cpu_id = bpf_get_smp_processor_id();

    // For tracepoint context, we need to read the data differently
    // This is a simplified version that gets the current process info
    u64 pid_tgid = bpf_get_current_pid_tgid();
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
    u64 unused__;
    long id;     // syscall number
    u64 args[6]; // syscall arguments
};

// Helper function to enable event generation on syscalls
static inline u32 classify_syscall(u32 syscall_nr) {

    switch (syscall_nr) {

    // Ignore some high-frequency less interesting syscalls
    case SYS_clock_nanosleep:
    case SYS_nanosleep:
    case SYS_write:
    case SYS_read:
    case SYS_getrandom:
    case SYS_rt_sigaction:
    case SYS_rt_sigprocmask:
    case SYS_ppoll:
    case SYS_epoll_pwait:
        return 0;

    default:
        return 1;
    }
}

// High-frequency syscall tracepoint - now tracks ALL syscalls
SEC("tp/raw_syscalls/sys_enter")
int trace_syscall_enter(void *ctx) {
    struct event *e;
    struct syscall_enter_args *args = (struct syscall_enter_args *)ctx;

    // Get the syscall number
    u32 syscall_nr = (u32)args->id;

    // Bounds check for BPF verifier
    if (syscall_nr >= MAX_SYSCALL_NR) {
        return 0;
    }

    // Update syscall counters in BPF map
    u64 *counter = bpf_map_lookup_elem(&syscall_counters, &syscall_nr);
    if (counter) {
        __sync_fetch_and_add(counter, 1);
    } else {
        u64 initial_count = 1;
        bpf_map_update_elem(&syscall_counters, &syscall_nr, &initial_count, 0);
    }

    // Only send detailed events for interesting syscalls to reduce overhead
    u32 category = classify_syscall(syscall_nr);
    if (category == 0) {
        return 0; // Skip sending event for uninteresting syscalls
    }

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->timestamp = bpf_ktime_get_ns();
    e->event_type = EVENT_SYSCALL;
    e->data.syscall.syscall_nr = syscall_nr;
    e->cpu_id = bpf_get_smp_processor_id();
    u64 pid_tgid = bpf_get_current_pid_tgid();
    e->data.syscall.pid = pid_tgid & 0xFFFFFFFF;
    e->data.syscall.tgid = (pid_tgid >> 32) & 0xFFFFFFFF;
    bpf_get_current_comm(&e->data.syscall.comm, sizeof(e->data.syscall.comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
