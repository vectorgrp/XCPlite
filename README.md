
XCPlite
28.9.2020


Raspberry Pi 4 XCP demo

Based on a very lite implementation of an ASAM standard XCP driver (1000 lines of code)
Inspired by the original free sources from Vector Informatik GmbH
List of restrictions compared to Vectors xcpBasic see xcpLite.c

Reduced Misra compliance for better readability of the source code

C and C++ measurement demo variables and code (ecu.c and ecupp.cpp)
Measure global variables and dynamic instances of structs and classes
C part compatible to Vectors XCPsim demo

Achieves 50 MByte/s UDP on a Raspberry Pi 4 with 50% CPU load
1ns measurement timestamp resolution

TCP not implemented














