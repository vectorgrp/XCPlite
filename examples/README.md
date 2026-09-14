# XCPlite Examples

This folder contains various examples demonstrating different features and capabilities of XCPlite.  

## Getting Started

To get started, take a first look at the C example `hello_xcp` or at `hello_xcp_cpp` for C++.


### Example CANape Projects

There is a CANape project for each example in a directory folder `<ExampleFolder>/CANape`.  
To load a project into CANape, select `load project` and navigate to the file `CANape.ini` in the "project" folder.  
All CANape project examples (except no_a2l and free_rtos demos) are configured to provide downloading the A2L file from target ECU via XCP upload commands (Note: XCP has unusual naming conventions, the target ECU technically is the UDP/TCP server, but the commands to read data from target are called "upload").  
The IP address of the XCP server may be cached by CANape or stored in the A2L file. If CANape can not connect, check that the correct IP address is configured in "Device Configuration/Devices/<DeviceName>/Protocol/Transport Layer".  

The examples should run with a CANape demo version, which can be downloaded from <https://www.vector.com/de/de/support-downloads/download-center>. The demo installation must be explicitly enabled in the installer and has some limitations: It will store only the first seconds of measurement data and the number of measurement signals is limited.



## Examples

Note that examples targets may need different XCPlite library build configurations. There are different build directories for each library configuration used: 

```bash
   cmake -B build        -S . -DXCPLITE_CONFIGURATION=default   # (or omit for default)
   cmake -B build-no_a2l -S . -DXCPLITE_CONFIGURATION=no_a2l    # for no_a2l_demo and no_a2l_demo_cpp
   cmake -B build-ptp    -S . -DXCPLITE_CONFIGURATION=ptp       # for ptp4l_demo
   cmake -B build-shm    -S . -DXCPLITE_CONFIGURATION=shm       # for silkit_demo
   cmake -B build-rtos   -S . -DXCPLITE_CONFIGURATION=rtos      # for freertos_demo with the FreeRTOS POSIX simulator
   cmake -B build-raw    -S . -DXCPLITE_CONFIGURATION=raw       # for udp_raw_demo with the raw Ethernet transport
```


### [hello_xcp](hello_xcp/README.md)

An example in pure C. Compiles a C or C++. Demonstrates the basic C API.  
- Start the XCP on Ethernet server and use the runtime A2L generator.  
- Create a global calibration parameter segment structure, register the parameters in the segment and access them safely.  
- Define events for measurement of global and local (stack) variables.  
- Use the different addressing modes for measurement variables and calibration parameters.  
- Instrument a function, register local variables and function parameters and create and trigger a measurement event in the function.  


### [hello_xcp_cpp](hello_xcp_cpp/README.md)

An example in C++ using more idiomatic C++ to demonstrate the capabilities of the additional C++ API.  
- Start the XCP on Ethernet server and use the runtime A2L generator.  
- Create a global calibration parameter segment structure, register the parameters in the segment and access them safely with a RAII wrapper.  
- Define events for measurement of global, local (stack), and  heap variables and instances.  
- Use the variadic C++ macro/template API.  
- Instrument a member function: Register and measure local function variables and parameters.  


### [no_a2l_demo](no_a2l_demo/README.md)

Demonstrates using XCPlite without on-target runtime A2L generation.  
Offline A2L generation is performed using the XCPlite specific A2L generator/creator tool (built into xcpclient) during the build process and ELF/DWARF linker file informations.
Does not need file system support on the target and A2L upload.  

### [no_a2l_demo_cpp](no_a2l_demo_cpp/README.md)

C++ version of no_a2l_demo.  
    

### [freertos_demo](freertos_demo/README.md)

Demonstrates XCPlite running inside FreeRTOS tasks on 32 bit microcontrollers.  
Examples for STM32 and ESP32 microcontrollers, and the FreeRTOS POSIX simulator on Linux.  
Shows how to use XCPlite in a real-time operating system with multiple tasks, and how to share parameters among tasks.  
Uses the offline A2L generator tool (xcpclient).    


