
# XCPlite V1.0.0

XCPlite is a lightweight pure C implementation of the ASAM XCP V1.4 standard protocol for measurement and calibration of electronic control units.  
It supports XCP on TCP or UDP with jumboframes.  
Runs on 64 Bit platforms with POSIX based (LINUX, MACOS) or Windows Operating Systems.  
The A2L measurement and calibration object database is generated during runtime and uploaded by the XCP client on connect.

XCPlite is provided to test and demonstrate calibration tools such as CANape or any other XCP client implementation.  
It may serve as a base for individually customized XCP implementations on Microprocessors.  

XCPlite is used as a C library for the implementation of XCP for rust in:  
<https://github.com/vectorgrp/xcp-lite>  

New to XCP?  
Checkout the Vector XCP Book:  
<https://www.vector.com/int/en/know-how/protocols/xcp-measurement-and-calibration-protocol/xcp-book#>  

Visit the Virtual VectorAcedemy for an E-Learning on XCP:  
<https://elearning.vector.com/>  

## Whats new in V1.0.0

- Breaking changes to V6.  
- Lockless transmit queue. Works on x86-64 strong and ARM-64 weak memory model.  
- Measurement and read access to variables on stack.  
- Supports multiple calibration segments with working and reference page and independent page switching
- Lock free and thread safe calibration parameter access, consistent calibration changes and page switches.  
- Build as a library.  
- Used (as FFI library) for the rust xcp-lite version.  

## Features

- Supports XCP on TCP or UDP with jumbo frames.  
- Thread safe, minimal thread lock and single copy event driven, timestamped high performance and consistent data acquisition.  
- Runtime A2L database file generation and upload.  
- Prepared for PTP synchronized timestamps.  
- Supports calibration and measurement of structures
- User friendly code instrumentation to create calibration parameter segments, measurement variables and A2L metadata descriptions.  
- Measurement of global (static) or local (stack) variables.  
- Thread safe, lock-free and wait-free ECU access to calibration data.  
- Calibration page switching and consistent calibration.  

A list of restrictions compared to Vectors free XCPbasic or commercial XCPprof may be found in the source file xcpLite.c.  
XCPbasic is an optimized implementation for smaller Microcontrollers and with CAN as Transport-Layer.
XCPprof is a product in Vectors AUTOSAR MICROSAR and CANbedded product portfolio.  

## Examples  

hello_xcp:  
  Getting started with a simple demo in C with minimum code and features.  
  Shows the basics how to integrate XCP in an existing application.  
  Create an event and measure an integer variable on stack and in global memory

c_demo:  
  Shows more complex data objects (structs, arrays), calibration objects (axis, maps and curves).  
  Measurement variables on stack and in global memory
  Consistent calibration changes and measurement.  
  Calibration page switching and EPK version check.  
  Note: A2lTypedefCurveComponentWithSharedAxis uses THIS. references to shared axis in typedef structures. This requires CANape24 or higher
  
struct_demo
  Shows how to define types for nested structs, array struct components and arrays of structs

multi_thread_demo
  Shows measurement in multiple thread
  Create thread local instances of events and measurements
  Share a calibration parameter segment among the threads
  Access to calibration parameters is thread safe and consistent

## Build

Be sure EPK is updated for a new build.
EPK is generated from __DATE__ and __TIME__ in a2l.h

Build the library and all examples:

'''

cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build  
touch src/a2l.c
make --directory ./build

./build/hello_xcp.out
./build/c_demo.out
./build/multi_thread_demo.out
./build/struct_demo.out

'''

## A2L file generation options

As default the A2L file is created during runtime and provided for upload to the XCP client.  
The A2L file is always up to date with correct address information.  

Another option is, to create the A2L file once and update it with an A2L update tool such as the CANape integrated A2L Updater or Open Source a2ltool.  
Note that currently, the A2L tools will only update absolute addresses for variables and instances in global memory.  

XCPlite makes intensive use of relative addressing.  
This is indicated by the address extension:  
0 - Calibration segment (A2L MEMORY_SEGMENT) relative address, high word of the address is the calibration segment index
1 - Absolute address (relative to main module load address)
2 - Signed 32Bit relative address, default is relative to the stack frame pointer of the function which triggers the event  
3 - Signed 16Bit relative address, high word of the address is the event id. This allows polling access for the variable

Future versions of the A2L updaters might support these addressing schemes.  

'''

../../xcp-lite-rdm/target/debug/examples/a2l_tool
../../xcp-lite-rdm/target/debug/examples/a2l_tool  --check --strict -a C_Demo.a2l

a2ltool c_demo.a2l --elffile c_demo.out --update --output c_demo_updated.a2l
a2ltool c_demo.a2l --enable-structures --update-mode PRESERVE  --elffile c_demo.out --update --output c_demo_updated.a2l
a2ltool --create -e c_demo.out    --measurement-regex "counter" --measurement-regex "params" --output c_demo.a2l
'''
