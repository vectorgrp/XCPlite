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
#else
#error "This example only works on Linux systems with BPF support"
#endif

#include "a2l.h"    // for xcplib A2l generation
#include "xcplib.h" // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "bpf_demo"  // Project name, used to build the A2L and BIN file name
#define OPTION_USE_TCP true             // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 512    // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

#define TO_XCP_TIMESTAMP(t) (t / 1000) // Convert to XCP timestamp in microseconds (OPTION_CLOCK_TICKS_1US)
// #define TO_XCP_TIMESTAMP(t) (t)        // Convert to XCP timestamp in nanoseconds (OPTION_CLOCK_TICKS_1NS)

//-----------------------------------------------------------------------------------------------------
// ARM64 Syscall monitoring
// Based on Linux kernel arch/arm64/include/asm/unistd.h and include/uapi/asm-generic/unistd.h

#include "process_monitor.bpf.h"

// syscall counters
static uint32_t syscall_event_counters[MAX_SYSCALL_NR] = {0};

// ARM64 syscall lookup table (major syscalls 0-462) - using constants for clarity
static const char *syscall_names[MAX_SYSCALL_NR] = {[SYS_io_setup] = "io_setup",
                                                    [SYS_io_destroy] = "io_destroy",
                                                    [SYS_io_submit] = "io_submit",
                                                    [SYS_io_cancel] = "io_cancel",
                                                    [SYS_io_getevents] = "io_getevents",
                                                    [SYS_setxattr] = "setxattr",
                                                    [SYS_lsetxattr] = "lsetxattr",
                                                    [SYS_fsetxattr] = "fsetxattr",
                                                    [SYS_getxattr] = "getxattr",
                                                    [SYS_lgetxattr] = "lgetxattr",
                                                    [SYS_fgetxattr] = "fgetxattr",
                                                    [SYS_listxattr] = "listxattr",
                                                    [SYS_llistxattr] = "llistxattr",
                                                    [SYS_flistxattr] = "flistxattr",
                                                    [SYS_removexattr] = "removexattr",
                                                    [SYS_lremovexattr] = "lremovexattr",
                                                    [SYS_fremovexattr] = "fremovexattr",
                                                    [SYS_getcwd] = "getcwd",
                                                    [SYS_lookup_dcookie] = "lookup_dcookie",
                                                    [SYS_eventfd2] = "eventfd2",
                                                    [SYS_epoll_create1] = "epoll_create1",
                                                    [SYS_epoll_ctl] = "epoll_ctl",
                                                    [SYS_epoll_pwait] = "epoll_pwait",
                                                    [SYS_dup] = "dup",
                                                    [SYS_dup3] = "dup3",
                                                    [SYS_fcntl] = "fcntl",
                                                    [SYS_inotify_init1] = "inotify_init1",
                                                    [SYS_inotify_add_watch] = "inotify_add_watch",
                                                    [SYS_inotify_rm_watch] = "inotify_rm_watch",
                                                    [SYS_ioctl] = "ioctl",
                                                    [SYS_ioprio_set] = "ioprio_set",
                                                    [SYS_ioprio_get] = "ioprio_get",
                                                    [SYS_flock] = "flock",
                                                    [SYS_mknodat] = "mknodat",
                                                    [SYS_mkdirat] = "mkdirat",
                                                    [SYS_unlinkat] = "unlinkat",
                                                    [SYS_symlinkat] = "symlinkat",
                                                    [SYS_linkat] = "linkat",
                                                    [SYS_renameat] = "renameat",
                                                    [SYS_umount2] = "umount2",
                                                    [SYS_mount] = "mount",
                                                    [SYS_pivot_root] = "pivot_root",
                                                    [SYS_nfsservctl] = "nfsservctl",
                                                    [SYS_statfs] = "statfs",
                                                    [SYS_fstatfs] = "fstatfs",
                                                    [SYS_truncate] = "truncate",
                                                    [SYS_ftruncate] = "ftruncate",
                                                    [SYS_fallocate] = "fallocate",
                                                    [SYS_faccessat] = "faccessat",
                                                    [SYS_chdir] = "chdir",
                                                    [SYS_fchdir] = "fchdir",
                                                    [SYS_chroot] = "chroot",
                                                    [SYS_fchmod] = "fchmod",
                                                    [SYS_fchmodat] = "fchmodat",
                                                    [SYS_fchownat] = "fchownat",
                                                    [SYS_fchown] = "fchown",
                                                    [SYS_openat] = "openat",
                                                    [SYS_close] = "close",
                                                    [SYS_vhangup] = "vhangup",
                                                    [SYS_pipe2] = "pipe2",
                                                    [SYS_quotactl] = "quotactl",
                                                    [SYS_getdents64] = "getdents64",
                                                    [SYS_lseek] = "lseek",
                                                    [SYS_read] = "read",
                                                    [SYS_write] = "write",
                                                    [SYS_readv] = "readv",
                                                    [SYS_writev] = "writev",
                                                    [SYS_pread64] = "pread64",
                                                    [SYS_pwrite64] = "pwrite64",
                                                    [SYS_preadv] = "preadv",
                                                    [SYS_pwritev] = "pwritev",
                                                    [SYS_sendfile] = "sendfile",
                                                    [SYS_pselect6] = "pselect6",
                                                    [SYS_ppoll] = "ppoll",
                                                    [SYS_signalfd4] = "signalfd4",
                                                    [SYS_vmsplice] = "vmsplice",
                                                    [SYS_splice] = "splice",
                                                    [SYS_tee] = "tee",
                                                    [SYS_readlinkat] = "readlinkat",
                                                    [SYS_fstatat] = "fstatat",
                                                    [SYS_fstat] = "fstat",
                                                    [SYS_sync] = "sync",
                                                    [SYS_fsync] = "fsync",
                                                    [SYS_fdatasync] = "fdatasync",
                                                    [SYS_sync_file_range] = "sync_file_range",
                                                    [SYS_timerfd_create] = "timerfd_create",
                                                    [SYS_timerfd_settime] = "timerfd_settime",
                                                    [SYS_timerfd_gettime] = "timerfd_gettime",
                                                    [SYS_utimensat] = "utimensat",
                                                    [SYS_acct] = "acct",
                                                    [SYS_capget] = "capget",
                                                    [SYS_capset] = "capset",
                                                    [SYS_personality] = "personality",
                                                    [SYS_exit] = "exit",
                                                    [SYS_exit_group] = "exit_group",
                                                    [SYS_waitid] = "waitid",
                                                    [SYS_set_tid_address] = "set_tid_address",
                                                    [SYS_unshare] = "unshare",
                                                    [SYS_futex] = "futex",
                                                    [SYS_set_robust_list] = "set_robust_list",
                                                    [SYS_get_robust_list] = "get_robust_list",
                                                    [SYS_nanosleep] = "nanosleep",
                                                    [SYS_getitimer] = "getitimer",
                                                    [SYS_setitimer] = "setitimer",
                                                    [SYS_kexec_load] = "kexec_load",
                                                    [SYS_init_module] = "init_module",
                                                    [SYS_delete_module] = "delete_module",
                                                    [SYS_timer_create] = "timer_create",
                                                    [SYS_timer_gettime] = "timer_gettime",
                                                    [SYS_timer_getoverrun] = "timer_getoverrun",
                                                    [SYS_timer_settime] = "timer_settime",
                                                    [SYS_timer_delete] = "timer_delete",
                                                    [SYS_clock_settime] = "clock_settime",
                                                    [SYS_clock_gettime] = "clock_gettime",
                                                    [SYS_clock_getres] = "clock_getres",
                                                    [SYS_clock_nanosleep] = "clock_nanosleep",
                                                    [SYS_syslog] = "syslog",
                                                    [SYS_ptrace] = "ptrace",
                                                    [SYS_sched_setparam] = "sched_setparam",
                                                    [SYS_sched_setscheduler] = "sched_setscheduler",
                                                    [SYS_sched_getscheduler] = "sched_getscheduler",
                                                    [SYS_sched_getparam] = "sched_getparam",
                                                    [SYS_sched_setaffinity] = "sched_setaffinity",
                                                    [SYS_sched_getaffinity] = "sched_getaffinity",
                                                    [SYS_sched_yield] = "sched_yield",
                                                    [SYS_sched_get_priority_max] = "sched_get_priority_max",
                                                    [SYS_sched_get_priority_min] = "sched_get_priority_min",
                                                    [SYS_sched_rr_get_interval] = "sched_rr_get_interval",
                                                    [SYS_restart_syscall] = "restart_syscall",
                                                    [SYS_kill] = "kill",
                                                    [SYS_tkill] = "tkill",
                                                    [SYS_tgkill] = "tgkill",
                                                    [SYS_sigaltstack] = "sigaltstack",
                                                    [SYS_rt_sigsuspend] = "rt_sigsuspend",
                                                    [SYS_rt_sigaction] = "rt_sigaction",
                                                    [SYS_rt_sigprocmask] = "rt_sigprocmask",
                                                    [SYS_rt_sigpending] = "rt_sigpending",
                                                    [SYS_rt_sigtimedwait] = "rt_sigtimedwait",
                                                    [SYS_rt_sigqueueinfo] = "rt_sigqueueinfo",
                                                    [SYS_rt_sigreturn] = "rt_sigreturn",
                                                    [SYS_setpriority] = "setpriority",
                                                    [SYS_getpriority] = "getpriority",
                                                    [SYS_reboot] = "reboot",
                                                    [SYS_setregid] = "setregid",
                                                    [SYS_setgid] = "setgid",
                                                    [SYS_setreuid] = "setreuid",
                                                    [SYS_setuid] = "setuid",
                                                    [SYS_setresuid] = "setresuid",
                                                    [SYS_getresuid] = "getresuid",
                                                    [SYS_setresgid] = "setresgid",
                                                    [SYS_getresgid] = "getresgid",
                                                    [SYS_setfsuid] = "setfsuid",
                                                    [SYS_setfsgid] = "setfsgid",
                                                    [SYS_times] = "times",
                                                    [SYS_setpgid] = "setpgid",
                                                    [SYS_getpgid] = "getpgid",
                                                    [SYS_getsid] = "getsid",
                                                    [SYS_setsid] = "setsid",
                                                    [SYS_getgroups] = "getgroups",
                                                    [SYS_setgroups] = "setgroups",
                                                    [SYS_uname] = "uname",
                                                    [SYS_sethostname] = "sethostname",
                                                    [SYS_setdomainname] = "setdomainname",
                                                    [SYS_getrlimit] = "getrlimit",
                                                    [SYS_setrlimit] = "setrlimit",
                                                    [SYS_getrusage] = "getrusage",
                                                    [SYS_umask] = "umask",
                                                    [SYS_prctl] = "prctl",
                                                    [SYS_getcpu] = "getcpu",
                                                    [SYS_gettimeofday] = "gettimeofday",
                                                    [SYS_settimeofday] = "settimeofday",
                                                    [SYS_adjtimex] = "adjtimex",
                                                    [SYS_getpid] = "getpid",
                                                    [SYS_getppid] = "getppid",
                                                    [SYS_getuid] = "getuid",
                                                    [SYS_geteuid] = "geteuid",
                                                    [SYS_getgid] = "getgid",
                                                    [SYS_getegid] = "getegid",
                                                    [SYS_gettid] = "gettid",
                                                    [SYS_sysinfo] = "sysinfo",
                                                    [SYS_mq_open] = "mq_open",
                                                    [SYS_mq_unlink] = "mq_unlink",
                                                    [SYS_mq_timedsend] = "mq_timedsend",
                                                    [SYS_mq_timedreceive] = "mq_timedreceive",
                                                    [SYS_mq_notify] = "mq_notify",
                                                    [SYS_mq_getsetattr] = "mq_getsetattr",
                                                    [SYS_msgget] = "msgget",
                                                    [SYS_msgctl] = "msgctl",
                                                    [SYS_msgrcv] = "msgrcv",
                                                    [SYS_msgsnd] = "msgsnd",
                                                    [SYS_semget] = "semget",
                                                    [SYS_semctl] = "semctl",
                                                    [SYS_semtimedop] = "semtimedop",
                                                    [SYS_semop] = "semop",
                                                    [SYS_shmget] = "shmget",
                                                    [SYS_shmctl] = "shmctl",
                                                    [SYS_shmat] = "shmat",
                                                    [SYS_shmdt] = "shmdt",
                                                    [SYS_socket] = "socket",
                                                    [SYS_socketpair] = "socketpair",
                                                    [SYS_bind] = "bind",
                                                    [SYS_listen] = "listen",
                                                    [SYS_accept] = "accept",
                                                    [SYS_connect] = "connect",
                                                    [SYS_getsockname] = "getsockname",
                                                    [SYS_getpeername] = "getpeername",
                                                    [SYS_sendto] = "sendto",
                                                    [SYS_recvfrom] = "recvfrom",
                                                    [SYS_setsockopt] = "setsockopt",
                                                    [SYS_getsockopt] = "getsockopt",
                                                    [SYS_shutdown] = "shutdown",
                                                    [SYS_sendmsg] = "sendmsg",
                                                    [SYS_recvmsg] = "recvmsg",
                                                    [SYS_readahead] = "readahead",
                                                    [SYS_brk] = "brk",
                                                    [SYS_munmap] = "munmap",
                                                    [SYS_mremap] = "mremap",
                                                    [SYS_add_key] = "add_key",
                                                    [SYS_request_key] = "request_key",
                                                    [SYS_keyctl] = "keyctl",
                                                    [SYS_clone] = "clone",
                                                    [SYS_execve] = "execve",
                                                    [SYS_mmap] = "mmap",
                                                    [SYS_fadvise64] = "fadvise64",
                                                    [SYS_swapon] = "swapon",
                                                    [SYS_swapoff] = "swapoff",
                                                    [SYS_mprotect] = "mprotect",
                                                    [SYS_msync] = "msync",
                                                    [SYS_mlock] = "mlock",
                                                    [SYS_munlock] = "munlock",
                                                    [SYS_mlockall] = "mlockall",
                                                    [SYS_munlockall] = "munlockall",
                                                    [SYS_mincore] = "mincore",
                                                    [SYS_madvise] = "madvise",
                                                    [SYS_remap_file_pages] = "remap_file_pages",
                                                    [SYS_mbind] = "mbind",
                                                    [SYS_get_mempolicy] = "get_mempolicy",
                                                    [SYS_set_mempolicy] = "set_mempolicy",
                                                    [SYS_migrate_pages] = "migrate_pages",
                                                    [SYS_move_pages] = "move_pages",
                                                    [SYS_rt_tgsigqueueinfo] = "rt_tgsigqueueinfo",
                                                    [SYS_perf_event_open] = "perf_event_open",
                                                    [SYS_accept4] = "accept4",
                                                    [SYS_recvmmsg] = "recvmmsg",
                                                    [SYS_arch_specific_syscall] = "arch_specific_syscall",
                                                    [SYS_wait4] = "wait4",
                                                    [SYS_renameat2] = "renameat2",
                                                    [SYS_seccomp] = "seccomp",
                                                    [SYS_getrandom] = "getrandom",
                                                    [SYS_memfd_create] = "memfd_create",
                                                    [SYS_bpf] = "bpf",
                                                    [SYS_execveat] = "execveat",
                                                    [SYS_userfaultfd] = "userfaultfd",
                                                    [SYS_membarrier] = "membarrier",
                                                    [SYS_mlock2] = "mlock2",
                                                    [SYS_copy_file_range] = "copy_file_range",
                                                    [SYS_preadv2] = "preadv2",
                                                    [SYS_pwritev2] = "pwritev2",
                                                    [SYS_pkey_mprotect] = "pkey_mprotect",
                                                    [SYS_pkey_alloc] = "pkey_alloc",
                                                    [SYS_pkey_free] = "pkey_free",
                                                    [SYS_statx] = "statx",
                                                    [SYS_io_pgetevents] = "io_pgetevents",
                                                    [SYS_rseq] = "rseq",
                                                    [SYS_kexec_file_load] = "kexec_file_load",
                                                    [SYS_pidfd_send_signal] = "pidfd_send_signal",
                                                    [SYS_io_uring_setup] = "io_uring_setup",
                                                    [SYS_io_uring_enter] = "io_uring_enter",
                                                    [SYS_io_uring_register] = "io_uring_register",
                                                    [SYS_open_tree] = "open_tree",
                                                    [SYS_move_mount] = "move_mount",
                                                    [SYS_fsopen] = "fsopen",
                                                    [SYS_fsconfig] = "fsconfig",
                                                    [SYS_fsmount] = "fsmount",
                                                    [SYS_fspick] = "fspick",
                                                    [SYS_pidfd_open] = "pidfd_open",
                                                    [SYS_clone3] = "clone3",
                                                    [SYS_close_range] = "close_range",
                                                    [SYS_openat2] = "openat2",
                                                    [SYS_pidfd_getfd] = "pidfd_getfd",
                                                    [SYS_faccessat2] = "faccessat2",
                                                    [SYS_process_madvise] = "process_madvise",
                                                    [SYS_epoll_pwait2] = "epoll_pwait2",
                                                    [SYS_mount_setattr] = "mount_setattr",
                                                    [SYS_quotactl_fd] = "quotactl_fd",
                                                    [SYS_landlock_create_ruleset] = "landlock_create_ruleset",
                                                    [SYS_landlock_add_rule] = "landlock_add_rule",
                                                    [SYS_landlock_restrict_self] = "landlock_restrict_self",
                                                    [SYS_memfd_secret] = "memfd_secret",
                                                    [SYS_process_mrelease] = "process_mrelease",
                                                    [SYS_futex_waitv] = "futex_waitv",
                                                    [SYS_set_mempolicy_home_node] = "set_mempolicy_home_node",
                                                    [SYS_cachestat] = "cachestat",
                                                    [SYS_fchmodat2] = "fchmodat2",
                                                    [SYS_map_shadow_stack] = "map_shadow_stack",
                                                    [SYS_futex_wake] = "futex_wake",
                                                    [SYS_futex_wait] = "futex_wait",
                                                    [SYS_futex_requeue] = "futex_requeue",
                                                    [SYS_statmount] = "statmount",
                                                    [SYS_listmount] = "listmount",
                                                    [SYS_lsm_get_self_attr] = "lsm_get_self_attr",
                                                    [SYS_lsm_set_self_attr] = "lsm_set_self_attr",
                                                    [SYS_lsm_list_modules] = "lsm_list_modules",
                                                    [SYS_mseal] = "mseal"};

