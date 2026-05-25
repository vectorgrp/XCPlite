# freertos_demo — XCPlite on FreeRTOS

This example demonstrates XCPlite (XCP measurement and calibration) running inside FreeRTOS tasks.
It uses the **FreeRTOS POSIX simulator** port so the demo builds and runs on macOS or Linux before
switching to a real embedded target such as an STM32.

---

## Overview

### What it shows

| Feature | How it is demonstrated |
|---|---|
| Periodic FreeRTOS tasks | `xTaskCreate` + `xTaskDelayUntil` for drift-free timing |
| XCP DAQ measurement | `DaqCreateEvent` / `DaqTriggerEvent` inside tasks |
| XCP calibration | `CalSegDecl` / `CalSegCreate`, `CalSegLock` / `CalSegUnlock` |
| Clean shutdown | `watchdogTask` monitors a signal flag and calls `vTaskEndScheduler()` |
| XCP server lifecycle | `XcpEthServerInit` called before the FreeRTOS scheduler starts |

### Architecture

```
main()
├── XcpInit() + XcpEthServerInit()    ← two POSIX RX/TX threads created here
├── xTaskCreate(measurementTask1)     ← 1 ms DAQ task
├── xTaskCreate(measurementTask2)     ← 10 ms DAQ task
├── xTaskCreate(watchdogTask)         ← polls gRunning, calls vTaskEndScheduler()
└── vTaskStartScheduler()             ← blocks; each task runs as a pthread
    ...
    vTaskEndScheduler()               ← called by watchdog on SIGINT/SIGTERM
    XcpDisconnect() + XcpEthServerShutdown()
```

The FreeRTOS POSIX simulator maps each task to a pthread and uses `SIGUSR1`/`SIGUSR2` for task
switching.  Normal POSIX APIs (BSD sockets, `clock_gettime`) work transparently alongside FreeRTOS
tasks, which is why the unmodified xcplite library works without any changes to its internal
networking code.

### Files

| File | Purpose |
|---|---|
| `src/main.c` | Demo application — tasks, events, calibration segments, XCP server setup |
| `FreeRTOSConfig.h` | FreeRTOS kernel configuration (tick rate, heap, priorities) |
| `CMakeLists.txt` | Build system: FetchContent for FreeRTOS-Kernel V11, portability-test option |

---

## Building the FreeRTOS demo

### Prerequisites

- CMake ≥ 3.14
- GCC or Clang (macOS or Linux)
- Internet access for the first configure (FreeRTOS-Kernel is downloaded via FetchContent)

### Build

Compiles xcplite with the FreeRTOS platform code paths active (_FREE_RTOS) using the POSIX simulator (FREE_RTOS_POSIX_SIM) so that the demo can be built and run on a development machine before porting to a microcontroller.
Uses Linux sockets and `clock_gettime` for the POSIX simulator



```bash
cmake -B build-freertos -S . -DXCPLITE_BUILD_FREERTOS_DEMO=ON -DCMAKE_BUILD_TYPE=Debug --fresh
cmake --build build-freertos --target freertos_demo
./build-freertos/examples/freertos_demo/freertos_demo
```


### Create an A2L file


```bash
xcpclient --offline   --elf ./examples/freertos_demo/CANape/freertos_demo --create-a2l  --a2l ./examplesfreertos_demo/CANape/freertos_demo.a2l
```

For more details and options on the A2L file generation, see no_a2l_demo and the xcpclient documentation.  

---


### Connection test

Do a test measurment.  
Visualize the counter variables in task1 and task2 with the generated A2L file.  

```bash
xcpclient --udp --dest-addr 192.168.0.206:5555   --a2l ./examples/freertos_demo/CANape/freertos_demo.a2l  --mea counter --verbose 2
```
---






## Configuration

### `FreeRTOSConfig.h`

Key settings for the POSIX simulator:

| Setting | Value | Notes |
|---|---|---|
| `configTICK_RATE_HZ` | 1000 | 1 ms tick |
| `configMINIMAL_STACK_SIZE` | 4096 words | Generous for POSIX; reduce on target |
| `configTOTAL_HEAP_SIZE` | 1 MB | Heap_3 delegates to `malloc`; unlimited on POSIX |
| `configMAX_PRIORITIES` | 7 | |
| `configUSE_TIMERS` | 1 | Software timer task created by kernel |

