# Changelog

All notable changes to XCPlite are documented in this file.

## [V1.0.0]

### Breaking API Changes


- XcpInit signature changed to
```c
void XcpInit(const char *name, const char *epk, bool activate);
```

- A2lInit signature changed to
```c
    bool A2lInit(const uint8_t addr[4], uint16_t port,bool use_tcp,uint32_t mode);
```

- CalSeg constructor signature changed, takes a pointer to the default parameters 
```c
    CalSeg(const char *name, const T *default_params)
```

- A2lTypedefBegin gets a pointer to an instance of the type.  
For convenience, it creates a local variable to store the instance pointer, multiple A2lTypedefBegin/End block may need individual scope to avoid conflicts
- A2lTypedefParameterComponent does not need the typename parameter anymore
- A2lTypedefMeasurementComponent does not need the typename parameter anymore and has a comment parameter
```c
{
    A2lTypedefBegin(ParametersT, &kParameters, "A2L Typedef for ParametersT");
    A2lTypedefParameterComponent(min, "Minimum random number value", "", -100.0, 100.0);
    A2lTypedefParameterComponent(max, "Maximum random number value", "", -100.0, 100.0);
    A2lTypedefEnd();
} 

- DaqCreateEventInstance does not return the event id anymore, new function DaqGetEventInstanceId to get the event id from the name
```c
    DaqCreateEventInstance("task");
    tXcpEventId task_event_id = DaqGetEventInstanceId("task");
```


### Added
- Absolute or relative calibration parameter segment addressing (`OPTION_CAL_SEGMENTS_ABS` in `main_cfg.h`)
- More flexible addressing mode configuration (see `xcp_cfg.h`)
- Support for more than one base address in relative address mode
- Optional async event with 1ms cycle time and prescaler support (`OPTION_DAQ_ASYNC_EVENT` in `main_cfg.h`)
- Different options to control the behavior of calibration segment persistence and freeze
- Memory optimization for event/daq-list mapping
- Variadic macro to create, trigger, and register local and member variables in one call (see hello_xcp_cpp example)
```c
    XcpDaqEventExt(avg_calc1, this,                                               //
                   (input, "Input value for floating average", "V", 0.0, 1000.0), //
                   (average, "Current calculated average"),                       //
                   (current_index_, "Current position in ring buffer"),           //
                   (sample_count_, "Number of samples collected"),                //
                   (sum_, "Running sum of all samples")

    );
```

### Changed
- Signature of `xcplib::CreateCalSeg` changed, pointer to reference page
- Automatic EPK segment is now optional (`OPTION_CAL_SEGMENT_EPK` in `main_cfg.h`)

### Experimental
- Include tool `bintool` to convert XCPlite-specific BIN files to Intel-HEX format and apply Intel-HEX files to BIN files
- New demo `no_a2l_demo` to demonstrate workflows without runtime A2L generation (using a XCPlite-specific A2L creator, see README.md of `no_a2l_demo`)
- Internal naming convention refactored to support A2L creation for dynamic objects from ELF/DWARF binaries (`gXcp`, `gA2l`, and `__*` are ignored by the A2L creator)
- Generated A2L file uses the `project_no` identifier to indicate the configured addressing schema (currently ACSDD or CASDD)

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
