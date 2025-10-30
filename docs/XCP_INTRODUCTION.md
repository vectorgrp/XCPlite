# Introduction to XCP

## What is XCP?

XCP is a measurement and parameter tuning (calibration) protocol commonly used in the automotive industry. It is an ASAM standard.

It provides real time signal oriented data acquisition (measurement, logging) and tuning of parameter constants (calibration) in a target micro controller system (ECU), to help observing and optimizing cyber physical control algorithms in real time.

## How XCP Works

Timestamped events, measurement variables and parameter constants are described by an ASAM-A2L description file, another associated ASAM standard. A2L is a human readable ASCII format.

In a micro controller system programmed in C or C++, measurement data items are directly accessed in their original memory locations. This concept has minimum impact on the target system in terms of memory consumption, runtime and needs minimum code instrumentation. The A2l is a kind of annotated ELF Linker-Address-Map, with meta information on data instances and data types (MC specific types - lookup-tables, axis scaling, physical limits and units, conversion rules, ...).

In a Microprocessor system developed in a system programming language like C, C++ or Rust, this concept is still useful and efficient. Measurement signals and calibration parameters usually have a static lifetime and a defined memory layout, but no predefined memory location and are not limited to static storage classes. Data acquisition and modification is achieved by appropriate code instrumentation for measurement and wrapper types for groups of calibration parameters.

## XCP from a Developer Perspective

From a software developer perspective, XCP may be considered to be a high-frequency application level tracing solution, using statically instrumented trace points with configurable associated data. Tracing can be started, stopped and reconfigured during runtime. What is not configured, does not consume bandwidth, memory and other resources. The acquired context data is always consistent and trace events are precisely time stamped. Data types and instances of available context data items are defined as code or obtained by a XCP tool from ELF/DWARF debug information. Data instances may be in global, local, thread local and heap storage locations. In addition to that, XCP provides the capability to modify application variables and state in a thread safe and consistent way.

## XCP Protocol and Transport Layers

The ASAM-XCP standard defines a protocol and a transport layer. There are transport layers for all common communication busses used in the automotive industry, such as CAN, CAN-FD, FLEXRAY, SPI and Ethernet.

## Learning Resources

New to XCP?

**Vector XCP Book:**  
<https://www.vector.com/int/en/know-how/protocols/xcp-measurement-and-calibration-protocol/xcp-book#>

**Virtual VectorAcademy E-Learning:**  
<https://elearning.vector.com/>

## Other XCP Implementations

There are other implementations of XCP available:

- **XCPbasic** - A free implementation for smaller Microcontrollers (even 8Bit) and optimized for CAN as Transport-Layer
- **XCPprof** - A commercial product in Vector's AUTOSAR MICROSAR and CANbedded product portfolio