These values are deliberately large for comfortable POSIX development. When porting to a
microcontroller, reduce `configMINIMAL_STACK_SIZE` and `configTOTAL_HEAP_SIZE` to match
available SRAM (see [Porting to a target](#porting-to-a-target)).

### `xcplib_rtos_cfg.h` (active when `XCPLIB_FOR_RTOS` is defined)

| Option | Value | Reason |
|---|---|---|
| `OPTION_QUEUE_32` | set | Mandatory on Cortex-M4: no 64-bit atomic operations |
| `OPTION_CLOCK_TICKS_1US` | set | `xTaskGetTickCount()`-based clock, 1 µs unit |
| `OPTION_MTU` | 1504 | 1504 − 32 = 1472 bytes, max standard UDP payload, aligned to 8 |
| `OPTION_CAL_MEM_SIZE` | 4 KB | Tune to available SRAM |
| `OPTION_DAQ_MEM_SIZE` | 4 KB | Tune to available SRAM |
| `OPTION_CAL_SEGMENT_COUNT` | 8 | Tune to number of calibration segments needed |

---

## Integrating XCPlite into a FreeRTOS application

### Step 1 — Add xcplite to your project

XCPlite ships as a CMake package. Add it as a subdirectory or install it and use `find_package`:

```cmake
add_subdirectory(path/to/XCPlite-RainerZ)  # builds xcplite static library
target_link_libraries(my_app PRIVATE xcplite freertos_kernel)
```

For a bare-metal target, also add the compile definitions:

```cmake
target_compile_definitions(xcplite PRIVATE
    _FREE_RTOS          # use FreeRTOS platform abstraction
    XCPLIB_FOR_RTOS     # use xcplib_rtos_cfg.h
)
```

### Step 2 — Provide `FreeRTOSConfig.h`

XCPlite includes `FreeRTOS.h` when `_FREE_RTOS` is defined; the FreeRTOS kernel must be able to
find your `FreeRTOSConfig.h`.  With FreeRTOS-Kernel V11+, use the recommended `freertos_config`
interface library:

```cmake
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"  # directory that contains FreeRTOSConfig.h
)
target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)
```

### Step 3 — Implement the socket layer (bare-metal)

When `_FREE_RTOS` is defined **without** `FREE_RTOS_POSIX_SIM`, the socket functions in
`platform.c` are stubs that return `true` without doing anything.  Replace them with a real
network stack implementation (e.g. **lwIP** or **FreeRTOS+TCP**) by filling in the
`#if defined(_FREE_RTOS) && !defined(FREE_RTOS_POSIX_SIM)` section in `platform.c`:

The required interface is documented in `src/platform.h` (search for `SOCKET_HANDLE`).

### Step 4 — Implement the clock (bare-metal)

The FreeRTOS clock in `platform.c` uses `xTaskGetTickCount()` which gives 1 ms granularity at
1 kHz.  For higher-resolution timestamps (sub-millisecond DAQ), replace `clockGet()` in the
`#if defined(_FREE_RTOS)` section with a hardware free-running counter, for example DWT on
Cortex-M4:

```c
// platform.c – high-resolution clock for Cortex-M4
uint64_t clockGet(void) {
    // DWT->CYCCNT counts CPU cycles; scale to microseconds
    uint64_t cycles = DWT->CYCCNT;
    uint64_t us = cycles / (SystemCoreClock / 1000000UL);
    gClockLast_ = us;
    return us;
}
```

Remember to enable the DWT counter in your startup code:
```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
```

### Step 5 — Initialise XCPlite and start tasks

```c
// Call XcpInit() and XcpEthServerInit() BEFORE vTaskStartScheduler().
// XcpEthServerInit creates two internal tasks for RX and TX.

XcpSetLogLevel(3);
XcpInit("my_project", "V1.0", XCP_MODE_LOCAL);

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



---

## Porting to a target

Checklist when moving from the POSIX simulator to a microcontroller (e.g. STM32):

- [ ] Replace `GCC_POSIX` FreeRTOS port with the correct Cortex-M4 port (`GCC/ARM_CM4F`)
- [ ] Write `FreeRTOSConfig.h` for the target clock frequency and available SRAM
- [ ] Implement the socket stub section in `platform.c` (lwIP or FreeRTOS+TCP)
- [ ] Optionally replace `clockGet()` with a hardware counter for sub-ms resolution
- [ ] Tune `OPTION_CAL_MEM_SIZE` and `OPTION_DAQ_MEM_SIZE` in `xcplib_rtos_cfg.h` to fit SRAM
- [ ] Remove or redirect `DBG_PRINT` output to a UART / ITM/SWO trace port
- [ ] Remove `FREE_RTOS_POSIX_SIM` from compile definitions (no POSIX socket bridge needed)
- [ ] Remove the `vTaskEndScheduler()` watchdog task (not available on bare-metal ports)

---

## Key files in the xcplite source tree

| File | Role |
|---|---|
| `src/xcplib_rtos_cfg.h` | XCPlite feature configuration for FreeRTOS targets |
| `src/xcplib_cfg.h` | Default feature configuration for POSIX / Windows targets |
