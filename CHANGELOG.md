# Changelog

All notable changes to XCPlite are documented in this file.




## [V2.0.1]

- General refactoring and code cleanup, various minor code improvements and optimizations
- Renamed to 'libxcplite' with external package name `xcplite`
- DAQ performance optimization, lock-less transmit queue for vectored IO
- New function XcpCreateCalBlk to create calibration blocks without A2L memory segments
- Simplified build script and CMake configuration, build script option to install libxcplite 
- Changed default encoding for dynamic addressing to 10 Bit event and 22 Bit signed offset, allowing for up to 4GB addressable range per event with max 1024 events
- New variadic C++ macro/template to create A2L typedefs and their components in one call with automatic type deduction (see hello_xcp_cpp example)
- New API functions to specify input quantities for axis
- New convenience functions in platform.c: clockGetMonotonicNs() and clockGetRealtimeNs()
- New demo `ptp4l_demo` demonstrating minimum requirement to achieve XCP PTP support
- Refactoring in platform.c socket abstraction: socket error handling, OPTION_SOCKET_HW_TIMESTAMPS, new function socketSetTimeout, improved debug logging for socketRecvFrom to help diagnose interface index issues on Linux, SOCKET keeps track of configured interface
- Log level 6 for very verbose debug logging
- Define GNU_SOURCE in cmakelists.txt
- xcpclient tool in Rust for testing and for offline A2L generation
- Improved Windows performance


### Breaking changes

- The signature of XpcInit has changed to
```c
void XcpInit(const char *name, const char *epk, uint8_t mode);
```
- The return value contract of `socketRecv` and `socketRecvFrom` has changed. Only code that uses these functions directly (i.e. code that includes `platform.h` is affected)

### Experimental

- Multi application mode (OPTION_SHM_MODE). See 'SHM.md' for details.  


## [V1.2.1]


## [V1.1.0]

### Added
- New variadic data acquisition C macro or C++ macro/template  (`DaqEventVar`)
- Support for enabling and disabling individual DAQ events at runtime (`DaqEventEnable`, `DaqEventDisable`)
- New demo `point_cloud_demo` demonstrating how to visualize arrays of objects in the CANape 3D scene window

### Changed
- Removed .out extension from the binaries on Linux and macOS builds

### Improvements
- Improved thread safety of event creation to avoid unnecessary THREAD_LOCAL state
- Optimized A2L generation macros, inlined address calculations moved into A2L creator functions
- Auto addressing mode now supports absolute and relative addressing
- More idiomatic C++ code in examples

### Fixed
- Bug in transmit thread queue handling causing too many packets sent
- Fixed cpp_demo assertion

### Minor Changes
- A2L transport layer section removed when bound to ANY and IP address auto-detection is off
- Support for hardware timestamping in platform.c for Linux
- Async event and prescaler turned off by default
- Improved code documentation and comments
- Refactored example applications for better clarity
- Various minor code improvements and optimizations


## [V1.0.0]

### Breaking API Changes

- XcpInit signature changed to
```c
void XcpInit(const char *name, const char *epk, bool activate);
```

- A2lInit signature changed to
```c
    bool A2lInit(const uint8_t addr[4], uint16_t port, bool use_tcp, uint32_t mode);
```

- CalSeg constructor signature changed, takes a pointer to the default parameters 
```c
    CalSeg(const char *name, const T *default_params)
```

- A2lTypedefBegin gets a pointer to any instance of the type.  
- A2lTypedefParameterComponent does not need the typename parameter anymore
- A2lTypedefMeasurementComponent does not need the typename parameter anymore and has a comment parameter
```c
{
    A2lTypedefBegin(ParametersT, &kParameters, "A2L Typedef for ParametersT");
    A2lTypedefParameterComponent(min, "Minimum random number value", "Volt", -100.0, 100.0);
    A2lTypedefParameterComponent(max, "Maximum random number value", "Volt", -100.0, 100.0);
    A2lTypedefEnd();
} 

- DaqCreateEventInstance does not return the event id anymore, new function DaqGetEventInstanceId to get the event id from the name
```c
    DaqCreateEventInstance("task");
    tXcpEventId task_event_id = DaqGetEventInstanceId("task");
```

- BIN format changes to enable future feature extensions, old BIN files are not compatible anymore


### Added
- Absolute or relative calibration parameter segment addressing (`OPTION_CAL_SEGMENTS_ABS` in `xcplib_cfg.h`)
- More flexible addressing scheme configuration (see `xcp_cfg.h`)
- Generated A2L file uses the `project_no` identifier to indicate the configured addressing schema (currently ACSDD or CASDD)
- Support for more than one base address in relative address mode, variadic function to trigger event with multiple base addresses
- Optional async event with 1ms cycle time and prescaler support (`OPTION_DAQ_ASYNC_EVENT` in `xcplib_cfg.h`)
- Different options to control the behavior of calibration segment persistence and freeze
- Memory optimization for event/daq-list mapping
- XCP_ENABLE_COPY_CAL_PAGE_WORKAROUND to enable workaround for CANape init calibration segments bug
- Variadic macro to create, trigger, and register local and member variables in one call with automatic addressing mode deduction (see hello_xcp_cpp example)
```c
    DaqEventExtVar(avg_calc1, this,                                               //
                   (input, "Input value for floating average", "V", 0.0, 1000.0), //
                   (average, "Current calculated average"),                       //
                   (current_index_, "Current position in ring buffer"),           //
                   (sample_count_, "Number of samples collected"),                //
                   (sum_, "Running sum of all samples")

    );
```

### Changed
- Signature of `xcplib::CreateCalSeg` changed, pointer to reference page
- Automatic EPK segment is now optional (`OPTION_CAL_SEGMENT_EPK` in `xcplib_cfg.h`)

### Experimental
- Tool `bintool` to convert XCPlite-specific BIN files to Intel-HEX format and apply Intel-HEX files to BIN files
- New demo `no_a2l_demo` to demonstrate workflows without runtime A2L generation (using a XCPlite-specific A2L creator, see README.md of `no_a2l_demo`)
- New demo 'bpf_demo' to demonstrate usage of XCPlite together with eBPF programs for Linux kernel tracing (see README.md of `bpf_demo`)
- Internal naming convention refactored to support A2L creation for dynamic objects from ELF/DWARF binaries
- Rust xcp-lite >V1.0.0 uses the calibration segment management of XCPlite instead of implementing its own

### Fixed
- Various bug fixes



## [V0.9.2]

### Breaking Changes
- Breaking changes from V0.6

### Added
- Lockless transmit queue (works on x86-64 strong and ARM-64 weak memory models)
- Measurement and read access to variables on stack
- Calibration segments for lock-free and thread-safe calibration parameter access, consistent calibration changes, and page switches
- Support for multiple segments with working and reference page and independent page switching
- Build as a library
- Used as FFI library for the Rust xcp-lite version

### Changed
- Refactored A2L generation macros
