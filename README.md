
# XCPlite
17.2.2021
Copyright 2021 Vector Informatik GmbH, RainerZ

Simple and light implementation of the ASAM XCP Protocol Layer V1.1 (1000 lines of code).

List of restrictions compared to Vectors xcpBasic see source file xcpLite.c.

Optimized for XCP on Ethernet (UDP), multi threaded, no thread lock and zero copy data acquisition.
C and C++ target support.

Achieves up to 80 MByte/s throughput on a Raspberry Pi 4.
3% single thread cpu time in event copy routine for 40MByte/s transfer rate. 
1us measurement timestamp resolution.

Experimental UDP on RAW socket optimization.

No A2L (ASAP2 ECU description) required. 
A A2L with reduced featureset is generated through code instrumentation during runtime on target system and automatically uploaded by XCP.

C and C++ measurement demo variables and code example in ecu.c and ecupp.cpp.
Measure global variables and dynamic instances of structs and classes.

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

Demo visual Studio and CANape project included for Raspberry Pi 4. 




## Configuration options:

All settings and parameters for the XCP protocol handler are in xcp_cfg.h

Basis compile options for the XCPlite demo are:
```
#define XCP_ENABLE_64       // Enable 64 bit platform support, otherwise assume 32 bit plattform
#define XCP_ENABLE_A2L      // Enable A2L creator and A2L upload to host
#define XCP_ENABLE_SO       // Enable measurement and calibration of shared objects
#define XCP_ENABLE_PTP      // Enable PTP synchronized DAQ time stamps

```

## Note:
CANape Linker Map Type ELF extended
Compile with -O2
Link with -lrt -lpthread

















