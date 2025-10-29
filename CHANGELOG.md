# Changelog



## [V0.9.3] - Work in Progress

- More flexible addressing mode configuration in xcp_cfg.h
- Absolute calibration segment addressing (OPTION_CAL_SEGMENTS_ABS in main_cfg.h)
- Signature of xcplib::CreateCalSeg changed, pointer to reference page page
- Automatic EPK segment is optional (OPTION_CAL_SEGMENT_EPK in main_cfg.h)
- Support for more than one base address in relative address mode
- Optional async event with 1ms cycle time and prescaler support (OPTION_DAQ_ASYNC_EVENT in main_cfg.h)
- New experimental demo no_a2l_demo to demonstrate workflows without runtime A2L generation (using a XCPlite specific A2L creator, see README.MD of no_a2l_demo)
- Memory optimization for event/daq-list mapping
- Internal naming convention refactored to support A2L creation for dynamic objects from ELF/DWARF binaries (gXcp, gA2l and __* are ignored by the A2L creator)
- Generated A2L file uses the project_no identifier to indicate the configured addressing schema (currently ACSDD or CASDD)
- Generate IF_DATA CANAPE_ADDRESS_UPDATE for memory segments
- Option to include or embed AML files
- Bugfixes

## [V0.9.2]

- Breaking changes to V6
- Lockless transmit queue. Works on x86-64 strong and ARM-64 weak memory model
- Measurement and read access to variables on stack
- Calibration segments for lock free and thread safe calibration parameter access, consistent calibration changes and page switches
- Supports multiple segments with working and reference page and independent page switching
- Refactored A2L generation macros
- Build as a library
- Used (as FFI library) for the rust xcp-lite version
