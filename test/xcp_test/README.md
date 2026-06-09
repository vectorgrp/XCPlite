# xcp_test

`xcp_test` is a small UDP XCP client test that sends an initial XCP command sequence to an XCP server and prints raw and decoded responses.

## What It Does

- Sends this command sequence to UDP port `5555`:
  - `CONNECT`
  - `GET_VERSION`
  - `GET_COMM_MODE_INFO` (when supported by `comm_mode_basic`)
- Prints TX and RX raw bytes
- Decodes and prints:
  - transport-layer payload length
  - transport-layer counter
  - UDP payload size
  - `CONNECT` response fields (resources, comm mode, max CTO/DTO, protocol versions)
  - `GET_VERSION` fields (protocol version, transport layer version)
  - `GET_COMM_MODE_INFO` fields (optional comm mode, max block size, min separation time, queue size, driver version)
- Tolerates transport-header payload length vs UDP payload mismatch and reports the difference
- Checks transport-layer counter progression across responses and reports whether it increments by `+1`

## Build

From repository root.

Windows (MSVC):

```sh
cmake -B build-msvc -S . -DXCPLITE_BUILD_TESTS=ON
cmake --build build-msvc --target xcp_test --config Debug
```

Linux/macOS (GCC/Clang):

```sh
cmake -B build -S . -DXCPLITE_BUILD_TESTS=ON
cmake --build build --target xcp_test
```

## Run

Windows (MSVC):

```sh
build-msvc/Debug/xcp_test.exe [server_ip] [timeout_ms|none]
```

Linux/macOS:

```sh
./build/xcp_test [server_ip] [timeout_ms|none]
```

## Arguments

- `server_ip` (optional): target IPv4 address
  - Default: `192.168.0.207`
- `timeout_ms|none` (optional): receive timeout
  - integer milliseconds, or `none` / `0` to disable timeout
  - Default: `3000`

## Examples

Windows examples:

```sh
build-msvc/Debug/xcp_test.exe
build-msvc/Debug/xcp_test.exe 127.0.0.1
build-msvc/Debug/xcp_test.exe 192.168.0.207 10000
build-msvc/Debug/xcp_test.exe 192.168.0.207 none
build-msvc/Debug/xcp_test.exe --help
```

Linux/macOS examples:

```sh
./build/xcp_test
./build/xcp_test 127.0.0.1
./build/xcp_test 192.168.0.207 10000
./build/xcp_test 192.168.0.207 none
./build/xcp_test --help
```

## Notes

- The client binds to `INADDR_ANY` and an ephemeral local UDP source port.
- The selected source port is printed at startup.
- Each response logs the transport-layer header and compares header payload length with actual UDP payload length.
- Counter progression is validated per response (`previous + 1` expected).
- The implementation is cross-platform (`winsock2` on Windows, POSIX sockets on Linux/macOS).
- Runtime behavior was validated in this workspace on Windows; Linux/macOS commands are provided above.
