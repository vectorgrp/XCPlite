# freertos_demo — Examples of how to use XCPlite on microcontroller platforms with FreeRTOS

There are 3 different demos:  

- freertos_emu_demo using the POSIX FreeRTOS emulator
- freertos_esp32_demo for ESP32 with PlatformIO
- freertos_stm32_demo for STM32 with CubeMX

All examples are based on the same demo application code in 'xcp_demo.c' and 'xcp_demo.h'.  

Refer to the README.md files in the demo folders for more specific details.  


## What is shown

The examples demonstrate XCPlite measurement and calibration running inside FreeRTOS tasks.

The included CANape projects and the XCP instrumentation in `xcp_demo.c` show:

- Create a high priority FreeRTOS task (fastTask) with precise cyclic execution timing
- Create a lower priority FreeRTOS task (slowTask) for background work
- Pin the tasks to the same core to watch scheduling in action
- Trigger XCP event tracepoints in both tasks and acquire global and local measurement variables
- Provide high-resolution XCP measurement event timestamps
- Display cycle time jitter of the tasks in CANape
- Count task deadline overruns when calibrated periods are too aggressive
- Create thread-safe calibration parameters accessible in both tasks
- Calibrate task cycle times and some other demo parameters
- Observe both task trigger points with a two-channel oscilloscope to evaluate XCP instrumentation cost
- Create an A2L file or A2L file template for the user application code

| Feature | API functions |
|---|---|
| XCP DAQ measurement | `DaqCreateEvent` / `DaqTriggerEvent` |
| XCP calibration | `CalSegDecl` / `CalSegDeclRef` / `CalSegCreate`, `CalSegLock` / `CalSegUnlock` |
| XCP server initialization | `XcpInit` `XcpEthServerInit` |

The example application code in xcp_demo.c compiles as C or as C++.  



## Build

Refer to the README.md files in the example folders.  

To build XCPlite library code for FreeRTOS, define `_FREE_RTOS` and `XCPLIB_CFG_OVERRIDE="xcplib_rtos_cfg.h"` to enable the FreeRTOS-specific code paths and configuration overrides.

The following files are required:

```
xcplite_sources = [
    "cal.c",
    "platform.c",
    "queue32m.c",
    "xcpappl.c",
    "xcpethserver.c",
    "xcpethtl.c",
    "xcplite.c",
]
```

The FreeRTOS build of XCPlite:
- Uses the FreeRTOS/lwIP socket, thread, mutex and clock platform abstractions in `src/platform.c`.
- Uses the mutex-based 32-bit queue implementation in `src/queue32.c` or the critical-section-based variant `src/queue32m.c`.


### Key files in the xcplite source tree

| File | Role |
|---|---|
| `src/xcplib_rtos_cfg.h` | XCPlite feature configuration for FreeRTOS targets |
| `src/xcplib_cfg.h` | Default feature configuration for POSIX / Windows targets |
| `src/xcp_cfg.h` | Default configuration for the XCP protocol layer |
| `src/xcptl_cfg.h` | Default configuration for the XCP ethernet transport layer |



## Generate A2L file

The xcpclient tool can be used to generate an A2L file or an A2L file template for further processing with other tools or CANape.  

Examples:

```bash
# Create an A2L file template with events, memory_segments, epk, and all required communication settings
# The given IP address is written to the A2L XCP IF_DATA  
xcpclient --offline --udp --dest-addr <ip-addr> --elf <elf-file>  --elf-unit-filter xcp_demo --a2l <a2l-file> --create-a2l-template


# Automatically add all possible measurement variables and calibration parameters in calibration parameter segments from compilation unit 'xcp_demo'
# Example freertos_stm32_demo:
xcpclient --offline --udp --dest-addr 192.168.0.207 --elf build/Debug/STM32H753EthDemo.elf --a2l CANape/stm32_freertos_demo.a2l --elf-unit-filter xcp_demo
# Example freertos_emu_demo:
xcpclient --offline --udp --dest-addr 127.0.0.1 --elf build-rtos/Debug/freertos_emu_demo --a2l examples/freertos_demo/freertos_emu_demo/CANape/freertos_demo.a2l --elf-unit-filter xcp_demo
```

See below how to obtain the xcpclient tool.  


## Test XCP Measurement and Calibration

