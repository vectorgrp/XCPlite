# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

XCPlite (`libxcplite`) is a C11/C++17 implementation of the ASAM XCP measurement and calibration protocol, for XCP-on-Ethernet (TCP/UDP), targeting multicore Linux/QNX/macOS microprocessors as well as FreeRTOS microcontrollers. It provides thread-safe, lock-free instrumentation macros/functions so application code can expose stack, heap, thread-local, and global variables as measurement signals and calibration parameters to XCP tools (CANape, CANoe, etc.), with A2L (ASAM description file) generation either at runtime on-target or offline from ELF/DWARF via the Rust `xcpclient` tool.

The public C API is `inc/xcplib.h` + `inc/a2l.h`; the C++ API is `inc/xcplib.hpp` + `inc/a2l.hpp` (RAII wrappers over the C core). Core protocol/runtime implementation lives in `src/`.

`libxcplite` also serves as the C foundation for the experimental [XCP-Lite Rust](https://github.com/vectorgrp/xcp-lite) crate.

## Build system

CMake-based. **Five mutually exclusive build configurations**, each with its own build directory — never mix configurations in one build dir, since compile definitions differ:

| Configuration | Build dir | Config override header | Use case |
|---|---|---|---|
| `default` | `build/` | *(none, uses `src/xcplib_cfg.h`)* | 64-bit, on-target A2L generation, filesystem, Ethernet UDP/TCP |
| `no_a2l` | `build-no_a2l/` | `src/xcplib_no_a2l_cfg.h` | No on-target A2L; generated externally from ELF via `xcpclient` |
| `ptp` | `build-ptp/` | `src/xcplib_ptp_cfg.h` | Socket hardware timestamps; Linux + PTP-capable NIC |
| `shm` | `build-shm/` | `src/xcplib_shm_cfg.h` | Shared-memory multi-application mode (`shmtool`, `xcpdaemon`) |
| `rtos` | `build-rtos/` | `src/xcplib_rtos_cfg.h` | FreeRTOS embedded targets: reduced footprint, no filesystem, 32-bit, no on-target A2L generation |

Selected via `-DXCPLITE_CONFIGURATION=<name>` (default: `default`). Within a configuration, `XCPLITE_BUILD_EXAMPLES`, `XCPLITE_BUILD_TESTS`, `XCPLITE_BUILD_TOOLS` (all default `OFF`) control which targets get built — which targets exist depends on the active configuration (see table in `docs/BUILDING.md`). `XCPLITE_BUILD_RUST_TOOLS` builds `xcpclient`/`bintool` via cargo (any configuration). `XCPLITE_BUILD_BPF_DEMO` builds `bpf_demo` (default config, Linux only, requires libbpf).

`examples/silkit_demo`, `examples/external_example` and `examples/fetchcontent_example` are standalone projects with their own `CMakeLists.txt` — they are not built from the root project. The first two consume an installed xcplite via `find_package(xcplite)`; `fetchcontent_example` builds xcplite from source via `FetchContent` (`./build.sh local` builds against the working tree). Both paths provide the target `xcplite::xcplite`. When consumed as a subproject, the root `CMakeLists.txt` leaves the consumer's install prefix and `CMAKE_<LANG>_FLAGS_<CONFIG>` untouched and defaults `XCPLITE_INSTALL` to `OFF`.

### Common commands

`build.sh` wraps CMake: `./build.sh [build_type] [configuration] [target] [options]` (any order; `build_type` = `debug`|`release`|`relwithdebinfo`, default `debug`; `configuration` = `default`|`no_a2l`|`ptp`|`shm`|`rtos`; `target` = `lib`|`examples`|`tests`|`tools`|`rust_tools`|`all`, default `examples`; `options` = `clean` `cleanall` `install` `install=<path>` `cargo_install` `tidy`).

```bash
./build.sh                          # library + examples, default config, debug
./build.sh tests                    # build+run default-config test suite
./build.sh shm tools                # shmtool + xcpdaemon
./build.sh ptp tools                # ptptool (Linux only)
./build.sh no_a2l examples          # no_a2l_demo(_cpp)
./build.sh rtos examples            # freertos_emu_demo (Linux/macOS host simulator)
./build.sh lib tidy                 # build lib + run clang-tidy
./build.sh clean examples           # clean rebuild
./build.sh cleanall                 # wipe all build-* dirs
CC=clang CXX=clang++ ./build.sh release all   # explicit compiler selection (respected even on existing build dirs)
```

Equivalent raw CMake:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DXCPLITE_BUILD_EXAMPLES=ON
cmake --build build --parallel
cmake --build build --target hello_xcp     # single target
cmake --install build                       # installs to build/install by default
```

Switching compilers requires a fresh build directory (CMake caches the compiler in `CMakeCache.txt`).

### Running tests

Tests are plain executables built under `XCPLITE_BUILD_TESTS=ON` (default config: `a2l_test`, `cal_test`, `daq_test`, `daq_config_test`, `clock_test`, `queue_test`, `xcp_test`, `type_detection_test_*`; `ptp` config: `clock_test` only). Build then run directly, e.g.:

```bash
./build.sh tests
./build/a2l_test
./build/cal_test
```

`test/test.sh [clean] [example_name]` runs the example integration tests against `build/` (not the unit test binaries above): builds each example, exercises it, and diffs generated `.a2l` files against `test/fixtures/`. Omit `example_name` to run all examples; `clean` first deletes generated `.a2l`/`.bin`/`.hex` files from the workspace root.

### Windows / QNX

Windows: MSVC via `cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build-msvc -DXCPLITE_BUILD_EXAMPLES=ON` (C++20, atomics emulated, transmit queue always mutex-based — see Known limitations below). QNX: requires QNX SDP installed; use `build_qnx.bat` (Windows host) or `./build.sh Debug qcc all -q=<sdp_path> -a=<x86_64|aarch64le>` (Linux host); C++ targets excluded on SDP ≤7.0 (no `std::optional`).

Full reference: `docs/BUILDING.md`.

## Architecture

### Layering

```
inc/xcplib.h, inc/a2l.h         C public API (instrumentation macros + functions)
inc/xcplib.hpp, inc/a2l.hpp     C++ RAII wrappers over the C API

src/xcplite.c, src/xcp.h        XCP protocol layer (command processing, DAQ list state machine)
src/xcpappl.c                   Application callback glue (clock, addressing, read/write)
src/xcpethserver.c/.h           XCP-on-Ethernet server (TCP/UDP), connection handling
src/xcpethtl.c/.h               Ethernet transport layer (packet framing)
src/xcpshmserver.h, src/shm.c/.h   Shared-memory transport layer (SHM configuration only)
src/queue*.c                    Lock-free/mutex transmit queue implementations (see below)
src/cal.c/.h                    Calibration segment RCU implementation (page switching, locks)
src/a2l.c, src/a2l_writer.c     Runtime A2L file generation
src/persistence.c/.h            Binary (.bin) parameter/event persistence across restarts
src/platform.c/.h               OS abstraction (threads, clock, mutex, atomics) — Linux/macOS/QNX/Windows/FreeRTOS
src/sockets.c/.h                Socket abstraction over the OS socket API (all standard platforms)
src/socket_raw.c                Raw-Ethernet UDP/IPv4 transport (OPTION_ENABLE_UDP_RAW), see docs/SOCKET_RAW.md
src/socket_raw_hal.h            Raw Ethernet HAL interface; socket_raw_hal_linux.c is the AF_PACKET backend
src/util.c/.h                   Shared helpers
```

`src/xcplib_cfg.h` holds default configuration knobs (protocol options, DAQ memory sizing, clock, logging). Each `src/xcplib_<name>_cfg.h` documents its overrides on top of that default — read the relevant one before changing configuration-dependent behavior. `src/xcptl_cfg.h` covers transport-layer packet sizing; `src/xcp_cfg.h` covers XCP protocol-layer feature flags (addressing modes, DAQ/calibration limits, checksum, seed/key).

### Transmit queue variants (`src/queue*.c`)

Selected at compile time based on platform/config (see `src/queue.h`):
- `queue64v.c` — generic, lock-free, variable entry size (default on 64-bit platforms with real atomics)
- `queue64f.c` — generic, lock-free, fixed entry size
- `queue32.c` — XCP-specific, mutex-based, variable size with message accumulation (32-bit/Windows fallback, or when `OPTION_ATOMIC_EMULATION` is set)
- `queue32m.c` — XCP-specific, critical-section based, variable size with accumulation (FreeRTOS)

The 64-bit lock-free queue needs `atomic_uint_least64_t`; some ARM/Clang combinations need `-latomic` (CMakeLists.txt detects and links this automatically).

### Calibration segments (RCU, see `docs/CAL_RCU.md`)

Calibration parameters live in **calibration segments**: a struct wrapped so the single XCP writer thread and multiple lock-free/wait-free reader threads (`XcpLockCalSeg`/`XcpUnlockCalSeg`, or `CalSeg<T>::lock()` in C++) stay consistent without blocking. Implementation uses a 3-page RCU scheme (`ecu_page`/`xcp_page`/`free_page`) — precondition is exactly one writer thread. Read `docs/CAL_RCU.md` before touching `src/cal.c`; the compromises/invariants documented there (e.g. visibility delay is "second lock after write", starvation is possible under heavy read contention) are load-bearing, not incidental.

### Addressing modes (see `docs/TECHNICAL.md`)

XCPlite encodes *where* a measured/calibrated variable lives (global, stack, heap, calibration-segment-relative) in the XCP address extension byte, since it uses relative addressing rather than plain absolute addresses. This is central to how instrumentation macros (`A2lSetAbsoluteAddrMode`, `A2lSetStackAddrMode`, `A2lSetRelativeAddrMode`, `A2lSetSegmentAddrMode`) and event triggers (`DaqTriggerEvent`, `DaqTriggerEventExt`, `DaqEventVar`/`DaqEventExtVar`) work together — an XCP client must respect fixed event definitions and address extensions to avoid corrupting acquisition. `CASDD` vs `ACSDD` (governed by `OPTION_CAL_SEGMENTS_ABS`) is a project-wide convention affecting whether address extension 0 means "absolute" or "calibration-segment-relative" — check which is active before reasoning about address extension values.

### Offline A2L generation (`xcpclient`, no-A2L builds)

`no_a2l` and `rtos` are the two configurations that do not use on-target runtime A2L generation. Build-time A2L generation instead relies on information embedded in ELF file sections and DWARF markers to locate events and calibration parameter segments in the code: the Rust `xcpclient` tool (`tools/xcpclient/`) parses the built ELF's DWARF debug info and two marker sections:
- `xcp_evts` section — `tXcpEventDescriptor` constants emitted by `DaqCreateEvent`/`DaqCreateAndTriggerEvent`
- `xcp_cals` section — `tXcpCalSegDescriptor` constants emitted by `CalSegDecl`+`CalSegCreate`

and DWARF scope anchors named `trg__<mode-letters>__<event-name>` (e.g. `trg__AAS__foo`, letter position = address-extension value [0..]: `A`=absolute, `C`=cal-segment-relative, `S`=stack-relative, `D`=dynamic/heap) emitted by the trigger macros, to reconstruct addressing without any runtime A2L calls. The marker contract (sections, marker names, trigger anchor naming) is in `docs/TECHNICAL.md`, the tool side (workflow, naming rules, symbol resolution, supported types) in `docs/OFFLINE_A2L.md`.

The generator reads ELF files only. Executables built on macOS are Mach-O and carry no DWARF (the linker leaves it in the `.o` files / `.dSYM`), so `xcpclient` rejects them with an explicit "macOS is not supported" error and exit status 1 — A2L files for `no_a2l`/`rtos` builds must be generated from a Linux build, which is what the examples' `create_a2l.sh` scripts do via a remote build on a Linux target.

### Shared-memory (SHM) multi-application mode (`docs/SHM.md`)

`shm` configuration lets multiple independent OS processes share one transmit queue and XCP server state via POSIX shared memory; exactly one process is elected XCP server/leader (`XCP_MODE_SHM_AUTO`) or forced (`XCP_MODE_SHM_SERVER`). Binary persistence (`.bin` file) is mandatory in this mode since it's how the shared state (event/calseg numbering, application list) is bootstrapped and kept stable across process restarts. `shmtool` inspects/clears SHM state; `xcpdaemon` is a standalone XCP-on-Ethernet server that attaches to SHM-instrumented applications.

### Configuration reference

`docs/xcplib_cfg.md` documents every tunable in `xcplib_cfg.h`/`xcptl_cfg.h`/`xcp_cfg.h` (protocol options, MTU/packet sizing, DAQ memory, clock epoch/resolution, calibration segment limits). Consult it before changing any `OPTION_*`/`XCP_*` macro rather than guessing at effects.

## Code style

- `.clang-format`: LLVM base style, 4-space indent, no tabs, 180-column limit. One format config covers both C and C++ (`Language: Cpp`).
- `.clang-tidy` defines the lint ruleset; run via `./build.sh lib tidy`.
- C11 / C++17 required (C++20 on Windows). Language features load-bearing for A2L type detection: `_Generic` (C11) and compiler-specific type introspection.

## Known platform limitations

- **Windows**: atomics are emulated; the transmit queue always falls back to the mutex-based implementation on the producer side (performance penalty vs. POSIX).
- **QNX ≤7.0 SDP**: C++ targets excluded (missing `std::optional`).
- CANape-specific protocol quirks/workarounds (COPY_CAL_PAGE, segment numbering, address extension handling, etc.) are catalogued in the "Known Issues" section of `docs/TECHNICAL.md` — check there before treating CANape interoperability behavior as a bug in this codebase.
