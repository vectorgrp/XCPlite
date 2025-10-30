# Changelog

All notable changes to XCPlite are documented in this file.

## [V1.0.0]

### Added
- Absolute or relative calibration parameter segment addressing (`OPTION_CAL_SEGMENTS_ABS` in `main_cfg.h`)
- More flexible addressing mode configuration (see `xcp_cfg.h`)
- Support for more than one base address in relative address mode
- Optional async event with 1ms cycle time and prescaler support (`OPTION_DAQ_ASYNC_EVENT` in `main_cfg.h`)
- Different options to control the behavior of calibration segment persistence and freeze
- Memory optimization for event/daq-list mapping

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