// Syscall monitoring variables
static uint32_t syscall_count = 0;  // Total number of tracked syscalls
static uint32_t syscall_nr = 0;     // Current syscall number
static uint32_t syscall_pid = 0;    // PID making the syscall
static uint32_t syscall_cpu_id = 0; // CPU where the syscall occurred
static uint32_t syscall_rate = 0;   // Syscalls per second (calculated)
static uint64_t syscall_time = 0;   // Timestamp of last syscall

// Get syscall name from number
static const char *get_syscall_name(uint32_t syscall_nr) {
    if (syscall_nr < sizeof(syscall_names) / sizeof(syscall_names[0]) && syscall_names[syscall_nr] != NULL) {
        return syscall_names[syscall_nr];
    }

    return "unknown";
}

// Print syscall statistics from BPF map
static void print_syscall_stats(int map_fd) {
    printf("\nSyscall Statistics:\n");
    uint64_t total_syscalls = 0;

    // Read all syscall counters from the BPF map
    for (uint32_t syscall_nr = 0; syscall_nr < 463; syscall_nr++) {
        uint64_t count = 0;
        if (bpf_map_lookup_elem(map_fd, &syscall_nr, &count) == 0 && count > 0) {
            total_syscalls += count;
            const char *name = get_syscall_name(syscall_nr);
            printf("  %u: %s: %llu calls\n", syscall_nr, name, count);
        }
    }

    printf("Total syscalls captured: %llu\n", total_syscalls);
}

