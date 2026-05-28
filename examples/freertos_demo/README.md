# freertos_demo — XCPlite on FreeRTOS

This example demonstrates XCPlite (XCP measurement and calibration) running inside FreeRTOS tasks.
It uses the **FreeRTOS POSIX simulator** port so the demo builds and runs on macOS or Linux before
switching to a real embedded target such as an STM32.

---

## Overview

### What it shows

| Feature | How it is demonstrated |
|---|---|
| XCP DAQ measurement | `DaqCreateEvent` / `DaqTriggerEvent` inside tasks |
| XCP calibration | `CalSegDecl` / `CalSegCreate`, `CalSegLock` / `CalSegUnlock` |
| XCP server initialization | `XcpInit` `XcpEthServerInit` called before the FreeRTOS scheduler starts |

### Architecture

```
main()
├── XcpInit()
├── XcpEthServerInit()                ← two FreeRTOS threads for XCP TX/RX created here
├── xTaskCreate(measurementTask1)     ← 1 ms DAQ task
├── xTaskCreate(measurementTask2)     ← 10 ms DAQ task
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
| `FreeRTOSConfig.h` | FreeRTOS kernel configuration (features, tick rate, heap, priorities) |
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
cmake -B build_freertos -S . -DXCPLITE_BUILD_FREERTOS_DEMO=ON -DCMAKE_BUILD_TYPE=Debug --fresh
cmake --build build_freertos --target freertos_demo
./build_freertos/examples/freertos_demo/freertos_demo
```


### Create an A2L file


```bash
xcpclient --offline   --elf ./examples/freertos_demo/CANape_Project/freertos_demo --create-a2l  --a2l ./examples/freertos_demo/CANape_Project/freertos_demo.a2l
```

For more details and options on the A2L file generation, see no_a2l_demo and the xcpclient documentation.  

---


### Connection test

Do a test measurment.  
Visualize the counter variables in task1 and task2 with the generated A2L file.  

```bash
xcpclient --udp --dest-addr 192.168.0.206:5555   --a2l ./examples/freertos_demo/CANape_Project/freertos_demo.a2l  --mea counter --verbose 2
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

### `xcplib_rtos_cfg.h` 

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
add_subdirectory(path/to/XCPlite)  # builds xcplite static library
target_link_libraries(my_app PRIVATE xcplite freertos_kernel)
```

For a bare-metal target, also add the compile definitions:

```cmake
target_compile_definitions(xcplite PRIVATE
    _FREE_RTOS                                      # use FreeRTOS specific code paths and platform abstraction
    "XCPLIB_CFG_OVERRIDE=\"xcplib_rtos_cfg.h\"".    # apply FreeRTOS-specific configuration overrides
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
`platform.c` are stubs that return `true` without doing anything.  Replace them with a real
network stack implementation (e.g. **lwIP** or **FreeRTOS+TCP**) by filling in the
`#if defined(_FREE_RTOS) && !defined(FREE_RTOS_POSIX_SIM)` section in `platform.c`:

The required interface is documented in `src/platform.h` (search for `SOCKET_HANDLE`).


### Step 4 — Implement the clock (bare-metal)

The FreeRTOS clock in `platform.c` uses `xTaskGetTickCount()` which gives 1 ms granularity at
1 kHz.  
For 1us high-resolution timestamps, replace `clockGet()` in the `#if defined(_FREE_RTOS)` section with a hardware free-running counter.  

Other clock resultions than 1us or 1ns are possible, but this requires to adapt the XCP protocol layer settings in xcp_cfg.h.  


For example DWT on Cortex-M4:

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
XcpCreateEpk("V1.0");

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

## Porting freertos_demo to a different target

Checklist when moving from the POSIX simulator to a microcontroller:

- [ ] Replace `GCC_POSIX` FreeRTOS port with the correct port
- [ ] Write `FreeRTOSConfig.h` for the target clock frequency and available SRAM
- [ ] Implement the socket stub section in `platform.c` (lwIP or FreeRTOS+TCP)
- [ ] Optionally replace `clockGet()` with a hardware counter for 1us or 1ns resolution
- [ ] Tune `OPTION_CAL_MEM_SIZE` and `OPTION_DAQ_MEM_SIZE` in `xcplib_rtos_cfg.h` to your needs and available SRAM
- [ ] Remove or redirect `DBG_PRINT` output to a UART / ITM/SWO trace port
- [ ] Remove `FREE_RTOS_POSIX_SIM` from compile definitions (no POSIX socket bridge needed)
- [ ] Remove the `vTaskEndScheduler()` watchdog task (not available on bare-metal ports)
- [ ] Check required stack size for the XCP rx and tx task.  
- [ ] Check atomics and mutex implementation for performance on your target.  

Note:
Calibration is lockfree based on atomics.  
The data acquisition queue is protected by a mutex.
XCP creates 2 tasks/threads for RX and TX via .  


---

## Key files in the xcplite source tree

| File | Role |
|---|---|
| `src/xcplib_rtos_cfg.h` | XCPlite feature configuration for FreeRTOS targets |
| `src/xcplib_cfg.h` | Default feature configuration for POSIX / Windows targets |
| `src/xcp_cfg.h` | Default configuration for the XCP protocol layer |
| `src/xcptl_cfg.h` | Default configuration for the XCP ethernet transport layer |



### TODO List and open issues

- Improve and complete C++ support 