### [silkit_demo](silkit_demo/README.md)

Demonstrates the use of XCPlite in a SILKIT simulation with multiple participants.
Builds against a pre-built libxcplite and silkit library


### [ptp4l_demo](ptp4l_demo/README.md)

Demonstrates how to use a PTP (Precision Time Protocol) synchronized clock as XCP data acquisition timestamp source.  


### [udp_raw_demo](udp_raw_demo/README.md)

Demonstrates XCP on UDP/IPv4 without any TCP/IP stack: 
- Transport is implemented inside xcplib on top of a thin raw Ethernet HAL.  
- For targets with no IP stack at all: a bare metal EMAC driver, or an RTOS Ethernet abstraction without lwIP.  
- xcplib answers ARP and ICMP itself, so the target is pingable without a stack.  
- Needs an explicit local IPv4 address, there is no DHCP and `0.0.0.0` (ANY) has no meaning.  
- Linux and FreeRTOS only, the HAL backend uses `AF_PACKET` and requires `CAP_NET_RAW`. See [docs/SOCKET_RAW.md](../docs/SOCKET_RAW.md).  
- ASAM CMP driver planned, for all operating systems.


### [cmp_demo](cmp_demo/README.md)

Demonstrates supplying your own Ethernet HAL backend from outside the library: 
- A standalone project consuming an installed xcplite, like external_example.  
- Prepares XCP over ASAM CMP, for testing XCP tools which communicate through capture modules.  
- Implements the six `eth_hal_*` functions of `src/socket_raw_hal.h` in the application; the built in AF_PACKET backend is then never pulled from the static library.  
- The CMP envelope itself is not implemented yet and is a pass through, so the HAL plumbing is testable on its own.  
- Linux only, needs `CAP_NET_RAW`. Not built from the root CMakeLists.  


### [external_example](external_example/README.md)

Demonstrates using libxcplite as a pre-built external library:
- Independent from the main build system.  
- Shows how to build against an installed libxcplite binary (system-wide or local staging).  
- Independent CMakeLists.txt using `find_package(libxcplite)`.  
- Typical workflow for production deployments where libxcplite is distributed as a library package.  
- No system installation required for development - uses local staging directory.  


### [fetchcontent_example](fetchcontent_example/README.md)

Demonstrates building libxcplite from source on Github as part of your own project via CMake `FetchContent`: 
- No install step.  
- Independent CMakeLists.txt with `FetchContent_Declare(xcplite GIT_REPOSITORY ... GIT_TAG ...)`, pinned to a release tag.  
- Same application code and the same `xcplite::xcplite` link target as external_example.  
- Typical workflow for CI builds, cross-compiling and projects which want the xcplite version pinned in code.  


### [c_demo](c_demo/README.md)

Shows more complex data objects (structs, arrays) and calibration objects (axis, maps and curves).  
Measurement variables on stack and in global memory. 
Asynchronous read (polling) and write to a value on the stack (variable: counter) 
Consistent atomic changes of multiple calibration parameters.  
Calibration page switching and EPK version check.  


### [cpp_demo](cpp_demo/README.md)

Demonstrates the calibration parameter segment RAII wrapper.  
Demonstrates measurement of member variables and stack variables in class instance member functions.  
Shows how to create a class with a calibration parameter segment as a member variable.  
Note: If CANAPE_24 is defined in sig_gen.hpp, the lookup table is a nested typedef, it uses a THIS. references to its shared axis contained in the typedef.


### [struct_demo](struct_demo/README.md)

Shows how to define measurement variables in nested structs, multidimensional fields and arrays of structs.
Pure measurement demo, does not have any calibration parameters.


### [multi_thread_demo](multi_thread_demo/README.md)

Shows measurement in multiple threads.  
Create thread local instances of events and measurements.  
Share a parameter segment among multiple threads.  
Thread safe and consistent access to parameters.  
Experimental code to demonstrate how to create context and spans using the XCP instrumentation API.  


