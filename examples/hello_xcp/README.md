# hello_xcp Demo

## Overview

Basic XCPlite example in pure C.  For C++ usage see the [hello_xcp_cpp](../hello_xcp_cpp/README.md) example.  
  
Demonstrates how to start the XCP on Ethernet server and use the runtime A2L generator.  

Shows how to create a calibration parameter segment structure, register the parameters in the segment and access them safely.  
Defines events for measurement of global and local variables.  
Demonstrates the different addressing modes for variables and parameters.  
Defines a function, registers local variables and function parameters and creates and triggers a measurement event in the function.  

More advanced topics are covered by the other examples:  

- Create a C++ application, use the C++ RAII calibration segment wrapper.  
- Safely share calibration parameters among different threads.  
- Measure instances of complex types, such a structs, arrays, nested structs and arrays of structs by using typedefs.  
- Create complex parameters, like maps, curves and lookup tables with fixed or shared axis.  
- Measure thread local instances of variables, create event instances.  
- Create physical conversion rules and enumerations.  
- Create additional groups.  
- Use consistent atomic calibration.  
- Make calibration changes persistent (freeze).  
- Create context and span, measure durations.  



## CANape Screenshot

![CANape Screenshot](CANape.png)

## A2L File

The generated A2L file can be found in the CANape project folder.  

