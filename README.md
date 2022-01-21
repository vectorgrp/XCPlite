
# XCPlite V5

Copyright 2022 Vector Informatik GmbH

Lightweight implementation of the ASAM XCP Protocol Layer V1.4 (XCPlite.c).

Supports Linux 32/64 Bit and Windows 32/64 Bit
Posix and Windows Sockets 
Windows XL-API sockets for BroadrReach available on request

List of restrictions compared to Vectors free xcpBasic and commercial xcpProf in source file xcpLite.c.

Supports only XCP on Ethernet, UDP or TCP / IPv4
C and C++ target support.

Thread safe, minimal thread lock and zero copy data acquisition (DAQ).
Achieves 100 MByte/s throughput on a Raspberry Pi 4 (jumbo frames enabled) with low cpu time in event copy routine

Simple, single calibration page example (CAL).

An A2L with reduced featureset is generated through code instrumentation during runtime on target system
and can be uploaded by XCP.

C and C++ measurement demo variables and code example in ecu.c and ecupp.cpp.
Demo how to measure global variables and dynamic instances of structs and classes.


## Code instrumentation for measurement:

Only simple code instrumentation needed for event triggering and data copy, event definition and data object definition.

Example:

### Definition:
```
  double channel1;
```

### Initialisation and A2L info generation:

```
  channel1 = 0;

  A2lCreateEvent("ECU"); // Create a new event with name "ECU""
  A2lSetEvent("ECU"); // Set event "ECU" to be associated to following measurement definitions
  A2lCreatePhysMeasurement(channel1, 2.0, 1.0, "Volt", "Demo floating point signal"); // Create a new measurement signal "channel1" with linear conversion rule (factor,offset) and unit "Volt"
```


### Measurement data acquisition event:

```
  channel1 += 0.6;
  XcpEvent(1); // Trigger event here, timestamp and copy measurement data
```

Demo visual Studio solution and CANape project included for Raspberry Pi 4 and Windows 32/64.

![CANape](Screenshot.png)




## Configuration options:

All settings and parameters for the XCP protocol and transport layer are located in xcp_cfg.h and xcptl_cfg.h
Compile options for the XCPlite demo are located in main_cfg.h:

## Notes:
- If A2L generation and upload is disabled, use CANape address update with Linker Map Type ELF extended for a.out format or PDB for .exe
- The A2L generator creates a unique file name for the A2L, for convinience use name detection (GET_ID 1)
- Linux Compile with -O2, Link with -pthread
- 64 bit version needs all objects within one 4 GByte data segment

## Version History

Version 5:
- TCP support
- "Slave" renamed to "Server""

Version 4:
- Refactoring to minimize dependencies
- All dependencies to UDP socket library in platform.h/.c
- Support for Vector XL-API removed

## Build

Source:
xcpLite.c     XCP Protocol Layer 
xcpTl.c       XCP Transport Layer XCP on Ethernet (IPv4 - TCP or UDP)
xcpServer.c   XPC implementation (Receive and Transmit Threads)
xcpAppl.c     Platform and feature specific functions (timestamp clock, address conversion, calibration page handling, ...)
clock.c       64 bit ns resolution, monotonic realtime clock (1ns since 1.1.1970)
platform.c    Operating system abtraction for Windows/Linux Mutex, Thread and Socket
A2L.c Simple  ASAM A2L file writer (reduced feature set)

main.cpp, ecupp.cpp, ecu.c     Demo application code 




### Linux x86_64

$ sudo apt-get install cmake g++ clang cmake ninja-build

#### Release Build
```
$ cd XcpLite
$ mkdir build_release
$ cd build_release
$ cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
$ ninja
```

#### Debug Build
```
$ cd XcpLite
$ mkdir build_debug
$ cd build_debug
$ cmake -GNinja ..
$ ninja
```
### Windows x86_64

Use the Visual Studio projects or CMake.

For the CMake setup, prepare your command line environment.
Set compiler to Microsoft x64 cl.exe and make sure the system finds cmake.exe and ninja.exe.
You can also use the Windows clang compiler.
```
> call "C:\Program Files (x86)\Microsoft Visual Studio 15.0\VC\Auxiliary\Build\vcvars64.bat"
> set PATH=C:\Tools\ninja;%PATH%
> set PATH=C:\Tools\cmake_3.17.2.0\bin;%PATH%
```
#### Release Build
```
> cd XcpLite
> mkdir build_release
> cd build_release
> cmake.exe -GNinja -DCMAKE_BUILD_TYPE=Release ..
> ninja.exe
```
#### Debug Build
```
> cd XcpLite
> mkdir build_debug
> cd build_debug
> cmake.exe -GNinja ..
> ninja.exe
```
