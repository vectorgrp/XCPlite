# FreeRTOS-Plus-TCP UDP support

XCPlite supports IPv4 unicast UDP with FreeRTOS-Plus-TCP V4.4.1. You can select it
as an alternative to lwIP, which remains the default FreeRTOS network stack. Both
backends are implemented in `src/sockets.c`.

Your application sets up the network stack, driver, interfaces, endpoints, IP
addresses, and network-event hooks.

For a working example and setup instructions, start with the
[Linux FreeRTOS-Plus-TCP emulator demo](../examples/freertos_demo/freertos_plus_tcp_emu_demo/README.md).

This backend uses single-core FreeRTOS, XCPlite's existing RX/TX tasks, and
`OPTION_QUEUE_32`.

**Limitations:**

- FreeRTOS SMP is unsupported; single-core configurations only.
- TCP, IPv6, multicast, and raw Ethernet are unsupported by this backend.
- Local-address discovery and hardware timestamps are unsupported.

## Integration

In your parent CMake project, create and configure the `freertos_kernel` and
`freertos_plus_tcp` targets first. Provide `FreeRTOSConfig.h`,
`FreeRTOSIPConfig.h`, and their required include paths and compiler definitions.
Then add XCPlite:

```cmake
set(XCPLITE_CONFIGURATION rtos CACHE STRING "" FORCE)
set(XCPLITE_FREERTOS_PLUS_TCP ON CACHE BOOL "" FORCE)
set(XCPLITE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/XCPlite)
target_link_libraries(firmware PRIVATE xcplite::xcplite)
```

This integration requires `XCPLITE_INSTALL=OFF`: XCPlite uses your application's
FreeRTOS targets as a source subproject and does not package those dependencies.

Linking against XCPlite automatically applies the backend compiler definitions
and dependency settings to your application. Your project supplies the
dependencies; XCPlite does not download them. The standalone
[emulator demo](../examples/freertos_demo/freertos_plus_tcp_emu_demo/) shows this
setup using FreeRTOS V11.3.0 and FreeRTOS-Plus-TCP V4.4.1. It downloads these
pinned versions unless you provide local source directories.

To customize the configuration, set `XCPLITE_CFG_OVERRIDE` to your own header.
Include `xcplib_rtos_cfg.h` first, then adjust its defaults with `#undef` and
`#define`. Keep `XCPLITE_CONFIGURATION=rtos` so CMake uses the RTOS dependency rules.

If you build without CMake, include `src/sockets.c` in the usual XCPlite FreeRTOS
source set and define `_FREE_RTOS`, `OPTION_FREERTOS_PLUS_TCP`, and
`XCPLIB_CFG_OVERRIDE="xcplib_rtos_cfg.h"`. Select exactly one network stack for
native FreeRTOS builds. Defining `FREE_RTOS_POSIX_SIM` selects host sockets.

## Configuration and startup

- Enable `ipconfigUSE_IPv4`. You can disable TCP support in the stack.
- Set `OPTION_MTU` no higher than `ipconfigNETWORK_MTU` or the actual link MTU.
  The maximum DTO plus its four-byte transport header must fit the configured segment.
- Pass zero for the server queue-size argument; the RTOS queue uses static storage.
- Task creation uses the existing `create_thread()` macro, with its static/dynamic
  allocation choice and assertion-based failure handling. The backend adds no
  task-state queries or worker-completion synchronization.
- FreeRTOS-Plus-TCP V4.4.1 needs dynamic allocation for socket event groups, even
  when XCPlite's tasks and mutexes use static storage. This stack version cannot
  run without a heap.

Initialize XCPlite once, from an application task, after the scheduler and IP
task are running. Blocking socket calls must run outside the IP task and ISRs.
`socketStartup()` does not require an active Ethernet link. It checks the
scheduler state when that API is available; otherwise your application must
ensure the scheduler is running and not suspended. FreeRTOS-Plus-TCP itself
requires `xTaskGetCurrentTaskHandle()`.

```c
const uint8_t address[4] = {192, 168, 1, 50};
if (!XcpInit("firmware", "1.0", XCP_MODE_LOCAL) ||
    !XcpEthServerInit(address, 5555, false, 0)) {
    /* Handle startup failure. */
}
```

The server starts its RX/TX tasks asynchronously. Its transport information
reports the configured bind address: binding to `0.0.0.0` also reports `0.0.0.0`.
The MAC address is reported as zeros because local-address discovery is not
implemented.

