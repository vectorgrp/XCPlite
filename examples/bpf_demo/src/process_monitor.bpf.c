// Define types first to avoid header conflicts
typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;

// Define minimal BPF helpers to avoid problematic headers
#define SEC(name) __attribute__((section(name), used))
#define BPF_MAP_TYPE_RINGBUF 27
#define __uint(name, val) int (*name)[val]

// BPF helper function declarations (minimal set)
static void *(*bpf_ringbuf_reserve)(void *ringbuf, __u64 size, __u64 flags) = (void *)131;
static void (*bpf_ringbuf_submit)(void *data, __u64 flags) = (void *)132;
static __u64 (*bpf_ktime_get_ns)(void) = (void *)5;
static __u64 (*bpf_get_current_pid_tgid)(void) = (void *)14;
static long (*bpf_get_current_comm)(void *buf, __u32 size_of_buf) = (void *)16;
static __u32 (*bpf_get_smp_processor_id)(void) = (void *)8;

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

// Event types to distinguish different tracepoints
#define EVENT_PROCESS_FORK 1
#define EVENT_SYSCALL 2
#define EVENT_TIMER_TICK 3

struct event {
    __u64 timestamp;  // Precise kernel timestamp from bpf_ktime_get_ns()
    __u32 event_type; // Type of event (fork, context switch, etc.)
    __u32 pid;
    __u32 ppid;         // For fork events: parent PID, for sched: previous state
    __u32 next_pid;     // For context switch: next PID to be scheduled
    __u32 cpu_id;       // CPU where event occurred
    char comm[16];      // Process/thread name
    char next_comm[16]; // For context switch: next process name
};

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

    // For tracepoint context, we need to read the data differently
    // This is a simplified version that gets the current process info
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    e->pid = pid_tgid & 0xFFFFFFFF;          // Lower 32 bits = TID
    e->ppid = (pid_tgid >> 32) & 0xFFFFFFFF; // Upper 32 bits = PID
    e->cpu_id = bpf_get_smp_processor_id();

    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

// High-frequency syscall tracepoint - more accessible than sched_switch
SEC("tp/raw_syscalls/sys_enter")
int trace_syscall_enter(void *ctx) {
    struct event *e;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    // Clear the entire structure first
    __builtin_memset(e, 0, sizeof(*e));

    // Capture precise kernel timestamp
    e->timestamp = bpf_ktime_get_ns();
    e->event_type = EVENT_SYSCALL; // Reusing event type for high-frequency events

    // Get current task info
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    e->pid = pid_tgid & 0xFFFFFFFF;
    e->ppid = (pid_tgid >> 32) & 0xFFFFFFFF;
    e->cpu_id = bpf_get_smp_processor_id();

    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

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

    // Get current task info
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    e->pid = pid_tgid & 0xFFFFFFFF;
    e->ppid = (pid_tgid >> 32) & 0xFFFFFFFF;
    e->cpu_id = bpf_get_smp_processor_id();

    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
