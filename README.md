
XCPlite
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

Only simple code instrumentation needed for event triggering and data copy, event definition and data object definition.

Example:

Definition:

	double channel1 = 0;

Initialisation:

  A2lCreateEvent("ECU"); // Create event

  channel = 0;

  A2lSetEvent("ECU"); // Define event ....

  A2lCreatePhysMeasurement(channel1, 1.0, 1.0, "Volt", "Demo floating point signal"); // Create signal


Measurement:

  channel1 += 0.6;

  XcpEvent(1); // Trigger event and copy measurement data


Demo visual Studio and CANape project included for Raspberry Pi 4. 


Note:
CANape Linker Map Type ELF extended
Compile with -O2
Link with -lrt -lpthread

