//-----------------------------------------------------------------------------------------------------
// BPF event structure

// Event structure must match the BPF program
#define EVENT_PROCESS_FORK 1
#define EVENT_SYSCALL 2

struct event {
    uint64_t timestamp;  // Precise kernel timestamp from bpf_ktime_get_ns()
    uint32_t event_type; // Type of event (fork, syscall)
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

    } data;
};

//-----------------------------------------------------------------------------------------------------
// Handle incoming events from BPF ring buffer

// Process monitoring variables
static uint32_t new_process_pid = 0; // Global variable to store new process PID

static struct bpf_object *obj = NULL;
static struct bpf_link *process_fork_link = NULL; // Link for process fork tracepoint
static struct bpf_link *syscall_link = NULL;      // Link for syscall tracepoint

static struct ring_buffer *rb = NULL; // BPF ring buffer for receiving events
static int map_fd = -1;               // BPF ring buffer map file descriptor
static int syscall_counters_fd = -1;  // BPF syscall counters map file descriptor

static int handle_event(void *ctx, void *data, size_t data_sz) {
    struct event *e = data;

    // Handle process fork events
    if (e->event_type == EVENT_PROCESS_FORK) {
        new_process_pid = e->data.fork.pid;

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Process created: PID=%u, PPID=%u, comm=%s, parent_comm=%s, CPU=%u, timestamp=%llu ns\n", e->data.fork.pid, e->data.fork.ppid,
                 e->data.fork.comm, e->data.fork.parent_comm, e->cpu_id, e->timestamp);
        XcpPrint(buffer);
        printf("%s\n", buffer);

        DaqEventAt(process_event, TO_XCP_TIMESTAMP(e->timestamp));

    }

    // Handle syscall events
    else if (e->event_type == EVENT_SYSCALL) {
        static uint64_t syscall_last_rate_calculation_time = 0;
        static uint64_t syscall_count_last_second = 0;

        syscall_nr = e->data.syscall.syscall_nr;
        syscall_pid = e->data.syscall.pid;
        syscall_cpu_id = e->cpu_id;
        syscall_time = e->timestamp;

        syscall_count++;
        if (syscall_nr < MAX_SYSCALL_NR) {
            syscall_event_counters[syscall_nr]++;
        }

        // Calculate syscall rate every second
        if (syscall_last_rate_calculation_time == 0) {
            syscall_last_rate_calculation_time = syscall_time;
            syscall_count_last_second = syscall_count;
        } else if ((syscall_time - syscall_last_rate_calculation_time) >= 1000000000ULL) { // 1 second in ns
            syscall_rate = syscall_count - syscall_count_last_second;
            syscall_count_last_second = syscall_count;
            syscall_last_rate_calculation_time = syscall_time;

            // Print syscall rate every second
            // printf("Tracked syscalls/sec: %u (Total: %u)\n", syscall_rate, syscall_count);

            // Show overall syscall statistics every 10 seconds
            // static uint32_t stats_counter = 0;
            // stats_counter++;
            // if (stats_counter >= 10) {
            //     print_syscall_stats(syscall_counters_fd);
            //     stats_counter = 0;
            // }
        }

        // Optional: Print detailed syscall info (comment out for less verbose output)
        const char *syscall_name = get_syscall_name(syscall_nr);
        printf("Syscall: %s [%u] called %s (%u) on CPU%u\n", e->data.syscall.comm, e->data.syscall.pid, syscall_name, syscall_nr, syscall_cpu_id);

        DaqEventAt(syscall_event, TO_XCP_TIMESTAMP(e->timestamp));
    }

    return 0;
}

