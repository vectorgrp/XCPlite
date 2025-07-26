# XCPlite

## Introduction to XCP

XCP is a measurement and calibration protocol commonly used in the automotive industry. It is an ASAM standard.  

It provides real time signal oriented data acquisition (measurement, logging) and modification of parameter constants (calibration) in a target micro controller system (ECU), to help observing and optimizing cyber physical control algorithms in real time.  
  
Timestamped events, measurement variables and parameter constants are described by an ASAM-A2L description file, another associated ASAM standard. A2L is a human readable ASCII format.  
Data objects are identified by address. In a micro controller system programmed in C or C++, these addresses are used to directly access the ECUs memory. This concept has minimum impact on the target system in terms of memory consumption, runtime and needs minimum code instrumentation. The A2l is a kind of annotated ELF Linker-Address-Map, with meta information on data instances and data types (MC specific types - lookup-tables, axis scaling, physical limits and units, conversion rules, ...).  
In a Microprocessor system developed in a system programming language like C, C++ or Rust, this concept is still usefull and efficient. Measurement signals and calibration parameters must have a static lifetime and a defined memory layout, but no predefined memory location and no static storage class. Data acquisition and modification is achieved by appropriate code instrumentation for measurement and wrapper types for groups of calibration parameters.

The ASAM-XCP standard defines a protocol and a transport layer. There are transport layers for all common communication busses used in the automotive industry, such as CAN, CAN-FD, FLEXRAY, SPI and Ethernet.  

New to XCP?  
Checkout the Vector XCP Book:  
<https://www.vector.com/int/en/know-how/protocols/xcp-measurement-and-calibration-protocol/xcp-book#>  

Visit the Virtual VectorAcademy for an E-Learning on XCP:  
<https://elearning.vector.com/>  

## XCPlite Overview

XCPlite is an implementation of XCP for Microprocessors in pure C, optimized for the XCP on Ethernet Transport Layer for TCP or UDP with jumbo frames.  
It is optimized for 64 Bit platforms with POSIX based Operating Systems, but also runs on 32 Bit platforms and on Windows with some restrictions.  
The A2L measurement and calibration object database can be generated during runtime and uploaded by the XCP client on connect.  

XCPlite is provided to test and demonstrate calibration tools such as CANape or any other XCP client implementation.  
It may serve as a base for individually customized XCP implementations on Microprocessors.  
It implements and demonstrates some techniques how to deal with variables in dynamically allocated memory and how to do measurement and calibration in multi-threaded applications.  

XCPlite is used as a C library by the implementation of XCP for Rust in:  
<https://github.com/vectorgrp/xcp-lite>  

### Whats new in XCPlite V0.9.2

- Breaking changes to V6.  
- Lockless transmit queue. Works on x86-64 strong and ARM-64 weak memory model.  
- Measurement and read access to variables on stack.  
- Supports multiple calibration segments with working and reference page and independent page switching.  
- Lock free and thread safe calibration parameter access, consistent calibration changes and page switches.  
- Refactored A2L generation macros.  
- Build as a library.  
- Used (as FFI library) for the rust xcp-lite version.  

### Features

- Supports XCP on TCP or UDP with jumbo frames.  
- Thread safe, minimal thread lock and single copy event driven, timestamped high performance and consistent data acquisition.  
- Runtime A2L database file generation and upload.  
- Prepared for PTP synchronized timestamps.  
- Supports calibration and measurement of structures.  
- User friendly code instrumentation to create calibration parameter segments, measurement variables and A2L metadata descriptions.  
- Measurement of global (static), local (stack) or heap variables and class instances.  
- Thread safe, lock-free and wait-free ECU access to calibration data.  
- Calibration page switching and consistent calibration.  
- Calibration segment persistence.  

There are other implementations of XCP available:  
XCPbasic is a free implementation for smaller Microcontrollers (even 8Bit) and optimized for CAN as Transport-Layer.  
XCPprof is a commercial product in Vectors AUTOSAR MICROSAR and CANbedded product portfolio.  

### Documentation

A description of the XCP instrumentation API is available in the doc folder.  (work in progress)

## XCPlite Examples  

hello_xcp:  
  Getting started with a simple demo in C, with minimum code and features.  
  Demonstrates basic code instrumentation to start the XCP server and how to create a calibration variable segment.  
  Defines an event for measurement of integer variables on stack and in global memory.  

c_demo:  
  Shows more complex data objects (structs, arrays), calibration objects (axis, maps and curves).  
  Measurement variables on stack and in global memory.  
  Consistent calibration changes and measurement.  
  Calibration page switching and EPK version check.  
  Note: A2lTypedefCurveComponentWithSharedAxis uses THIS. references to shared axis in typedef structures. This requires CANape24 or higher.  

struct_demo:  
  Shows how to define measurement variables in nested structs, multidimensional fields and arrays of structs

multi_thread_demo:  
  Shows measurement in multiple threads.  
  Create thread local instances of events and measurements.  
  Share a calibration parameter segment among multiple thread.  
  Thread safe and consistent access to calibration parameters.  
  Experimental code to demonstrate how to create context and spans using the XCP instrumentation API.  

cpp_demo:  
  Demonstrates the calibration segment RAII wrapper.  
  Demonstrates measurement of member variables and stack variables in class instance member functions.  
  Shows how to create a class with calibration parameters as member variables.  

![CANape Sreenshot](examples/cpp_demo/cpp_demo.png)

### XCPlite Build

