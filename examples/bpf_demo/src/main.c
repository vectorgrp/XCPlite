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
// ARM64 Syscall Number Constants
// Based on Linux kernel arch/arm64/include/asm/unistd.h and include/uapi/asm-generic/unistd.h

// I/O Operations
#define SYS_io_setup 0
#define SYS_io_destroy 1
#define SYS_io_submit 2
#define SYS_io_cancel 3
#define SYS_io_getevents 4

// Extended Attributes
#define SYS_setxattr 5
#define SYS_lsetxattr 6
#define SYS_fsetxattr 7
#define SYS_getxattr 8
#define SYS_lgetxattr 9
#define SYS_fgetxattr 10
#define SYS_listxattr 11
#define SYS_llistxattr 12
#define SYS_flistxattr 13
#define SYS_removexattr 14
#define SYS_lremovexattr 15
#define SYS_fremovexattr 16

// File System Operations
#define SYS_getcwd 17
#define SYS_lookup_dcookie 18
#define SYS_eventfd2 19
#define SYS_epoll_create1 20
#define SYS_epoll_ctl 21
#define SYS_epoll_pwait 22
#define SYS_dup 23
#define SYS_dup3 24
#define SYS_fcntl 25
#define SYS_inotify_init1 26
#define SYS_inotify_add_watch 27
#define SYS_inotify_rm_watch 28
#define SYS_ioctl 29
#define SYS_ioprio_set 30
#define SYS_ioprio_get 31
#define SYS_flock 32
#define SYS_mknodat 33
#define SYS_mkdirat 34
#define SYS_unlinkat 35
#define SYS_symlinkat 36
#define SYS_linkat 37
#define SYS_renameat 38
#define SYS_umount2 39
#define SYS_mount 40
#define SYS_pivot_root 41
#define SYS_nfsservctl 42
#define SYS_statfs 43
#define SYS_fstatfs 44
#define SYS_truncate 45
#define SYS_ftruncate 46
#define SYS_fallocate 47
#define SYS_faccessat 48
#define SYS_chdir 49
#define SYS_fchdir 50
#define SYS_chroot 51
#define SYS_fchmod 52
#define SYS_fchmodat 53
#define SYS_fchownat 54
#define SYS_fchown 55
#define SYS_openat 56
#define SYS_close 57
#define SYS_vhangup 58
#define SYS_pipe2 59
#define SYS_quotactl 60
#define SYS_getdents64 61
#define SYS_lseek 62
#define SYS_read 63
#define SYS_write 64
#define SYS_readv 65
#define SYS_writev 66
#define SYS_pread64 67
#define SYS_pwrite64 68
#define SYS_preadv 69
#define SYS_pwritev 70
#define SYS_sendfile 71
#define SYS_pselect6 72
#define SYS_ppoll 73
#define SYS_signalfd4 74
#define SYS_vmsplice 75
#define SYS_splice 76
#define SYS_tee 77
#define SYS_readlinkat 78
#define SYS_fstatat 79
#define SYS_fstat 80
#define SYS_sync 81
#define SYS_fsync 82
#define SYS_fdatasync 83
#define SYS_sync_file_range 84

// Timer Operations
#define SYS_timerfd_create 85
#define SYS_timerfd_settime 86
#define SYS_timerfd_gettime 87
#define SYS_utimensat 88
#define SYS_acct 89

// Process Management
#define SYS_capget 90
#define SYS_capset 91
#define SYS_personality 92
#define SYS_exit 93
#define SYS_exit_group 94
#define SYS_waitid 95
#define SYS_set_tid_address 96
#define SYS_unshare 97
#define SYS_futex 98
#define SYS_set_robust_list 99
#define SYS_get_robust_list 100

// Time and Sleep Operations
#define SYS_nanosleep 101 // Note: ARM64 has nanosleep at both 101 and 115
#define SYS_getitimer 102
#define SYS_setitimer 103
#define SYS_kexec_load 104
#define SYS_init_module 105
#define SYS_delete_module 106
#define SYS_timer_create 107
#define SYS_timer_gettime 108
#define SYS_timer_getoverrun 109
#define SYS_timer_settime 110
#define SYS_timer_delete 111
#define SYS_clock_settime 112
#define SYS_clock_gettime 113
#define SYS_clock_getres 114
#define SYS_clock_nanosleep 115 // This is the main nanosleep syscall on ARM64
#define SYS_syslog 116

