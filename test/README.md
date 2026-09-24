# XCPlite tests

This folder contains the unit, integration and interactive tests for `libxcplite`, plus a few
shell scripts that drive end-to-end and hardware/network scenarios.

The tests do not (yet) follow a single uniform pattern.
Some are self-checking unit tests that return a pass/fail exit code, some start an XCP server and wait for a human to connect a tool (CANape or `xcpclient`) to inspect the output, and a few need real hardware or root privileges.
The self-verifying tests are now wired into **CTest** (`ctest --test-dir build`).


---

## Quick start

Tests are plain executables gated behind `-DXCPLITE_BUILD_TESTS=ON`. Which tests exist depends on
the active `XCPLITE_CONFIGURATION` (see [docs/BUILDING.md](../docs/BUILDING.md)).

```bash
# Build the tests, then build and run them
./build.sh tests           # build only
./build.sh tests run       # build and run the registered tests via ctest
#   'tests' is equivalent to:
#   cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DXCPLITE_BUILD_TESTS=ON
#   cmake --build build --parallel

# Run the self-verifying tests unattended via CTest directly
ctest --test-dir build --output-on-failure   # all registered tests (same as: ./build.sh tests run)
ctest --test-dir build -L unit               # only the fast unit tests

# Or run an individual test binary directly
./build/daq_config_test
./build/passive_test_c
./build/cal_test            # hermetic self-check; add --server for interactive CANape/xcpclient use
```

`./build.sh tests` only builds the test binaries; `./build.sh tests run` builds and then runs them via
`ctest`. You can also run `ctest` yourself or run a binary directly.
The interactive server tests (`daq_test`, `clock_test`), the client probe (`xcp_test`) and the
`queue_test` benchmark are **not** registered with CTest — run them by hand (see below).  

Other configurations expose different tests:

```bash
./build.sh ptp tests     # clock_test only (Linux + PTP hardware)
./build.sh raw tests     # socket_raw_test only (Linux)
```

---

## Test catalog

Legend for **Type**:
- **unit** — self-contained, self-checking, no network peer or external tool; exits `0` on pass,
  non-zero on fail. Registered with CTest, safe to run unattended / in CI.
- **unit+tool** — self-checking, but needs an external validator binary installed to fully pass;
  registered with CTest only when the tool is found.
- **interactive** — starts an XCP server and **runs until you connect a tool** (CANape or
  `xcpclient`) and observe the result manually; not registered with CTest.
- **client** — connects to an already-running XCP server; used to probe connectivity by hand.
- **hardware** — needs a real NIC / PTP grandmaster / remote target / root.
- **benchmark** — long-running throughput/stress run that prints statistics.