Build the library and all examples:  

'''

cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build  
make --directory ./build

./build/hello_xcp.out
./build/c_demo.out
./build/cpp_demo.out
./build/multi_thread_demo.out
./build/struct_demo.out

'''

## Appendix

### Instrumentation cost and side effects

Keeping code instrumentation side effects as small as possible was one of the major goals.
  
Data Acquisition:  
  
The measurement data acquisition trigger, data transfer and calibration segment access are lock-free implementations.  
DAQ trigger needs a system call to get the current time.  
Some of the DAQ trigger macros do an event lookup by name at the first time (for the convenience not to care about event handles), and cache the result in static or thread local memory.  
The instrumentation to create events, uses a mutex lock against other simultaneous event creations.  
  
Calibration:  
  
The instrumentation to create calibration segments, use a mutex lock against other simultaneous segment creations.  
During the creation of a calibration segment, heap allocations for 3 copies of the initial page are requested (a reference, a working page and a single RCU swap page).  
Calibration segment access is thread safe and lock less. There is no more heap allocation per thread needed.  

A2L Generation:  
  
The A2L generation uses the file system. There is not need for memory. It opens 4 different files, which will be merged on A2L finalization.  
The A2l generation macros are not thread safe and don't have an underlying once pattern. It is up to the user to take care for locking and one time execution. There are helper functions and macros to make this easy.  
  
The overall concepts often relies on one time execution patterns. If this is not acceptable, the application has to take care for creating events and calibration segments in a controlled way. The A2l address generation for measurement variables on stack needs to be done once in local scope, there is no other option yet. Also the different options to create thread local instances of measurement data.  

### A2L file generation and address update options

Option 1:
The A2L file is always created during application runtime. The A2L is allowed to change on each restart of the application. It is just uploaded again by the XCP client.  
To avoid A2L changes on each restart, the creation order of events and calibration segments has to be deterministic.  
As a default, the A2L version identifier (EPK) is generated from build time and date. If the A2L file is not stable, it is up to the user to provide an EPK which reflects this.

Option 2:
The A2l file is created only once during the first run of a new build of the application.  
A copy of all calibration segments and events definitions and calibration segment data is stored in a binary .bin file to achieve the same ordering in the next application start. BIN and A2L file get a unique name based on the software version string. The EPK software version is used to check validity of the A2l and BIN file.  
The existing A2L file is provided for upload to the XCP client or may be provided to the tool by copying it.  
As a side effect, calibration segment persistency (freeze command) is supported.

Option 3:
Create the A2L file once and update it with an A2L update tool such as the CANape integrated A2L Updater or Open Source a2ltool.  
Note that currently, the usual A2L tools will only update absolute addresses for variables and instances in global memory and offsets of structure fields.  
Data acquisition of variables on stack and relative addressing, is not possible today. This might change in a future version of the A2L Updater.  

XCPlite makes intensive use of relative addressing.  
The addressing mode is indicated by the address extension:  
0 - Calibration segment (A2L MEMORY_SEGMENT) relative address, high word of the address is the calibration segment index.  
1 - Absolute address (Unsigned 32Bit, relative to main module load address).  
2 - Signed 32Bit relative address, default is relative to the stack frame pointer of the function which triggers the event.  
3 - Signed 16Bit relative address, high word of the address is the event id. This allows polling access to the variable. Used for heap and class instance member variables.

### Platform and language standard requirements and resource usage

- _Generic and declspec for A2L generation type detection
  Requires C11 and C++11

- File system: fopen, fprintf.  
  Used for A2L generation and optional calibration persistency to a binary file

- Heap allocation: malloc, free.  
  Transmit queue (XcpEthServerInit, parameter queue size).  
  DAQ table memory (XcpInit, OPTION_DAQ_MEM_SIZE in main_cfg.h).  
  Calibration segments page memory (XcpCreateCalSeg, 3 copies of the default page for working page, xcp page and RCU).  

- Atomics (C11 stdatomic.h).  
  Requires atomic_bool, atomic_uintptr_t, atomic_uint8_t, compare_exchange, fetch_sub.  
  Used for lock free queue (xcpQueue64), lock free calibration segments, DYN address mode cmd pending state, DAQ running state.  

- THREAD (Linux: pthread_create, pthread_join, pthread_cancel).  
  Used for XCP transmit and receive thread.  

- THREAD_LOCAL (C11:_Thread_local).  
  Used for the DaqEvent macros and A2L generation for per thread variable instances.  

- MUTEX (Linux: pthread_mutex_lock, pthread_mutex_unlock).  
  Used for 32 Bit Queue acquire, queue consumer incrementing the transport layer counter, thread safe creating event and calseg, thread safe lazy A2L registration.  

- SleepMs, SleepNs (Linux: nanosleep).  
  Used for receive thread polling loop.  

- Clock (Linux: clock_gettime).  
  Used as DAQ timestamp clock.  

- Sockets (Linux: socket, ...).  

### CANape known issues

- Initialize RAM (COPY_CAL_PAGE) is executed only on the first calibration segment
- GET_SEGMENT_MODE is executed multiple times on only the last calibration segment before freeze request
- Address extension of memory segment is ignored
- Request for unique address extension per DAQ list is ignored (DAQ_KEY_BYTE == DAQ_EXT_DAQ)
- CANape < V24 does not support THIS. axis references

### Suggestions for improvement

- Transport Layer counter mutex could be avoided with different counter mode
- Indicate when polling access is not possible
