# BPF Demo for XCPlite

This demo shows how to integrate eBPF (Extended Berkeley Packet Filter) with XCPlite to monitor system events and trigger XCP measurements.

## Overview

The BPF demo captures process creation events using eBPF and exposes the process IDs through XCP for real-time monitoring. When a new process is created on the system, the BPF program captures the event and triggers an XCP measurement event.

## Features

- **Process Monitoring**: Uses eBPF to capture `sched_process_fork` tracepoint events
- **Real-time Data**: Process IDs are stored in a global variable accessible via XCP
- **Cross-platform**: Compiles on both Linux (with BPF support) and other platforms (without BPF)
- **XCP Integration**: Triggers dedicated XCP events when new processes are detected

## Prerequisites

### Linux (for full BPF functionality)

```bash
# Ubuntu/Debian
sudo apt-get install clang llvm libbpf-dev linux-headers-$(uname -r)

# RHEL/CentOS/Fedora
sudo dnf install clang llvm libbpf-devel kernel-headers

# Arch Linux
sudo pacman -S clang llvm libbpf linux-headers
```

## Building

1. **Build the BPF program (Linux only)**:

   ```bash
   cd examples/bpf_demo
   ./build_bpf.sh
   ```

2. **Build the main application**:

   ```bash
   # From project root
   cmake -B build -S .
   cmake --build build --target bpf_demo
   ```

## Running

### On Linux with BPF support

```bash
# Root privileges required for BPF programs
sudo ./build/bpf_demo
```

## Troubleshooting

### "Permission denied" when running

- BPF programs require root privileges on Linux
- Use `sudo` to run the demo

### "Failed to load BPF object"

- Ensure BPF development tools are installed
- Check that the BPF program was built successfully
- Verify kernel supports BPF (kernel 4.1+)

### No BPF events detected

- BPF functionality only works on Linux
- Check if `process_monitor.bpf.o` exists in the build directory
- Ensure the program is running with root privileges

## Files

- `main.c`: Main application with XCP and BPF integration
- `process_monitor.bpf.c`: BPF kernel program for process monitoring
- `Makefile`: Build system for BPF program
- `build_bpf.sh`: Helper script to build BPF components
- `README.md`: This documentation file

## Technical Details

The BPF program attaches to the `sched/sched_process_fork` tracepoint, which is triggered whenever a new process is created. The event data includes:

- Process ID (PID) of the new process
- Parent Process ID (PPID)
- Command name (comm)

This data is passed to userspace via a BPF ring buffer and processed by the main application, which updates the global `new_process_pid` variable and triggers an XCP measurement event.

# Raspberry Pi

```bash
sudo bpftrace --info


System
  OS: Linux 6.12.34+rpt-rpi-2712 #1 SMP PREEMPT Debian 1:6.12.34-1+rpt1~bookworm (2025-06-26)
  Arch: aarch64

Build
  version: v0.17.0
  LLVM: 14.0.6
  unsafe uprobe: no
  bfd: no
  libdw (DWARF support): yes

Kernel helpers
  probe_read: yes
  probe_read_str: yes
  probe_read_user: yes
  probe_read_user_str: yes
  probe_read_kernel: yes
  probe_read_kernel_str: yes
  get_current_cgroup_id: yes
  send_signal: yes
  override_return: yes
  get_boot_ns: yes
  dpath: no
  skboutput: no

Kernel features
  Instruction limit: 1000000
  Loop support: yes
  btf: no
  map batch: yes
  uprobe refcount (depends on Build:bcc bpf_attach_uprobe refcount): no

Map types
  hash: yes
  percpu hash: yes
  array: yes
  percpu array: yes
  stack_trace: yes
  perf_event_array: yes

Probe types
  kprobe: yes
  tracepoint: yes
  perf_event: yes
  kfunc: no
  iter:task: no
  iter:task_file: no
  kprobe_multi: no
  raw_tp_special: yes

```