Using xcpclient:  

Watch the global_counter counter

```bash
xcpclient --udp --dest-addr <esp32-ip-address> --a2l <a2l-file> --mea global_counter 
# Example freertos_emu_demo:
xcpclient --udp --dest-addr 127.0.0.1 --a2l examples/freertos_demo/freertos_emu_demo/CANape/freertos_demo.a2l --mea global_counter 
```

Watch scheduler pressure:

```bash
xcpclient --udp --dest-addr <esp32-ip-address> --a2l <a2l-file> --mea fastTaskOverruns
```

Calibrate

```bash
xcpclient --udp --dest-addr <esp32-ip-address> --a2l esp32_freertos_demo.a2l --cal parameters.counter_max 500
```

Or use the CANape project in folder `CANape`.

![CANape Screenshot](CANape.png)

Notes on CANape:
- To enable the CANape internal ELF/DWARF reader and address updater, select the Map file reader: 'C# version with extended C++ support'.  
- CANape will read the IP address of the XCP server from the generated A2L file. The xcpclient A2L generator writes the IP address given on its command line or otherwise defaults to 127.0.0.1.


### What to Measure

Good first measurements:
- `global_counter`: global fast task activity counter
- `fastTaskOverruns`: high-priority task missed deadlines
- `slowTaskOverruns`: lower-priority task missed deadlines
- `counter` in `fastTask`: local stack measurement in scope when the fast-task DAQ event is triggered
- `counter` and `channel` in `slowTask`: local stack measurements in scope of the slow task DAQ trigger tracepoint

And calibration parameters to play with:
- `parameters.fast_task_period_ms`: calibratable fast task period
- `parameters.slow_task_period_ms`: calibratable slow task period
- `parameters.amplitude`: calibratable sine amplitude
- `parameters.counter_max`: the maximum value of the fast task counter, global_counter variables

The local variables are intentionally marked `volatile` in `main.cpp` so optimized builds keep them visible (spilled to stack) for offline ELF/DWARF based A2L generation.  
Depending on the compiler and its optimization level, simple demo measurement variables may be optimized away completely.





## Code instrumentation for offline A2L generation

The xcpclient A2L generator reads two named ELF sections written by the XCPlite macros, plus
DWARF debug information, to build the A2L file without any runtime A2L calls in the application.

**`xcp_evts` section** — every `DaqCreateEvent(name)` or `DaqCreateAndTriggerEvent(name)`
emits a `tXcpEventDescriptor` (name, cycle time, priority) into `.xcp_evts`. xcpclient
iterates this section to discover all events.

**`xcp_cals` section** — every `CalSegDecl(name)` at file scope emits a `tXcpCalSegDescriptor`
(name, default page address, size) into `.xcp_cals`. xcpclient iterates this to discover all
calibration segments.

**DWARF trigger point anchors** — every event trigger macro emits a named static variable
(e.g. `trg__AAS__name`, `trg__AASD__name`) whose name encodes the active addressing modes.
xcpclient finds this variable in the DWARF and walks all variables in the same lexical scope
— those become the local measurements in the A2L.

**Therefore: always use the macros, never the raw C API functions** (`XcpCreateEvent()`,
`XcpCreateCalSeg()`, etc.). Only the macros emit the section data and anchors that xcpclient
needs to build the A2L automatically.

**Local variables must be `volatile`** in optimized builds to remain visible in DWARF.
Without `volatile` the compiler may eliminate them or give them unreliable location expressions:

```cpp
void fastTask(void *pv) {
    volatile uint32_t counter = 0;   // XCP: keep on stack for offline A2L
    DaqCreateAndTriggerEvent(fastTask);
}
```

**Build with debug information** (`-g` / `Debug` or `RelWithDebInfo`). Strip builds have no
DWARF type or location data.