### [point_cloud_demo](point_cloud_demo/README.md)

Demonstrates how to visualize dynamic data structures in the CANape 3D scene window.  
Creates and measures an array of structures representing 3D points with additional information.


### [bpf_demo](bpf_demo/README.md)

Experimental.  
Demonstrates tracing of process creations and selected syscalls.  




## Advanced Topics Covered

The examples demonstrate various advanced topics:

- Safely share parameters among different threads
- Measure instances of complex types, such as structs, arrays, nested structs and arrays of structs by using typedefs
- Create complex parameters, like maps, curves and lookup tables with fixed or shared axis
- Measure thread local instances of variables, create event instances
- Create physical conversion rules and enumerations
- Create additional groups
- Use consistent atomic parameter modification
- Make parameter changes persistent (freeze)
- Use the API to create context and span, measure durations




## Building a CANape Project from Scratch

How to create a new CANape project from scratch is described in the CANape help under "Creating a project and configuration (quick guide)".  

The easiest way to create a new CANape project for XCPlite is:  

- Create a new project in 'Backstage/Project/New'.  
- Drag&Drop the A2L file generated by XCPlite on the CANape desktop and step through the pop up dialog pages:  
    Select XCP, Ethernet and LocalPC:ETH to use the local PCs ethernet adapter.  
    All other settings may stay on default.  
- CANape should now be already connected.  
    If not, check the Ethernet settings in 'Device/DeviceConfiguration/Devices/MyDevice/Protocol/TransportLayer'.  
- To configure CANape for automatic upload of the A2L file, a few more settings have to be modified once in the new project:  
    In 'Device/DeviceConfiguration/Devices/MyDevice/Database', enable automatic detection of database content and select 'Check identifier and content'.
- To use the consistent calibration mode (indirect calibrationmode), the user defined XCP command for start and end calibration sequence have to be configured. This setting is not default in CANape. Refer to one of the example projects for details.
 
The automatic A2L upload from target happens every time a new version of A2L file has been generated on target.  
Depending on the settings in XCPlite, this happens after the first run of a new software build, or each time the application is restarted.  
Of course, the A2L file may also be copied manually into the CANape project folder.



## API usage overview

The XCPlite public API is spread across four headers: `xcplib.h` (C), `xcplib.hpp` (C++),
`a2l.h` (C/C++ A2L generation), and `a2l.hpp` (C++ A2L helpers). The table below shows which
API areas each example exercises.

**Legend:** ✅ used  —  — not used  *(blank = not applicable)*

