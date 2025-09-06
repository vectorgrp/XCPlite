#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <linux/sched.h>

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
int trace_process_fork(struct trace_event_raw_sched_process_fork *ctx) {
    struct event *e;

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    // Capture precise kernel timestamp first
    e->timestamp = bpf_ktime_get_ns();
    e->pid = ctx->child_pid;
    e->ppid = ctx->parent_pid;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
