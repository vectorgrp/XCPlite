# xcp_test

`xcp_test` is an XCP on UDP client test. It sends an XCP command sequence, prints raw and decoded responses and measures the command round trip latency.

By default it is a hermetic self-check: it starts an XCP on UDP server in process on `127.0.0.1` and runs against it, registered with CTest (label `integration`).
With `--server <ip>` it probes an external XCP on UDP server instead, e.g. to check connectivity to a new target and to analyze the transport layer framing.

## What It Does

- Sends this command sequence:
  - `CONNECT`
  - `GET_VERSION`
  - `GET_COMM_MODE_INFO` (when supported by `comm_mode_basic`)
  - `GET_STATUS` latency measurement
  - `DISCONNECT`
- Prints TX and RX raw bytes, the transport layer header and the decoded `CONNECT`, `GET_VERSION` and `GET_COMM_MODE_INFO` responses
- Measures the command round trip latency with `GET_STATUS`, printing min, median, avg, p90, p99 and max:
  - back to back: `--count` commands without pause after 10 warm-up commands, the steady state latency
  - after idle: 20 commands, each after a 10 ms sleep, includes the wake-up latency of the client and server threads
  - nothing is printed during the measurement, so console output does not distort the results
- Supports multiple XCP messages in one UDP datagram, skips `EVENT` and `SERV` messages

## Pass/Fail

The test exits with `0` on success and `1` on any of these errors:

- socket error or receive timeout
- negative response or response too short
- unexpected PID, more than one response in a datagram
- transport layer length exceeding the datagram
- transport layer counter not incrementing by `+1`
- median back to back latency above the limit (`--max-latency`, default 1000 us)

The limit is checked against the median, not the average, to be robust against outliers on loaded machines. It detects gross regressions,
e.g. command responses delayed by the transmit queue. The after idle latency depends on OS power management and is only reported.

## Build

From repository root.

Linux/macOS (GCC/Clang):

```sh
cmake -B build -S . -DXCPLITE_BUILD_TESTS=ON
cmake --build build --target xcp_test
```

Windows (MSVC):

```sh
cmake -B build-msvc -S . -DXCPLITE_BUILD_TESTS=ON
cmake --build build-msvc --target xcp_test --config Debug
```

## Run

```sh
./build/xcp_test [options]
ctest --test-dir build -R xcp_test --output-on-failure
```

## Options

- `--server <ip>`: probe an external XCP on UDP server instead of the in-process server
- `--port <port>`: external server port, default `5555`
- `--timeout <ms|none>`: receive timeout in milliseconds, `none` or `0` disables the timeout (for server-side debugging), default `3000`
- `--count <n>`: number of back to back `GET_STATUS` commands for the latency measurement, `0` skips it, default `1000`
- `--max-latency <us>`: limit for the median back to back latency, `0` disables the check, default `1000`
  - the environment variable `XCP_TEST_MAX_LATENCY_US` overrides the default, the command line option takes precedence

## Examples

```sh
./build/xcp_test
./build/xcp_test --count 10000
./build/xcp_test --server 127.0.0.1
./build/xcp_test --server 192.168.0.207 --timeout none
XCP_TEST_MAX_LATENCY_US=5000 ctest --test-dir build -R xcp_test
```

## Notes

- The in-process server binds to `127.0.0.1` on the first free UDP port from `5599`, so it does not collide with an XCP application running on `5555`.
- The in-process server runs at log level 2, logging adds latency to every command. When probing an external XCPlite server, use log level 3 or lower for
  representative latency numbers. With `OPTION_ENABLE_DBG_CMD_TIMESTAMPS` the server log shows the server side share of the latency.
- The client binds to `INADDR_ANY` and an ephemeral local UDP source port, the selected source port is printed at startup.
- The implementation is cross-platform (`winsock2` on Windows, POSIX sockets on Linux/macOS).
