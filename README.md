
# XCPlite V2

Copyright 2021 Vector Informatik GmbH

Lightweight implementation of the ASAM XCP Protocol Layer V1.4 (1000 lines of code in XCPlite.c).

Demos for Linux and Windows (Winsock and XL-API V3 (VN5xxx) with simple buildin UDP stack)

List of restrictions compared to Vectors xcpBasic and xcpProf in source file xcpLite.c.

Optimized for XCP on Ethernet (UDP), thread safe, minimal thread lock and zero copy data acquisition.
C and C++ target support.

Achieves up to 80 MByte/s throughput on a Raspberry Pi 4, more than 100 MByte/s on a PC (option -jumbo).
3% single thread raspi cpu time in event copy routine for 40MByte/s transfer rate. 
1us measurement timestamp resolution.

No A2L (ASAP2 ECU description) required. 
A A2L with reduced featureset is generated through code instrumentation during runtime on target system 
and automatically uploaded by XCP (option -a2l).

C and C++ measurement demo variables and code example in ecu.c and ecupp.cpp.
Demo how to measure global variables and dynamic instances of structs and classes.

## Code instrumentation:

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

Compile options for the XCPlite demo are main.h:
```
#define XCPSIM_ENABLE_A2L_GEN  // Enable A2L creator and A2L upload to host
#define XCPSIM_ENABLE_XLAPI_V3  // Enable Vector XL-API stack (option -v3)
```

## Notes:
- If A2L generation and upload is disabled, use CANape address update with Linker Map Type ELF extended for a.out format or PDB for .exe 
- The A2L generator creates a unique file name for the A2L, for convinience use name detection (GET_ID 1) 
- Linux Compile with -O2, Link with -pthread
- Jumbo frames are disables by default
- 64 bit version needs all objects within on 4 GByte data segment  
- Multicast time syncronisation, multicast device detection, PTP master clock emulation, TCP transport layer may be future features


