// Process and Thread Management
#define SYS_ptrace 117
#define SYS_sched_setparam 118
#define SYS_sched_setscheduler 119
#define SYS_sched_getscheduler 120
#define SYS_sched_getparam 121
#define SYS_sched_setaffinity 122
#define SYS_sched_getaffinity 123
#define SYS_sched_yield 124
#define SYS_sched_get_priority_max 125
#define SYS_sched_get_priority_min 126
#define SYS_sched_rr_get_interval 127
#define SYS_restart_syscall 128
#define SYS_kill 129
#define SYS_tkill 130
#define SYS_tgkill 131

// Signal Handling
#define SYS_sigaltstack 132
#define SYS_rt_sigsuspend 133
#define SYS_rt_sigaction 134
#define SYS_rt_sigprocmask 135
#define SYS_rt_sigpending 136
#define SYS_rt_sigtimedwait 137
#define SYS_rt_sigqueueinfo 138
#define SYS_rt_sigreturn 139

// User/Group Management
#define SYS_setpriority 140
#define SYS_getpriority 141
#define SYS_reboot 142
#define SYS_setregid 143
#define SYS_setgid 144
#define SYS_setreuid 145
#define SYS_setuid 146
#define SYS_setresuid 147
#define SYS_getresuid 148
#define SYS_setresgid 149
#define SYS_getresgid 150
#define SYS_setfsuid 151
#define SYS_setfsgid 152
#define SYS_times 153
#define SYS_setpgid 154
#define SYS_getpgid 155
#define SYS_getsid 156
#define SYS_setsid 157
#define SYS_getgroups 158
#define SYS_setgroups 159

// System Information
#define SYS_uname 160
#define SYS_sethostname 161
#define SYS_setdomainname 162
#define SYS_getrlimit 163
#define SYS_setrlimit 164
#define SYS_getrusage 165
#define SYS_umask 166
#define SYS_prctl 167
#define SYS_getcpu 168
#define SYS_gettimeofday 169
#define SYS_settimeofday 170
#define SYS_adjtimex 171
#define SYS_getpid 172
#define SYS_getppid 173
#define SYS_getuid 174
#define SYS_geteuid 175
#define SYS_getgid 176
#define SYS_getegid 177
#define SYS_gettid 178
#define SYS_sysinfo 179

// Message Queues
#define SYS_mq_open 180
#define SYS_mq_unlink 181
#define SYS_mq_timedsend 182
#define SYS_mq_timedreceive 183
#define SYS_mq_notify 184
#define SYS_mq_getsetattr 185

// System V IPC
#define SYS_msgget 186
#define SYS_msgctl 187
#define SYS_msgrcv 188
#define SYS_msgsnd 189
#define SYS_semget 190
#define SYS_semctl 191
#define SYS_semtimedop 192
#define SYS_semop 193
#define SYS_shmget 194
#define SYS_shmctl 195
#define SYS_shmat 196
#define SYS_shmdt 197

// Network Operations
#define SYS_socket 198
#define SYS_socketpair 199
#define SYS_bind 200
#define SYS_listen 201
#define SYS_accept 202
#define SYS_connect 203
#define SYS_getsockname 204
#define SYS_getpeername 205
#define SYS_sendto 206
#define SYS_recvfrom 207
#define SYS_setsockopt 208
#define SYS_getsockopt 209
#define SYS_shutdown 210
#define SYS_sendmsg 211
#define SYS_recvmsg 212

// Memory Management
#define SYS_readahead 213
#define SYS_brk 214
#define SYS_munmap 215
#define SYS_mremap 216
#define SYS_add_key 217
#define SYS_request_key 218
#define SYS_keyctl 219
#define SYS_clone 220
#define SYS_execve 221
#define SYS_mmap 222
#define SYS_fadvise64 223
#define SYS_swapon 224
#define SYS_swapoff 225
#define SYS_mprotect 226
#define SYS_msync 227
#define SYS_mlock 228
#define SYS_munlock 229
#define SYS_mlockall 230
#define SYS_munlockall 231
#define SYS_mincore 232
#define SYS_madvise 233
#define SYS_remap_file_pages 234
#define SYS_mbind 235
#define SYS_get_mempolicy 236
#define SYS_set_mempolicy 237
#define SYS_migrate_pages 238
#define SYS_move_pages 239

// Advanced Operations
#define SYS_rt_tgsigqueueinfo 240
#define SYS_perf_event_open 241
#define SYS_accept4 242
#define SYS_recvmmsg 243
#define SYS_arch_specific_syscall 244
#define SYS_wait4 260

