# XCPlite Examples

This directory contains various examples demonstrating different features and capabilities of XCPlite.

## Getting Started

To get started, take a first look at the C example `hello_xcp` and then at `hello_xcp_cpp` for C++.

## Example CANape Projects

There is a CANape project for each example in a directory folder `examples/<ExampleName>/CANape`.  
To load a project into CANape, select load project and navigate to the CANape.ini file in this folder.  
All CANape project examples are configured to upload the A2L file via XCP. The IP address of the XCP server is stored in the A2L file uploaded last time. If CANape can not connect, check that the correct IP address is configured in "Device Configuration/Devices/<DeviceName>/Protocol/Transport Layer".  

The examples should run with a CANape demo version, which can be downloaded from <https://www.vector.com/de/de/support-downloads/download-center>.
The demo installation must be explicitly enabled in the installer and has some limitations:  
It will store only the first seconds of measurement data and the number of measurement signals is limited.

Note: Some of the examples use display windows without title bars to make it look cleaner. This option can be turned of with "Options/Display/View/Display Headline".  

## Example Details

### hello_xcp

An example in pure C. Compiles a C or C++. Demonstrates the basic C API.  
- Start the XCP on Ethernet server and use the runtime A2L generator.  
- Create a global calibration parameter segment structure, register the parameters in the segment and access them safely.  
- Define events for measurement of global and local (stack) variables.  
- Use the different addressing modes for measurement variables and calibration parameters.  
- Instrument a function, register local variables and function parameters and create and trigger a measurement event in the function.  

### hello_xcp_cpp

An example in C++ using more idiomatic C++ to demonstrate the capabilities of the additional C++ API.  
- Start the XCP on Ethernet server and use the runtime A2L generator.  
- Create a global calibration parameter segment structure, register the parameters in the segment and access them safely with a RAII wrapper.  
- Define events for measurement of global, local (stack), and  heap variables and instances.  
- Use the variadic C++ macro/template API.  
- Instrument a member function: Register and measure local function variables and parameters.  





### c_demo

Shows more complex data objects (structs, arrays) and calibration objects (axis, maps and curves).  
Measurement variables on stack and in global memory.  
Consistent parameter changes and measurement.  
Calibration page switching and EPK version check.  
Note: If CANAPE_24 is defined in sig_gen.hpp, the lookup table is a nested typedef, it uses a THIS. references to its shared axis contained in the typedef.

### cpp_demo

Demonstrates the calibration parameter segment RAII wrapper.  
Demonstrates measurement of member variables and stack variables in class instance member functions.  
Shows how to create a class with a calibration parameter segment as a member variable.  

### struct_demo

Shows how to define measurement variables in nested structs, multidimensional fields and arrays of structs.
Pure measurement demo, does not have any calibration parameters.

### multi_thread_demo

Shows measurement in multiple threads.  
Create thread local instances of events and measurements.  
Share a parameter segment among multiple threads.  
Thread safe and consistent access to parameters.  
Experimental code to demonstrate how to create context and spans using the XCP instrumentation API.  


![CANape Screenshot](cpp_demo/cpp_demo.png)

### no_a2l_demo

Demonstrates XCPlite without runtime A2L generation by using an A2L generation tool during the build process.  
This variant is currently limited to measurement and modification of global variables.  

### threadx_demo

Planned

### bpf_demo

Experimental, work in progress.  
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
- Use the xcplib API to create context and span, measure durations

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
 
The automatic A2L upload then happens every time a new version of A2L file has been generated.  
Depending on the settings in XCPlite, this happens after the first run of a new software build, or each time the application is restarted.  
Of course, the A2L file may also be copied manually into the CANape project folder.