| Test binary | Lang | Config | Type | CTest | Needs | What it covers |
|---|---|---|---|---|---|---|
| `type_detection_test_c` | C | default | unit | yes (`unit`) | — | `_Generic` A2L type detection ([type_detection_test/](type_detection_test/)) |
| `type_detection_test_cpp` | C++ | default | unit | yes (`unit`) | — | C++ trait-based A2L type detection |
| `daq_config_test` | C | default | unit | yes (`unit`) | — | Dynamic DAQ list config bounds & event linking via the command processor ([daq_config_test/](daq_config_test/)) |
| `eth_transport_test` | C | default | unit | yes (`unit`) | loopback sockets | TCP/UDP command framing & receive recovery (forks a child) ([eth_transport_test/](eth_transport_test/)) |
| `socket_recv_test` | C | default | unit | yes (`unit`) | loopback sockets | Deterministic header/payload receive splits |
| `passive_test_c` | C | default | unit | yes (`unit`) | — | `XCP_MODE_DEACTIVATE` passive-mode contract ([passive_test/](passive_test/)) |
| `passive_test_cpp` | C++ | default | unit | yes (`unit`) | — | Passive-mode contract via the C++ API |
| `cal_test` | C++ | default | unit (+opt. server) | yes (`unit;calibration`) | — hermetic by default; `--server` adds CANape/`xcpclient` | Calibration-segment RCU correctness + multi-thread stress; **self-terminates** ([cal_test/](cal_test/)) |
| `a2l_test` | C | default | unit+tool | yes if [`a2ltool`](https://crates.io/crates/a2ltool) on `PATH` | `a2ltool` | Generates an A2L and validates it with an external checker ([a2l_test/](a2l_test/)) |
| `queue_test` | C | default | benchmark | no | — | Lock-free transmit-queue correctness & throughput (multi-thread/process) ([queue_test/](queue_test/)) |
| `daq_test` | C | default | interactive | no | CANape or `xcpclient` | Multi-thread DAQ measurement server; loops until Ctrl+C ([daq_test/](daq_test/)) |
| `clock_test` | C++ | default, ptp | interactive / hardware | no | CANape/`xcpclient`; PTP config also needs `ptp4l` + a PTP NIC | Clock/time-sync behavior; loops until Ctrl+C ([clock_test/](clock_test/)) |
| `xcp_test` | C++ | default | client | no | a running XCP server (e.g. `hello_xcp`) | Sends a raw CONNECT and prints the response; connectivity probe ([xcp_test/](xcp_test/)) |
| `socket_raw_test` | C | raw | unit | yes (`unit`) | Linux | Raw-Ethernet transport: checksums, framing, ARP/ICMP (fake HAL, no network) ([socket_raw_test/](socket_raw_test/)) |


> **Platform note:** the `eth_transport_test` cases `tcp_partial_frames` and `socket_accept_failure`
> are excluded on macOS (`#if !defined(_MACOS)`) because the BSD/Darwin TCP stack does not surface
> the partial-frame shutdown and non-blocking accept-failure states the same way Linux does. All
> other cases run on macOS, and the full set runs on Linux/Windows.

---

## Driving the interactive tests

The interactive server tests `daq_test` and `clock_test` loop until Ctrl+C and display some statistics, they do not verify
themselves.


---

## Example integration script: `test_examples.sh`

[`test_examples.sh`](test_examples.sh) is a smoke/regression harness that runs the **`examples/`** binaries (not the
test binaries in this folder), each for ~2 s, optionally connects `xcpclient`, and diffs the
generated `.a2l`/`.hex` against golden files in [`fixtures/`](fixtures/).

```bash
./test_examples.sh                 # run all examples
./test_examples.sh hello_xcp       # run one example
./test_examples.sh clean           # delete generated .a2l/.bin/.hex first, then run all
```

Requires a prior `./build.sh examples`. Optional tools on `PATH`: `xcpclient`, `bintool`, `a2ltool`.
On a fixture mismatch the script **overwrites the fixture** and records it in the summary — review
the diff in `test/test_*.log` before committing an updated fixture.

## Hardware / privileged shell scripts

| Script | Needs | Purpose |
|---|---|---|
| [`test_socket_raw.sh`](test_socket_raw.sh) | Linux, **root** | Sets up a veth/netns pair to manually exercise the raw Ethernet transport (see [docs/SOCKET_RAW.md](../docs/SOCKET_RAW.md)) |
| [`test_bintool.sh`](test_bintool.sh) | remote Raspberry Pi over SSH | BIN→HEX→upload round-trip for `bintool` |
| [`test_bpf_demo_pi.sh`](test_bpf_demo_pi.sh) | remote Raspberry Pi over SSH | End-to-end `bpf_demo` workflow on the Pi |

These are **not** part of any automated run and need manual setup; run them by hand.

---

## Conventions for new tests

1. Is **self-verifying**: it decides pass/fail itself and returns `0` on success, non-zero on
   failure. Do not require a human to inspect output.
2. Is **hermetic and deterministic**: no dependency on wall-clock timing, no external server, no
   network peer beyond loopback, no installed tool, no hardware. Same result every run.
3. Uses the shared `CHECK` macro from [support/check.h](support/check.h), which **evaluates in
   Release too** (unlike bare `assert`, which compiles out under `NDEBUG`) and prints the failing
   condition, file and line. `CHECK(cond)` and `CHECK(cond, "message", ...)` (printf-style, optional)
   both work.
4. Is **fast** (well under a second) so it can run on every PR.
5. Is registered in the `default` block of the root [CMakeLists.txt](../CMakeLists.txt) test section,
   linking `xcplite`. Split C and C++ variants as `<name>_c` / `<name>_cpp`.
6. Prints a final `PASSED` line for humans, but the **exit code is the source of truth**
   (`CHECK` exits non-zero on the first failing condition).

If the test genuinely needs a server, hardware, root, or an external tool - ok, but
label it (see proposals below), keep it out of the default unattended run, and document
in the test's header comment exactly how to drive it and what "pass" looks like.

Minimal template (copy [passive_test/src/main.c](passive_test/src/main.c) for a full example):

```c
#include "../../support/check.h" // shared CHECK macro (fatal, prints cond/file/line + optional message)

int main(void) {
    // ... exercise the API ...
    CHECK(some_condition);                          // no message
    CHECK(value == expected, "value was %d", value); // optional printf-style message
    printf("PASSED\n");
    return 0;
}
```

## Possible Improvements

1. Register the interactive/benchmark tests as opt-in (CTest `DISABLED` property or a
   `--serve`/`--interactive` self-terminating flag like `cal_test` now has), so they can be run with
   `ctest -L interactive` without hanging the default run.
2. Detect optional tools/hardware and skip (CTest `SKIP_RETURN_CODE`) instead of failing
   when `xcpclient`/PTP is absent. `a2l_test` already only registers when `a2ltool` is found.
3. Normalize naming to `<area>_test[_c|_cpp]` and keep the `_c`/`_cpp` suffix whenever both
   language variants exist.
4. Generate `build.sh`'s printed test list from CMake instead of a hand-maintained string (it is
   currently stale). `./build.sh tests run` already builds and runs the registered tests via `ctest`.
5. Adopt the shared [support/check.h](support/check.h) `CHECK` macro in the remaining tests that still
   define their own (`socket_raw_test` uses a reversed `CHECK(what, cond)` tabular reporter;
   `cmp_demo`'s codec test has its own count-and-continue variant).
