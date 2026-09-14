# no_a2l_demo_cpp - XCPlite example

Demonstrates XCPlite usage in C++ without runtime on-target A2L database generation.
Requires xcpclient tool or manual A2L generation from ELF file.

Please find more general information in [no_a2l_demo C version](../no_a2l_demo/README.md).
The offline A2L generation is described in [docs/OFFLINE_A2L.md](../../docs/OFFLINE_A2L.md).

## C++ Calibration Parameter Pattern Used

This example demonstrates an idiomatic C++ pattern for calibration-aware components:

- Define a calibration parameter set struct in a class (`CounterController::Params`).
- Create a static-lifetime default value instance of the parameter set.
- Register segments with `CalSegDeclRef` and inject the typed `CalSegRef` handle into the class constructor.


The static-lifetime requirement is important: calibration segment default objects must remain valid for the full program lifetime. The static-lifetime default instance initializes the calibration segment, and its address is used as the A2L instance address. For const defaults, this instance typically resides in the rodata section.

## Types and Variables with the Same Name in Different Namespaces

The namespaces `motor_control` and `valve_control` both define a struct `Input` and a global variable `input`.
The DWARF debug information only contains the unqualified names and A2L has one flat name space for typedefs, so the
ELF->A2L generator qualifies both with their namespace: the typedefs become `motor_control.Input` and `valve_control.Input`,
the instances `motor_control.input` and `valve_control.input`. Types and variables with a unique name keep their plain name.
The `XCP_COMMENT` annotations of both variables are placed inside the namespaces and use the plain name `input`,
the generator qualifies the annotation with the namespace it is placed in.

## Build and Run

```bash
# build locally 
# build both targets, no_a2l_demo and no_a2l_demo_cpp
../build.sh no_a2l
# build on remote target, upload ELF file and generate A2L from ELF file here 
# requires xcpclient tool in PATH and SSH access to target
./examples/no_a2l_demo_cpp/create_a2l.sh
```

