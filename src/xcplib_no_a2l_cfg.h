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
|   Use case: The A2L database is generated externally by the xcpclient tool from the application ELF file.
|   No runtime A2L generation
|   No on-target A2L upload
|   Persistence is optional, disabling persistence and ELF upload completely removes the file system dependency
|   See free_rtos_demo for an example with persistence disabled
|
|   Only settings that DIFFER from the POSIX defaults are listed here.
 ----------------------------------------------------------------------------*/

//-------------------------------------------------------------------------------
// Calibration segments

// No persistence — not supported in OPTION_CAL_SEGMENTS_ABS
#undef OPTION_ENABLE_PERSISTENCE

// Absolute addressing mode (address extension 0 is absolute addressing)
// Default: Relative addressing mode (address extension 0 is segment relative addressing)
#define OPTION_CAL_SEGMENTS_ABS

// @@@@ TODO: Fix new offline ELF section based A2L generation in segment relative mode with persistence enabled, does not work yet


#undef OPTION_ENABLE_PERSISTENCE


//-------------------------------------------------------------------------------
// A2L / ELF — generated externally from ELF by xcpclient; disable on-target features
#undef OPTION_ENABLE_A2L_GENERATOR
#undef OPTION_ENABLE_A2L_UPLOAD
// #undef OPTION_ENABLE_ELF_UPLOAD