//-----------------------------------------------------------------------------------------------------
// Initialize and load BPF program

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
    if (obj) {
        bpf_object__close(obj);
        obj = NULL;
    }
}

//-----------------------------------------------------------------------------------------------------
// Main

// Signal handler for clean shutdown
static volatile bool running = true;
static void sig_handler(int sig) { running = false; }

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

    uint32_t counter = 0;

    // Create events for DAQ (Data Acquisition)
    DaqCreateEvent(mainloop_event);
    DaqCreateEvent(process_event);
    DaqCreateEvent(syscall_event);

    // Register statistics measurement variables (mainloop every 100ms  )
    A2lSetStackAddrMode(mainloop_event);
    A2lCreateMeasurement(counter, "Mainloop counter value"); // Mainloop counter
    A2lSetAbsoluteAddrMode(mainloop_event);
    A2lCreateMeasurement(syscall_count, "Total tracked syscalls count");                             // Total syscall count
    A2lCreatePhysMeasurement(syscall_rate, "Total tracked syscalls per second", "1/s", 0.0, 2000.0); // Total syscall rate

    // New process PID creation event monitoring (BPF event)
    A2lSetAbsoluteAddrMode(process_event);
    A2lCreateMeasurement(new_process_pid, "New process PID");

    // Syscall event monitoring  (BPF event)
    A2lSetAbsoluteAddrMode(syscall_event);
    A2lCreateMeasurement(syscall_nr, "Current syscall number");     // Current syscall number
    A2lCreateMeasurement(syscall_pid, "Syscall PID");               // PID making the syscall
    for (uint32_t syscall_nr = 0; syscall_nr < 463; syscall_nr++) { // Individual measurement variables for each syscall
        const char *name = get_syscall_name(syscall_nr);
        if (name && strcmp(name, "unknown") != 0) {
            // Using the A2L generation function directly to create variables with dynamic names
            A2lCreateMeasurement_(NULL, name, A2L_TYPE_UINT32, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(syscall_event_counters[syscall_nr])), NULL, 0.0, 0.0, "");
        }
    }

    A2lFinalize(); // Finalize A2L file now, do not wait for XCP connect

    // Start main loop
    printf("Start main loop...\n");
    while (running) {

        // Update counter
        counter++;

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
