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

### Other platforms

The demo will compile and run without BPF support, showing only the basic XCP functionality.

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
sudo ./build/bpf_demo.out
```

### On other platforms or without BPF

```bash
./build/bpf_demo.out
```

## XCP Measurements

The demo exposes the following measurements via XCP:

- `static_counter`: Incremental counter
- `static_struct`: Demo structure with byte and word fields
- `new_process_pid`: PID of the most recently created process (Linux only)
- `loop_counter`: Local loop iteration counter

## XCP Events

- `mainloop_event`: Triggered every loop iteration (1ms)
- `process_event`: Triggered when a new process is detected (Linux only)

## Monitoring with CANape

1. Connect to the XCP server (default: UDP port 5555)
2. Load the generated A2L file (`bpf_demo.a2l`)
3. Create measurements for the available variables
4. Monitor `new_process_pid` to see process creation events in real-time

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
