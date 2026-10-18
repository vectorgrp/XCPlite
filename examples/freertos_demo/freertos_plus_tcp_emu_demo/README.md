# FreeRTOS-Plus-TCP Emulator Demo

This example demonstrates running **XCPlite over FreeRTOS-Plus-TCP on Linux**, without requiring an embedded board.

It uses the FreeRTOS kernel (GCC/POSIX port) and FreeRTOS-Plus-TCP network stack. The dependencies are pinned to FreeRTOS V11.3.0 and FreeRTOS-Plus-TCP V4.4.1 (202604.00-LTS).

The demo reuses the existing [`xcp_demo.c`](../xcp_demo.c) application, providing periodic measurement tasks, counters, a sine wave, and calibration parameters through XCP.

Two network modes are available:

- **POSIX:** Uses a Linux Ethernet interface through libpcap. External XCP clients can connect.
- **LOOPBACK:** Keeps network traffic inside FreeRTOS-Plus-TCP. Useful for automated testing without network access.

## 1. Build and Run

**Requirements:** Linux, CMake 3.24+, a C/C++ compiler, and libpcap development files (`libpcap-dev` on Ubuntu/Debian).

The first CMake configuration automatically downloads the pinned FreeRTOS dependencies.

From this directory, build the demo:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DFREERTOS_PLUS_TCP_NETWORK_IF=POSIX
cmake --build build --parallel
```

Run it with an unused IP address on your Ethernet network:

```bash
sudo ./build/freertos_plus_tcp_emu_demo 192.168.0.208
```

The demo acts as a **separate network device**, using:

| Setting | Default |
|---|---|
| IP address | Supplied when starting the demo |
| Subnet mask | `255.255.255.0` |
| Gateway | `0.0.0.0` |
| MAC address | `02:00:00:00:00:01` |
| XCP transport | UDP, port `5555` |

The IP address must be different from your Linux host's IP. Run one instance per Ethernet network with the supplied MAC address. Running multiple instances requires changing `mac` in `src/main.c` to a unique address for each instance.

You can optionally specify the subnet mask and gateway:

```bash
sudo ./build/freertos_plus_tcp_emu_demo 192.168.0.208 255.255.255.0 192.168.0.1
```

**Selecting the Ethernet interface**

The libpcap driver lists available interfaces at startup. If necessary, select a different interface using its one-based index:

```bash
cmake -S . -B build -DDEMO_NETWORK_INTERFACE=2
cmake --build build --parallel
```

Packet capture requires appropriate permissions, which is why the example uses `sudo`. A wired Ethernet connection is recommended; Wi-Fi and NAT-only virtual networks may not support the required packet injection.

## 2. Generate the A2L File

An **A2L file** describes the measurements, calibration parameters, memory addresses, and DAQ events exposed by the XCP server. XCP measurement tools use it to interpret the device's data.

Generate the A2L from the same executable you intend to run:

```bash
./create_a2l.sh build/freertos_plus_tcp_emu_demo 192.168.0.208
```

The script uses XCPlite's `xcpclient` to generate `CANape/freertos_demo.a2l` offline. The server does not need to be running.

The executable must contain DWARF debug information. If `xcpclient` is not on your `PATH`, specify its location using `XCPCLIENT=/path/to/xcpclient`.

See the [`xcpclient` build instructions](../../../tools/xcpclient/README.md) to build the tool.

Regenerate the A2L after rebuilding because memory addresses and event IDs may change.

**Connecting with CANape**

The [`CANape/`](CANape/) directory contains an optional project template. Generate the A2L, open the project in CANape, and configure the XCP destination using your demo's IP address and UDP port `5555`.

CANape is not required to run or test XCPlite. See the [CANape README](CANape/README.md) for details.

## 3. Automated Tests and Loopback Mode

Run the included integration tests:

```bash
ctest --test-dir build --output-on-failure
```

The tests use an internal FreeRTOS-Plus-TCP loopback endpoint in separate processes. They don't require Ethernet access or elevated permissions and cover socket operations, XCP communication, DAQ, calibration, and fault handling.

You can also build the demo entirely in loopback mode:

```bash
cmake -S . -B build-loopback -DCMAKE_BUILD_TYPE=Debug \
    -DFREERTOS_PLUS_TCP_NETWORK_IF=LOOPBACK

cmake --build build-loopback --parallel
ctest --test-dir build-loopback --output-on-failure
./build-loopback/freertos_plus_tcp_emu_demo
```

In this mode, `127.0.0.1` belongs to FreeRTOS-Plus-TCP's internal network, **not the Linux host**. External XCP clients cannot connect to it.

## 4. Configuration and Notes

Useful CMake options:

| Option | Description |
|---|---|
| `DEMO_BUILD_TESTS=OFF` | Build the demo without integration tests |
| `DEMO_STATIC_ALLOCATION=OFF` | Use dynamic allocation for XCPlite FreeRTOS tasks and mutexes |
| `DEMO_TICK_RATE=100` | Use a 100 Hz FreeRTOS tick instead of the default 1000 Hz |
| `FREERTOS_PLUS_TCP_NETWORK_IF=LOOPBACK` | Use internal networking instead of libpcap |

Local FreeRTOS source checkouts can be selected with `FETCHCONTENT_SOURCE_DIR_FREERTOS_KERNEL` and `FETCHCONTENT_SOURCE_DIR_FREERTOS_PLUS_TCP`.

If networking fails on a virtual Ethernet interface, checksum offloading may cause libpcap to capture incomplete checksums. Disabling transmit checksum offload on the affected virtual interface may help.

The demo runs until the process is terminated. Pressing Ctrl+C ends the process; graceful XCPlite shutdown and in-process restart are not demonstrated. Timing also uses the existing FreeRTOS tick-based clock rather than a high-resolution hardware timer.

For additional configuration details and embedded target limitations, see [FreeRTOS-Plus-TCP Integration](../../../docs/FREERTOS_PLUS_TCP.md).
