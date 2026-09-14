# freertos_emu_demo — XCPlite on FreeRTOS POSIX emulator

This example uses the **FreeRTOS POSIX simulator** port so the demo builds and runs on macOS or Linux. 
A2L generation works on Linux only,  


## Notes

The FreeRTOS POSIX simulator maps each task to a pthread and uses `SIGUSR1`/`SIGUSR2` for task
switching.  Normal POSIX APIs (BSD sockets) work transparently alongside FreeRTOS
tasks, which is why the XCPlite library works without any changes to its internal
networking code.


## Building the FreeRTOS demo

### Prerequisites

- CMake ≥ 3.14
- GCC or Clang (macOS or Linux)
- Internet access for the first configure (FreeRTOS-Kernel is downloaded via FetchContent)

### Build

Compiles xcplite with the FreeRTOS platform code paths active (_FREE_RTOS) using the POSIX simulator (FREE_RTOS_POSIX_SIM) so that the demo can be built and run on a development machine before porting to a microcontroller.
Uses Linux sockets and `clock_gettime` for the POSIX simulator



```bash
./build.sh rtos examples
./build-rtos/freertos_demo
```

Or using CMake directly:

```bash
cmake -B build-rtos -S . -DXCPLITE_CONFIGURATION=rtos -DXCPLITE_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-rtos --target freertos_demo
./build-rtos/freertos_demo
```



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




