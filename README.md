
XCPlite
V1.2 28.9.2020
Copyright 2020 RainerZ

Simple and light implementation of the ASAM XCP Protocol Layer V1.1 (1000 lines of code)
Inspired by the original free sources from Vector Informatik GmbH
List of restrictions compared to Vectors xcpBasic see source file xcpLite.c

Optimized for XCP on Ethernet, multi threaded, no thread lock and zero copy data acquisition
C and C++ target support

Demo visual Studio and CANape project included for Raspberry Pi 4 

C and C++ measurement demo variables and code (ecu.c and ecupp.cpp)
Measure global variables and dynamic instances of structs and classes
C part compatible to Vectors XCPsim demo

Achieves 50 MByte/s UDP on a Raspberry Pi 4 with 50% CPU load
1ns measurement timestamp resolution

TCP not implemented














