#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcplib_no_a2l_cfg.h
|
| Description:
|   XCPlite configuration OVERRIDES for the no-A2L use case
|   Applied AFTER the defaults in xcplib_cfg.h via:
|     cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_no_a2l_cfg.h\"")
|
|   The A2L database is generated externally by the xcpclient tool from the application ELF file.
|   No runtime A2L generation and A2L upload from the target
|   On-target calibration persistency with .BIN files is supported
|   ELF upload is optional
|
|   Key differences in overrides from the defaults in xcplib_cfg.h:
|       #undef OPTION_ENABLE_A2L_GENERATOR
|       #undef OPTION_ENABLE_A2L_UPLOAD
|   Addressing scheme:
|     Default
|   Platform requirements:
|    File system for ELF optional ELF upload and .BIN files
|   Examples:
|    no_a2l_demo, no_a2l_demo_cpp
|   Tools:
|     Use xcpclient A2L generation from ELF
|   Tests:
|     -
 ----------------------------------------------------------------------------*/

// No persistence — not supported in OPTION_CAL_SEGMENTS_ABS
#undef OPTION_ENABLE_PERSISTENCE

//-------------------------------------------------------------------------------
// Calibration segments

// Default: Relative addressing mode (address extension 0 is segment relative addressing)

// Option: Absolute addressing mode (address extension 0 is absolute addressing)
#define OPTION_CAL_SEGMENTS_ABS

//-------------------------------------------------------------------------------
// Events

// DAQ event management
// Enables tXcpEvent, XcpCreateIndexedEvent, XcpCreateEvent, XcpCreateEventInstance, XcpGetEventCount, XcpFindEvent, XcpGetEventName, XcpGetEventIndex, XcpGetEvent
// Optional, event trigger function not supported without OPTION_DAQ_EVENT_LIST are: DaqEventVar, DaqTriggerEventExt_s
#undef OPTION_DAQ_EVENT_LIST

//-------------------------------------------------------------------------------
// A2L / ELF — generated externally from ELF by xcpclient; disable on-target features
#undef OPTION_ENABLE_A2L_GENERATOR
#undef OPTION_ENABLE_A2L_UPLOAD
#define OPTION_ENABLE_ELF_UPLOAD
