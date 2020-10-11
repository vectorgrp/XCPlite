
XCPlite
12.10.2020
Copyright 2020 RainerZ

Simple and light implementation of the ASAM XCP Protocol Layer V1.1 (1000 lines of code).
Inspired by the original free sources from Vector Informatik GmbH.

List of restrictions compared to Vectors xcpBasic see source file xcpLite.c.

Optimized for XCP on Ethernet (UDP), multi threaded, no thread lock and zero copy data acquisition.
C and C++ target support.

Experimental UDP on RAW socket optimization.

Demo visual Studio and CANape project included for Raspberry Pi 4. 

C and C++ measurement demo variables and code (ecu.c and ecupp.cpp).
Measure global variables and dynamic instances of structs and classes.

Achieves above 50 MByte/s UDP on a Raspberry Pi 4 with 50% CPU load.
Up to 1ns measurement timestamp resolution.

UDP only.

Note:
CANape Linker Map Type ELF extended
Compile with -O2
Link with -lrt -lpthread

