| API area | hello_xcp | hello_xcp_cpp | c_demo | cpp_demo | struct_demo | multi_thread_demo | no_a2l_demo | freertos_demo |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Language** | C | C++ | C | C++ | C | C | C | C |
| **Server** | | | | | | | | |
| `XcpInit` / `XcpEthServerInit` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `XcpSetElfName` (ELF upload) | ✅ | ✅ | — | — | — | — | ✅ | ✅ |
| **Calibration — C macros** | | | | | | | | |
| `CalSegDecl` / `CalSegCreate` | ✅ | — | ✅ | — | — | ✅ | ✅ | ✅ |
| `CalSegLock` / `CalSegUnlock` | ✅ | — | ✅ | — | — | ✅ | ✅ | ✅ |
| `XcpFreeze` / `XcpBinWrite` | — | — | ✅ | — | — | — | — | — |
| **Calibration — C++ RAII** | | | | | | | | |
| `CalSegDeclRef` / `CalSegRef<T>` (section-registered) | — | — | — | — | — | — | ✅ | ✅ |
| `xcp::CalSeg<T>` (runtime-created, with A2L) | — | ✅ | — | ✅ | — | — | — | — |
| `CalSeg::lock()` / `CalSegRef::lock()` / auto unlock | — | ✅ | — | ✅ | — | — | ✅ | ✅ |
| **Events & measurement triggers** | | | | | | | | |
| `DaqCreateEvent` | ✅ | ✅ | ✅ | ✅ | ✅ | — | ✅ | ✅ |
| `DaqCreateEventInstance` (thread-local) | — | — | — | — | — | ✅ | — | — |
| `DaqCreateAndTriggerEvent` (inline) | — | — | — | — | — | — | ✅ | — |
| `DaqTriggerEvent` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `DaqTriggerEventExt` (relative base addr) | — | — | — | — | ✅ | — | ✅ | — |
| `DaqEventVar` (C+A2L / C++ combined create+trigger) | — | — | — | ✅ | — | — | — | — |
| `DaqEventExtVar` / `DaqTriggerEventVar` (C++ variadic) | — | ✅ | — | ✅ | — | — | — | — |
| **A2L — measurement registration** | | | | | | | | |
| `A2lCreateMeasurement` (scalar) | ✅ | — | ✅ | ✅ | ✅ | ✅ | — | — |
| `A2lCreateMeasurementArray` / `Matrix` | — | — | ✅ | ✅ | — | ✅ | — | — |
| `A2lCreateMeasurementInstance` (named) | ✅ | — | — | ✅ | — | ✅ | — | — |
| `A2lCreatePhysMeasurement` (with unit/conv) | ✅ | — | — | ✅ | — | ✅ | — | — |
| **A2L — calibration parameter registration** | | | | | | | | |
| `A2lCreateParameter` (scalar) | ✅ | — | ✅ | — | — | ✅ | — | — |
| `A2lCreateCurve` / `A2lCreateMap` | — | — | ✅ | — | — | — | — | — |
| `A2lCreateCurveWithSharedAxis` / `MapWithSharedAxis` | — | — | — | ✅ | — | — | — | — |
| `A2lCreateAxis` | — | — | ✅ | — | — | — | — | — |
| **A2L — typedef type system** | | | | | | | | |
| `A2lTypedefBegin` / `A2lTypedefEnd` | — | ✅ | ✅ | ✅ | ✅ | ✅ | — | — |
| `A2lTypedefMeasurementComponent` | — | — | — | — | ✅ | ✅ | — | — |
| `A2lTypedefParameterComponent` | — | — | — | ✅ | — | — | — | — |
| `A2lTypedefCurveComponent` / `AxisComponent` | — | — | — | ✅ | — | — | — | — |
| `A2lCreateTypedefInstance` / `InstanceArray` | — | — | ✅ | ✅ | ✅ | ✅ | — | — |
| `A2lCreateTypedefReference` (pointer/heap) | — | — | — | — | ✅ | — | — | — |
| **A2L — conversions and metadata** | | | | | | | | |
| `A2lCreateLinearConversion` | ✅ | — | — | ✅ | — | — | — | — |
| `A2lCreateEnumConversion` | — | — | — | ✅ | — | — | — | — |
| **A2L — addressing modes** | | | | | | | | |
| `A2lSetAbsoluteAddrMode` | ✅ | — | ✅ | ✅ | ✅ | — | — | — |
| `A2lSetRelativeAddrMode` (heap/instance base) | — | ✅ | — | ✅ | ✅ | ✅ | — | — |
| `A2lSetStackAddrMode` (stack variables) | ✅ | — | ✅ | ✅ | ✅ | ✅ | — | — |
| `A2lSetSegmentAddrMode` (CalSeg base) | ✅ | — | ✅ | — | — | ✅ | — | — |
| **On-target A2L generation** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | **—** | — |
| **Offline A2L via xcpclient + ELF** | — | — | — | — | — | — | **✅** | ✅ |

> **no_a2l_demo** and **freertos_demo** use only the XCP measurement/calibration core
> (`DaqCreateEvent`, `DaqTriggerEvent`, `CalSegDecl`, `CalSegLock`/`Unlock`) — no `a2l.h`
> calls. The A2L file is generated offline from the ELF/DWARF debug info by `xcpclient`.
> See [no_a2l_demo/README.md](no_a2l_demo/README.md) for the full offline workflow.

