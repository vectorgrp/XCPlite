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

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

struct event {
    __u64 timestamp; // Precise kernel timestamp from bpf_ktime_get_ns()
    __u32 pid;
    __u32 ppid;
    char comm[16];
};

SEC("tp/sched/sched_process_fork")
int trace_process_fork(void *ctx) {
    struct event *e;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    // Capture precise kernel timestamp first
    e->timestamp = bpf_ktime_get_ns();

    // For tracepoint context, we need to read the data differently
    // This is a simplified version that gets the current process info
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    e->pid = pid_tgid & 0xFFFFFFFF;          // Lower 32 bits = TID
    e->ppid = (pid_tgid >> 32) & 0xFFFFFFFF; // Upper 32 bits = PID

    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
