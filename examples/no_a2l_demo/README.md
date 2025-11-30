# no_a2l_demo Demo

Demonstrates XCPlite operation without runtime A2L generation.  
This is experimental and work in progress.  
The A2L creator and ELF/DWARF reader are quick and dirty code, partly AI generated, partly taken from DanielT a2ltool and from the VectorGrp Rust xcp-lite MC registry and A2L writer.  
Not ready for production.  
This is could be be the base for the planned ThreadX version of XCPlite.  

## The Build Time A2L Generation Concept
  
### Current State/* Achievements

Needs a XCPlite specific A2L writer and an ELF/DWARF reader in a separate tool at build time to:  

Step 1: A2L template generation:

- Create an A2L template from ELF by detecting segments and events
    Event and segment numbers are sequentially allocated an may not have the correct number

Step 2: AL2 content generation:

- Add parameters
    The reference pages of all calibration parameters must be in addressable (4 GB - 32bit) global memory (.bss segment must be in this range)
    Parameters must be in a structure, a calibration segment contains a single structure to assure a defined memory area and layout
    Detect calibration parameters by the address of their reference page by naming convention and segment marker variable
    XCP needs to be configure for absolute calibration segment addressing
- Add measurements
    Global or static measurements must be in addressable (4 GB - 32bit) global memory (.bss segment must be in this range)
    Takes all global, static and local variables into account in specified compilation units
    Try to detect an appropriate fixed event for each variable by detecting a event trigger in the same function, if not use the unsafe standard async event as default event
- Add all types required as TYPEDEF_STRUCTURE

Content generation step 2 can alternatively be done by hand, with any other A2L tool from Vector or open source

Step 3: A2L Fix:

- The remaining problem is the wrong event and segment numbers. The A2L file is consistent and complete, but the numbers do not match the runtime numbers
  This can be fixed connecting to the target and querying segments and events
  CANape already does this for events, but not for segments
  The associating is done by namme
  Problem is, that the XCP standard command GET_SEGMENT_INFO does not support segment names, only numbers
  The is a small extension in XCPlite, which supports segment name query

  The proposal is, to simply integrate this into CANape and we are done ....

  Of course the whole process could be integrated into CANape, which would eliminate the need for an A2L file at all!

### TODO List

- The xcp_client tool is just proof of concept
    Make it more flexible by adding a regular expression filter to the xcp_client tool, allow to specify a list of compilation units, ...
- Support relative calibration segment addressing
    XCPlite can then be configured for relative calibration segment addressing as an option
    As long as all reference pages are in a 4 GB addressable range, there is no benefit of relative addressing
    By convention, parameters always use address extension 0 ACFDD or CAFDD
    Currently we preliminary use AAFDD, because the XCPlite macros for event triggering so not detect the calibration segment addressing mode and CANape can not handle ACFDD
- Free standing parameters not in calibration segments
    Not implemented yet, would probably need code macro annotations to detect them
- Make sure the event trigger location and the variable location have the same CFA (have not seen any violations yet)
- Heap measurement variables
    The A2L creator can not handle heap variables yet
    Needs to detect trg__AAS or trg__AASD type and analyze the argument type of DaqTriggerEvent(), pointer to type
- Thread local variables
    The A2L creator can not handle thread local variables yet
    The DAQ capture method does not work for TLS, need a ApplXcpGetTlsBaseAddress() function, maybe introduce AAST type
    Detect the base address of the TLS block, like it is done in ApplXcpGetBaseAddr()/xcp_get_base_addr() for the global variables
    The DaqCapture macros as an alternative, does not work yet
- EPK
    Detect if the target application has a EPK segment or not
    Currently not EPK segment is generated, switched off in XCPlite
- Function parameters
    Define a macro to declare function parameters as XCP_MEA, which sills them to stack
    A2L Creator ELF reader parser must detect the function parameters with the CFA offset in the stack frame
- bool
    The A2L creator must create a BOOL conversion rule and detect the size of the bool type
- Nested structures
    Should work, if Daniel handles it correct, not tested yet
- Arrays of structures
    Should work, if Daniel handles it correct, not tested yet
- Multiple source files not tested yet
- Support for C++, name spaces, classes, member functions, ...

## Using the xcp_client tool

The xcp_client tool from Rust xcp-lite can generate A2L file templates for XCPlite, for further processing with other tools.  
It can update A2L files with all visible measurement and calibration variables.  
To generate the correct (safe) event id and calibration segment number for the XCP protocol, the application can be run on the target to enable xcp_client to upload event and calibration segment information via XCP.  
CANape currently only detects and corrects event ids, but not calibration segment numbers !!!

Example: Create an A2L template from target:  
(Note that the tool will connect to the target ECU to get event id and calibration memory segment number information)

```bash
$xcp_client   --dest-addr=$TARGET_HOST:5555 --tcp --create-a2l --a2l no_a2l_demo.a2l
```

Example: Create an A2L template from binary file with debug information:  
(Note that the protocol information is only needed, to store it in the A2L file, the A2L file will be consistent, but the event ids and calibration memory segment numbers may be wrong!)

```bash
$xcp_client   --dest-addr=$TARGET_HOST:5555 --tcp --offline --elf no_a2l_demo.out --create-a2l --a2l no_a2l_demo.a2l
```

Example: Create measurement and calibration variables from a binary file with ELF/DWARF debug information and add variable from specified compilation units filtered by a regular expression

```bash
$xcp_client   --dest-addr=$TARGET_HOST:5555 --tcp --offline --elf no_a2l_demo.out  --a2l no_a2l_demo.a2l
```

Refer to the xcp_client command line parameter description for details.  

The A2L file template can now be modified, extended and updated with CANape or any other A2L tool.
This approach works for global measurement variables and calibration parameters.  
XCPlite must be configured for absolute calibration segment addressing (OPTION_CAL_SEGMENTS_ABS).  

To make local variables visible, they have to live on stack which is usually not true with optimized code.
There is a macro XCP_MEA to spill local variables to stack with minimal runtime overhead.

Example:

```bash
XCP_MEA uint8_t counter = 0;
```

.

### Using Vector CANape

Drop the template generated by xcp_client into CANape and create a new XCP on Ethernet device.  
Enable access to the ELF file in the device configuration.  
Use the A2L editor to add individual measurement parameters.  
For calibration create an calibration instance for the complete calibration parameter struct.

### Using Vector A2L-Toolset A2L-Creator to add measurement and calibration metadata

The example code contains some A2L creator metadata annotation to add metadata such as calibration variable limits and physical units

### Using Open Source a2ltool

Example:
Add the calibration segment 'params' and the measurment variable 'counter' to the A2L template:

```bash
a2ltool  --update --measurement-regex "counter"  --characteristic-regex "params" --elffile  no_a2l_demo.out  --enable-structures --output no_a2l_demo.out no_a2l_demo.out 
```

### Demo

The demo script create_a2l.sh automates the complete remote build and A2L generation process.

```bash
./create_a2l.sh
```

### Work in progress

This is currently experimental.  
The approach will be used for the upcoming ThreadX demo with build time A2L generation.

objdump -W no_a2l_demo.out
objdump --all-headers no_a2l_demo.out