UDP startup checks that binding and receive-timeout setup succeed. If either
fails, it cleans up the resources created so far. If the stack rejects a close
request, XCPlite keeps the handle and retries that cleanup before a later
initialization can replace it. Task and mutex allocation still use the existing
assertion-based failure handling.

## Send and error semantics

A successful UDP send returns the number of bytes accepted by FreeRTOS-Plus-TCP.
It does not confirm that the XCP client received the datagram.

Sending uses the stack's copying API, so XCPlite keeps ownership of its buffers.
`FREERTOS_ZERO_COPY` requires a payload allocated by the stack and transfers
ownership on success. XCPlite queue buffers cannot be used with that API. The
wrapper adds no packet buffer or extra payload copy.

In V4.4.1, a send can return zero for several reasons: an oversized payload,
failed automatic binding, no available network buffer, or failure to enqueue the
send event. XCPlite checks payload limits before sending. After a zero result,
it checks the local port and reports an error if automatic binding failed. For
a bound socket, it retries the send internally, matching lwIP's indefinite
`ERR_MEM`/`ENOMEM` wait.

Negative `ENOMEM` and `ENOBUFS` results are also retried. Other negative errors,
including `EWOULDBLOCK`, `ETIMEDOUT`, and `EINTR`, return to the caller. V4.4.1
reports timeouts while waiting for send resources as zero. An unexpected short
positive send is treated as an I/O error.

Retries yield for one RTOS tick. `OPTION_FREERTOS_SEND_TIMEOUT_MS` defaults to
20 ms and applies to each stack send attempt. Setting it to zero makes each
attempt nonblocking. The wrapper can still wait indefinitely if resources remain
unavailable.

The transport holds its counter mutex while sending. A blocked DAQ send therefore
also delays CRM responses, and queue overflow can discard new measurements under
the existing queue policy. Packet counters, segment accumulation, CRM queueing,
command dispatch, and session handling follow the existing transport behavior.
An unrecoverable send error terminates the TX worker.

Native UDP send and receive calls report errors as negative codes. Pass
`-result` to the socket error helpers to classify an error. This backend uses no TLS slot,
global last-error state, or `socketGetLastError()`. The UDP receive path reads
errors from the return value. A receive timeout, interruption, or empty datagram
returns zero. If you enable IPv6 in the stack, non-IPv4 datagrams are consumed
and ignored.

## Lifecycle limitations

This backend is intended for applications that initialize XCPlite once and keep
it running. The existing FreeRTOS `join_thread()` is a no-op, so shutdown does
not wait for the workers to exit before destroying resources. Graceful shutdown
and repeated runtime restart are therefore not guaranteed. A send can also stay
blocked indefinitely while holding the transport mutex if resources remain
unavailable.

UDP `socketShutdown()` is a no-op. A finite receive timeout lets the receive task
wake periodically, but does not wait for it to finish. Before closing a socket
directly, your application must ensure that every task using it has stopped
accessing it. `FreeRTOS_closesocket()` accepts cleanup asynchronously and may
block waiting for the IP task. This backend adds no cancellation, DAQ producer
synchronization, worker-completion handshake, or shutdown recovery mechanism.

## Validation

The emulator runs the shared FreeRTOS demo application and uses libpcap for
connections from external tools. The tests use the real stack's internal
loopback driver and never open a host network interface. To build and run the
tests without libpcap:

```sh
cmake -S examples/freertos_demo/freertos_plus_tcp_emu_demo -B build-plus-tcp -DCMAKE_BUILD_TYPE=Debug -DFREERTOS_PLUS_TCP_NETWORK_IF=LOOPBACK
cmake --build build-plus-tcp --parallel
ctest --test-dir build-plus-tcp --output-on-failure
```

Add `-DDEMO_STATIC_ALLOCATION=OFF` to test dynamic allocation for XCPlite tasks
and mutexes. The tests cover socket creation, bind and timeout-setup failures,
native error codes, buffer ownership after copying sends, actual network-buffer
exhaustion and recovery, CONNECT/DISCONNECT, CRM counters, DAQ transmission and
stop, segment accumulation, queue ownership during retries, and fatal TX errors.
They start the server once; shutdown/restart safety is outside these checks.

The stack's loopback address is separate from the Linux host's loopback address.
See the [demo README](../examples/freertos_demo/freertos_plus_tcp_emu_demo/README.md)
for libpcap networking, A2L generation and CANape project setup.
Before deploying to a board, check driver/DMA/cache behavior, link recovery,
sustained load, command latency, task priorities, stack usage, and network-buffer
capacity. The platform clock still uses the existing tick-based implementation
and retains its rollover behavior.