For the complete technical details — ELF section layouts, `trg__` anchor naming convention,
and `AddrExt` encoding — see
[docs/TECHNICAL.md — Offline A2L Generation](../../docs/TECHNICAL.md#offline-a2l-generation--elfdwarf-internals).



## Configuration and Memory Consumption

FreeRTOS configuration overrides are in `xcplib_rtos_cfg.h`:  

| Option | Value | Reason |
|---|---|---|
| `OPTION_QUEUE_32` | set | Mandatory on Cortex-M4: no 64-bit atomic operations |
| `OPTION_QUEUE_32_SEGMENT_COUNT` | 16 | Number of statically allocated transmit queue segments; minimum 2 |
| `OPTION_CLOCK_TICKS_1US` | set | `xTaskGetTickCount()`-based clock, 1 µs unit |
| `OPTION_MTU` | 1500 | 1500 − 20-byte IPv4 header − 8-byte UDP header = 1472-byte maximum UDP payload |
| `OPTION_CAL_MEM_SIZE` | 4 KB | Tune to available SRAM |
| `OPTION_DAQ_MEM_SIZE` | 4 KB | Tune to available SRAM |
| `OPTION_CAL_SEGMENT_COUNT` | 8 | Tune to number of calibration segments needed |


Stack size settings, DAQ list size, DAQ queue size, maximum number of events and calibration segments influence memory consumption and may be tuned. 

Check log-level 5 for information about memory usage:

Example:
```
XcpInit name=stm32_freertos_demo, epk=V100, mode=01
  sizeof(tXcpData)=8200  sizeof(tXcpLocalData)=176
XcpEthServerInit
  sizeof(gXcpServer)=24
Init transport layer queue (queue32)
  buffer_size=23680, queue_size=16 (23680 Bytes)
```

The 32-bit transmit queue used for FreeRTOS has a fixed, statically allocated buffer (`queue32m.c`). Its size is derived from `OPTION_QUEUE_32_SEGMENT_COUNT`.
There are no heap allocations in the 32-bit FreeRTOS build.

Each queue segment provides storage for up to one UDP payload plus 8 bytes of internal metadata. With the default MTU of 1500, each segment occupies 1480 bytes and the default 16 segments consume 23,680 bytes of static RAM. Increase `OPTION_QUEUE_32_SEGMENT_COUNT` if packets are dropped during DAQ bursts, or decrease it to reduce static RAM usage.

The queue state and buffer use the default `.dtcm` and `.noncacheable` sections on embedded targets. These sections may be overridden in `xcplib_rtos_cfg.h` to match the application linker script:

```c
#define OPTION_QUEUE_32_ATTRIBUTE __attribute__((section(".queue")))
#define OPTION_QUEUE_32_BUFFER_ATTRIBUTE __attribute__((section(".queue_buffer")))
```

The memory size of static `tXcpData` depends on the configuration in `xcplib_cfg.h` and `xcplib_rtos_cfg.h`:
- OPTION_CAL_SEGMENT_COUNT: Max number of calibration segments or blocks
- OPTION_CAL_MEM_SIZE: Space reserved for calibration data swapping and working pages (needs: 3 * page size * segment count)
- OPTION_DAQ_MEM_SIZE: Memory size for DAQ tables (needs: 6 bytes per measurement with full fragmentation)
- OPTION_DAQ_EVENT_COUNT: Maximum number of DAQ events

The stack sizes of the XCP RX and TX tasks may be defined in `xcplib_rtos_cfg.h`.
It is important to check these values to avoid wasting memory. Search for `TEST_STACK_SIZE`.

Example:
```
#define OPTION_FREERTOS_STACK_BYTES (4U * 1024U)
#define OPTION_FREERTOS_PRIORITY (tskIDLE_PRIORITY + 2U)
```

For further optimization, the stack sizes for the receive and transmit tasks should be tuned separately because the RX path has higher stack usage than the TX path. Typical values are 4 KB for RX and 2 KB for TX.
The stack usage of the XCP code has been optimized, but lwIP socket stack usage might depend on configuration.
Both XCP tasks currently use one tXcpCtoMessage (roughly MAX_CTO_SIZE+4 = 252 bytes) on stack, which may be optimized further.  




## Getting xcpclient

`xcpclient` is used for two jobs in this demo:

- generating an A2L file from the firmware ELF
- running simple command-line XCP connection, calibration and measurement tests

### Build xcpclient

Build `xcpclient` from source.  
The Rust `xcp-lite` library is an external dependency. It contains the A2L registry, reader, and writer for the xcpclient database generator.

For the current development state, use matching branches of both repositories, for example `V2.1.x` on your XCPlite fork and the corresponding `V2.1.x` branch of your `xcp-lite` fork or dependency in Cargo.toml of xcpclient.

Typical workflow:

```bash
cd git
git clone --recursive <xcp-lite-repository-url>
git clone  <XCPlite-repository-url>
cd git/xcp-lite
git checkout V2.1.0
git submodule update --init --recursive
cd git/XCPlite
git checkout V2.1.0
cd tools/xcpclient
cargo build --release
cargo install --path .
```

After installation, confirm that the tool is reachable:

```bash
xcpclient --help
```

Note that the A2L generator is not considered stable yet. 
It has been tested with ELF files from Linux gcc and clang tool chains.  

For more information on offline A2L generation see:
- [tools/xcpclient/README.md](../../tools/xcpclient/README.md) — xcpclient documentation and all command-line options
- [examples/no_a2l_demo/README.md](../no_a2l_demo/README.md) — dedicated no-A2L / offline A2L workflow example
- [docs/TECHNICAL.md — Offline A2L Generation](../../docs/TECHNICAL.md#offline-a2l-generation) — ELF/DWARF internals and design details of the offline A2L generation approach
- [examples/freertos_demo/README.md](../freertos_demo/README.md) — Linux FreeRTOS demo with offline A2L generation



## Integrating XCPlite into a FreeRTOS application

### Step 1 — Add xcplite to your project

XCPlite ships as a CMake package. Add it as a subdirectory or install it and use `find_package`:

```cmake
add_subdirectory(path/to/XCPlite)  # builds xcplite static library
target_link_libraries(my_app PRIVATE xcplite freertos_kernel)
```

For a bare-metal target, also add the compile definitions:

```cmake
target_compile_definitions(xcplite PRIVATE
    _FREE_RTOS                                      # use FreeRTOS-specific code paths and platform abstraction
    "XCPLIB_CFG_OVERRIDE=\"xcplib_rtos_cfg.h\""     # apply FreeRTOS-specific configuration overrides
)
```

| Description:
|   XCPlite configuration OVERRIDES for FreeRTOS embedded targets
|   Applied AFTER the defaults in xcplib_cfg.h via:
|     cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_rtos_cfg.h\"")
|
|   Only settings that DIFFER from the POSIX defaults are listed here.
|   Key differences:
|     - OPTION_QUEUE_32          mandatory when no 64-bit atomics are available
|     - OPTION_CLOCK_TICKS_1US   FreeRTOS tick-based clock (1 ms granularity)
|     - No persistence, no A2L/ELF upload (no filesystem)
|     - No forceful thread termination (use vTaskDelete instead)
|     - Reduced queue size, and max event number and calibration segment counts to fit in embedded SRAM



### Step 2 — Provide `FreeRTOSConfig.h`

XCPlite includes `FreeRTOS.h` when `_FREE_RTOS` is defined.  
The FreeRTOS kernel must be able to find `FreeRTOSConfig.h`.  
With FreeRTOS-Kernel V11+, use the recommended `freertos_config` interface library:

```cmake
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"  # directory that contains FreeRTOSConfig.h
)
target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)
```

### Step 3 — Implement the socket layer

When `_FREE_RTOS` is defined **without** `FREE_RTOS_POSIX_SIM`, the socket functions in
`platform.c` use the lwIP socket API.
Replace them with another implementation if required.
The required interface is documented in `src/platform.h` (search for `SOCKET_HANDLE`).


### Step 4 — Implement the clock (bare-metal)

The FreeRTOS clock in `platform.c` uses `xTaskGetTickCount()` which gives 1 ms granularity at 1 kHz.  
For high-resolution timestamps, register an alternative clock function as shown in the examples.  
For example DWT on STM32.  



### Step 5 — Initialise XCPlite and start tasks

```c

// Initialize XCP
XcpSetLogLevel(3);
XcpInit("my_project", "V1.0", XCP_MODE_LOCAL);
XcpCreateEpk("V1.0");

// XcpEthServerInit creates two internal tasks for RX and TX.
uint8_t addr[4] = {192, 168, 0, 10};
XcpEthServerInit(addr, 5555, false /*UDP*/, 8192 /*queue bytes*/);

// Create your application tasks
xTaskCreate(myMeasurementTask, "meas", 512, NULL, tskIDLE_PRIORITY + 2, NULL);

vTaskStartScheduler();  // never returns on bare-metal (no vTaskEndScheduler)
```

### Step 6 — Add measurement and calibration in a task

```c

// Calibration parameters
static const MyParams params = { .gain = 1.0f, .offset = 0.0f };

// Create a calibration segment for the parameters named 'params' with the address and size of the params structure
CalSegDecl(params);  // or use XcpCreateCalSeg(params) at runtime

// Inside a FreeRTOS task:
void myMeasurementTask(void *pv) {

    // Create XCP event
    DaqCreateEvent(task_fast);           // register an XCP DAQ event
 
    uint32_t counter = 0;

    TickType_t xLastWake = xTaskGetTickCount();
    for (;;) {

        counter++;

        // Read calibration parameters thread-safely and consistently
        const MyParams *p = CalSegLock(params);
        float value = p->gain * readSensor() + p->offset;
        CalSegUnlock(params);

        // Trigger XCP event to measure any global, static and local variables
        DaqTriggerEvent(task_fast);

        xTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(1));  // 1 ms period
    }
}
```

## Calibration parameters in C++ — `CalSegDeclRef`

The C++ demo uses `CalSegDeclRef`, the idiomatic C++ macro for static (compile-time registered)
calibration segments. It combines a linker-section descriptor (for `xcpclient` and `XcpInit`)
with a typed RAII handle in a single declaration:

```cpp

// 1. Define the parameter struct and its default values (const = reference/FLASH page)
//    Note that the struct name and the variable name must be identical for xcpclient to find the default value in the ELF ! 
struct parameters {
    uint32_t fast_task_period_ms;
    uint32_t slow_task_period_ms;
    uint16_t counter_max;
    float    amplitude;
};
const struct parameters parameters = { .fast_task_period_ms = 2, ... };

// 2. Declare the calibration segment and typed C++ handle in one line
//    XcpInit() and xcpclient scan the xcp_cals section and register the segment automatically.
CalSegDeclRef(parameters, parameters_calseg);
// This expands to:
//   static tXcpCalSegIndex     calseg_id_parameters = XCP_UNDEFINED_CALSEG;
//   static tXcpCalSegDescriptor   calseg__parameters   __attribute__((section("xcp_cals"))) = {...};
//   static CalSegRef<parameters> parameters_calseg(&calseg_id_parameters, &parameters);
//

// 3. Use the handle anywhere — RAII lock returns a const pointer, auto-unlocks on scope exit
{
    auto params = parameters_calseg.lock();
    uint32_t period = params->fast_task_period_ms;
} // unlocked here
```

The lock is **wait-free** — it uses atomics (RCU), not a mutex — so it is safe to call
from an ISR or from a high-priority FreeRTOS task.


**`CalSegDeclRef(value, handle)`** vs. the other calibration API macros:

| Macro / Class | Registration | Handle type | Use case |
|---|---|---|---|
| `CalSegDeclRef(val, hdl)` | `xcp_cals` section → `XcpInit()` | `CalSegRef<T>` | C++ with section-registered segment |
| `CalSegDecl(val)` | `xcp_cals` section → `XcpInit()` | `val##_calseg` (same) | C++ shorthand — handle named `<val>_calseg` |
| `CalSegDecl` (C, `xcplib.h`) | `xcp_cals` section → `XcpInit()` | `calseg_id_<name>` | C with section-registered segment |
| `xcp::CalSeg<T>(name, ptr)` | Runtime `XcpCreateCalSeg()` | `CalSeg<T>` | C++ with runtime A2L generation |


## Notes

- Calibration is lock-free and based on atomics.
- The data acquisition queue is protected by a mutex.
- XCP creates two tasks/threads for RX and TX.




## TODO

- Don't generate fixed events for global variables
- Calibration parameter segment address update, fix segment addresses ?
  Implement IF_DATA description to enable CANape Linker-Map update for MEMORY_SEGMENTS 
- xcpclient error message when .aml not found
- Support for A2L generation of arrays
- Disable the automatic GET_ID and try A2L upload in xcpclient
- Check if the mutex-based queue is acceptable or if we should port one of the lockless queue implementations based on 64-bit atomic head and tail
- Add TCP support
- Do some benchmarking on CPU load, event trigger and calibration RCU latency
- Avoid copying the transmit buffers
- CANape does not support address update for local variables on stack and address update of calibration memory segments.  
