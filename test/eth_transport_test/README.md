# Ethernet transport tests

`eth_transport_test` validates TCP and UDP command framing and recovery from malformed traffic using loopback sockets. `socket_recv_test` checks fragmented reads and allocation-failure cleanup.

## Build

From the repository root.

Linux/macOS (GCC/Clang):

```sh
cmake -S . -B build-eth -DXCPLITE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-eth --target eth_transport_test socket_recv_test
```

Windows (Visual Studio 2022 and a Windows SDK, PowerShell):

```powershell
cmake -S . -B build-eth-win -G "Visual Studio 17 2022" -A x64 -DXCPLITE_BUILD_TESTS=ON
cmake --build build-eth-win --config Debug --target eth_transport_test socket_recv_test
```

## Run

Linux/macOS:

```sh
./build-eth/eth_transport_test
./build-eth/socket_recv_test
```

Windows (PowerShell):

```powershell
.\build-eth-win\Debug\eth_transport_test.exe
.\build-eth-win\Debug\socket_recv_test.exe
```

To run a single transport scenario, pass its name, for example:

```sh
./build-eth/eth_transport_test tcp_eof_stale_error
```

## Notes

- Covers invalid lengths, timeouts, EOF, reconnection, UDP peer preservation, and server-thread recovery.
- Uses ephemeral loopback ports; no external XCP client is required. Raw Ethernet hardware validation is not covered.
- Checks remain active in Release builds. Use `-DCMAKE_BUILD_TYPE=Release` on Linux/macOS, or `--config Release` and the `Release` executable directory on Windows.
- For Linux timestamp-enabled tests, add `-DOPTION_SOCKET_HW_TIMESTAMPS` to both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` in a separate build. This enables `socket_recv_test socket_open_allocation_failure` and `socket_recv_test socket_accept_allocation_failure`; no timestamp-capable hardware is required.
- For Linux ASan/UBSan coverage, configure a separate build with `-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and `-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`. Run with `UBSAN_OPTIONS=halt_on_error=1`.