// Recent Syscalls
#define SYS_renameat2 276
#define SYS_seccomp 277
#define SYS_getrandom 278
#define SYS_memfd_create 279
#define SYS_bpf 280
#define SYS_execveat 281
#define SYS_userfaultfd 282
#define SYS_membarrier 283
#define SYS_mlock2 284
#define SYS_copy_file_range 285
#define SYS_preadv2 286
#define SYS_pwritev2 287
#define SYS_pkey_mprotect 288
#define SYS_pkey_alloc 289
#define SYS_pkey_free 290
#define SYS_statx 291
#define SYS_io_pgetevents 292
#define SYS_rseq 293
#define SYS_kexec_file_load 294

// Modern Syscalls (400+)
#define SYS_pidfd_send_signal 424
#define SYS_io_uring_setup 425
#define SYS_io_uring_enter 426
#define SYS_io_uring_register 427
#define SYS_open_tree 428
#define SYS_move_mount 429
#define SYS_fsopen 430
#define SYS_fsconfig 431
#define SYS_fsmount 432
#define SYS_fspick 433
#define SYS_pidfd_open 434
#define SYS_clone3 435
#define SYS_close_range 436
#define SYS_openat2 437
#define SYS_pidfd_getfd 438
#define SYS_faccessat2 439
#define SYS_process_madvise 440
#define SYS_epoll_pwait2 441
#define SYS_mount_setattr 442
#define SYS_quotactl_fd 443
#define SYS_landlock_create_ruleset 444
#define SYS_landlock_add_rule 445
#define SYS_landlock_restrict_self 446
#define SYS_memfd_secret 447
#define SYS_process_mrelease 448
#define SYS_futex_waitv 449
#define SYS_set_mempolicy_home_node 450
#define SYS_cachestat 451
#define SYS_fchmodat2 452
#define SYS_map_shadow_stack 453
#define SYS_futex_wake 454
#define SYS_futex_wait 455
#define SYS_futex_requeue 456
#define SYS_statmount 457
#define SYS_listmount 458
#define SYS_lsm_get_self_attr 459
#define SYS_lsm_set_self_attr 460
#define SYS_lsm_list_modules 461
#define SYS_mseal 462

// Legacy compatibility defines (from BPF file)
#define SYSCALL_FUTEX SYS_futex
#define SYSCALL_EXIT SYS_exit
#define SYSCALL_CLOCK_GETTIME SYS_clock_gettime
#define SYSCALL_NANOSLEEP SYS_clock_nanosleep // ARM64 uses 115 for nanosleep
#define SYSCALL_SCHED_SETSCHEDULER SYS_sched_setscheduler
#define SYSCALL_SCHED_YIELD SYS_sched_yield
#define SYSCALL_BRK SYS_brk
#define SYSCALL_MUNMAP SYS_munmap
#define SYSCALL_CLONE SYS_clone
#define SYSCALL_MMAP SYS_mmap
#define SYSCALL_MPROTECT SYS_mprotect
#define SYSCALL_WAIT4 SYS_wait4
#define SYSCALL_PIPE2 SYS_pipe2

//-----------------------------------------------------------------------------------------------------
// XCP params

#define OPTION_PROJECT_NAME "bpf_demo"  // Project name, used to build the A2L and BIN file name
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 16     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

#define TO_XCP_TIMESTAMP(t) (t / 1000) // Convert to XCP timestamp in microseconds (OPTION_CLOCK_TICKS_1US)
// #define TO_XCP_TIMESTAMP(t) (t)        // Convert to XCP timestamp in nanoseconds (OPTION_CLOCK_TICKS_1NS)

//-----------------------------------------------------------------------------------------------------
// Global variables

// Syscall monitoring variables
static uint32_t syscall_count = 0;          // Total number of tracked syscalls
static uint32_t current_syscall_nr = 0;     // Current syscall number
static uint32_t current_syscall_pid = 0;    // PID making the syscall
static uint32_t current_syscall_cpu_id = 0; // CPU where the syscall occurred
static uint32_t syscall_rate = 0;           // Syscalls per second (calculated)
static uint64_t last_syscall_time = 0;      // Timestamp of last syscall

// BPF event structure
// Event structure that matches the BPF program
#define EVENT_PROCESS_FORK 1
#define EVENT_SYSCALL 2
#define EVENT_TIMER_TICK 3

// ARM64 syscall lookup table (major syscalls 0-462) - using constants for clarity
static const char *arm64_syscall_names[] = {[SYS_io_setup] = "io_setup",
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
                                            [SYS_clock_nanosleep] = "nanosleep",
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

static const char *get_syscall_name(uint32_t syscall_nr) {

    if (syscall_nr < sizeof(arm64_syscall_names) / sizeof(arm64_syscall_names[0]) && arm64_syscall_names[syscall_nr] != NULL) {
        return arm64_syscall_names[syscall_nr];
    }

    return "unknown";
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

static uint16_t static_counter = 0; // Local counter variable for measurement

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
        double percentage = (double)top_syscalls[i] / total_syscalls * 100.0;
        printf("  %2d. %s(%u): %llu calls (%.1f%%)\n", i + 1, name, top_numbers[i], top_syscalls[i], percentage);
    }
    printf("\n");
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    struct event *e = data;
    static uint64_t last_rate_calculation_time = 0;
    static uint64_t syscall_count_last_second = 0;
    static uint64_t timer_count_last_second = 0;

    // Handle process fork events
    if (e->event_type == EVENT_PROCESS_FORK) {
        new_process_pid = e->data.fork.pid;

        printf("Process created: PID=%u, PPID=%u, comm=%s, parent_comm=%s, CPU=%u, timestamp=%llu ns\n", e->data.fork.pid, e->data.fork.ppid, e->data.fork.comm,
               e->data.fork.parent_comm, e->cpu_id, e->timestamp);

        DaqEventAt(process_event, TO_XCP_TIMESTAMP(e->timestamp));

    }

    // Handle syscall events
    else if (e->event_type == EVENT_SYSCALL) {
        syscall_count++;
        current_syscall_nr = e->data.syscall.syscall_nr;
        current_syscall_pid = e->data.syscall.pid;
        current_syscall_cpu_id = e->cpu_id;
        last_syscall_time = e->timestamp;

        // Calculate syscall rate every second
        uint64_t current_time_ns = e->timestamp;
        if (last_rate_calculation_time == 0) {
            last_rate_calculation_time = current_time_ns;
            syscall_count_last_second = syscall_count;
        } else if ((current_time_ns - last_rate_calculation_time) >= 1000000000ULL) { // 1 second in ns
            syscall_rate = syscall_count - syscall_count_last_second;
            syscall_count_last_second = syscall_count;
            last_rate_calculation_time = current_time_ns;

            // Print syscall rate every second
            printf("Tracked syscalls/sec: %u (Total: %u)\n", syscall_rate, syscall_count);

            // Show overall syscall statistics every 10 seconds
            static uint32_t stats_counter = 0;
            stats_counter++;
            if (stats_counter >= 10) {
                print_all_syscall_stats(syscall_counters_fd);
                stats_counter = 0;
            }
        }

        // Optional: Print detailed syscall info (comment out for less verbose output)
        if (current_syscall_nr != SYS_clock_nanosleep) {
            const char *syscall_name = get_syscall_name(current_syscall_nr);
            printf("Syscall: %s [%u] called %s (%u) on CPU%u\n", e->data.syscall.comm, e->data.syscall.pid, syscall_name, current_syscall_nr, current_syscall_cpu_id);
        }

        DaqEventAt(syscall_event, TO_XCP_TIMESTAMP(e->timestamp));

    }

    // Handle timer tick events (IRQ/softirq activity)
    else if (e->event_type == EVENT_TIMER_TICK) {
        timer_tick_count++;
        current_softirq_type = e->data.timer.softirq_type;
        current_irq_vec = e->data.timer.irq_vec;
        last_timer_tick_time = e->timestamp;

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

        DaqEventAt(syscall_event, TO_XCP_TIMESTAMP(e->timestamp));
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
    DaqCreateEvent(syscall_event);

    // Register statistics measurement variables (mainloop every 100ms  )
    A2lSetAbsoluteAddrMode(mainloop_event);
    A2lCreateMeasurement(static_counter, "Mainloop counter value");          // Mainloop counter
    A2lCreateMeasurement(syscall_count, "Total tracked syscalls count");     // Total syscall count
    A2lCreateMeasurement(syscall_rate, "Total tracked syscalls per second"); // Total syscall rate
    A2lCreateMeasurement(timer_tick_count, "Total timer ticks");             // Total timer tick count
    A2lCreateMeasurement(timer_tick_rate, "Timer ticks per second");         // Timer tick rate

    // New process PID creation event monitoring (BPF event)
    A2lSetAbsoluteAddrMode(process_event);
    A2lCreateMeasurement(new_process_pid, "New process PID");

    // Syscall event monitoring  (BPF event)
    A2lSetAbsoluteAddrMode(syscall_event);
    A2lCreateMeasurement(current_syscall_nr, "Current syscall number"); // Current syscall number
    A2lCreateMeasurement(current_syscall_pid, "Syscall PID");           // PID making the syscall

    // Timer tick event monitoring  (BPF event)
    A2lCreateMeasurement(current_softirq_type, "Current softirq type"); // Current softirq type
    A2lCreateMeasurement(current_irq_vec, "Current IRQ vector");        // Current IRQ vector

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
