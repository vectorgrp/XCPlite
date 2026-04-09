/*----------------------------------------------------------------------------
| File:
|   xcpappl.c
|
| Description:
|   Application specific functions and callbacks for xcplite.c
|   Additional functions for interface
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
 ----------------------------------------------------------------------------*/

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for fclose, fopen, fread, fseek, ftell
#include <string.h>  // for strncpy

#include "dbg_print.h"  // for DBG_PRINTF3, DBG_PRINT4, DBG_PRINTF4, DBG...
#include "platform.h"   // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "xcp.h"        // for CRC_XXX
#include "xcp_cfg.h"    // for XCP_ENABLE_xxx
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS) && !defined(_QNX)
#error "Please define platform _WIN, _MACOS or _LINUX or _QNX"
#endif

// @@@@ TODO: Improve, __write_delayed is the consistency hold flag parameter for the __callback_write function
#ifdef XCP_ENABLE_USER_COMMAND
static bool __write_delayed = false;
#endif

/**************************************************************************/
// Callbacks
/**************************************************************************/

static bool (*__callback_connect)(uint8_t mode) = NULL;
static uint8_t (*__callback_prepare_daq)(void) = NULL;
static uint8_t (*__callback_start_daq)(void) = NULL;
static void (*__callback_stop_daq)(void) = NULL;
static uint8_t (*__callback_freeze_daq)(uint8_t clear, uint16_t config_id) = NULL;
static uint8_t (*__callback_get_cal_page)(uint8_t segment, uint8_t mode) = NULL;
static uint8_t (*__callback_set_cal_page)(uint8_t segment, uint8_t page, uint8_t mode) = NULL;
static uint8_t (*__callback_init_cal)(uint8_t src_page, uint8_t dst_page) = NULL;
static uint8_t (*__callback_freeze_cal)(void) = NULL;
static uint8_t (*__callback_check)(uint8_t ext, uint32_t addr, uint8_t size) = NULL;
static uint8_t (*__callback_read)(uint32_t src, uint8_t size, uint8_t *dst) = NULL;
static uint8_t (*__callback_write)(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay) = NULL;
static uint8_t (*__callback_flush)(void) = NULL;

void ApplXcpRegisterConnectCallback(bool (*cb_connect)(uint8_t mode)) { __callback_connect = cb_connect; }
void ApplXcpRegisterPrepareDaqCallback(uint8_t (*cb_prepare_daq)(void)) { __callback_prepare_daq = cb_prepare_daq; }
void ApplXcpRegisterStartDaqCallback(uint8_t (*cb_start_daq)(void)) { __callback_start_daq = cb_start_daq; }
void ApplXcpRegisterStopDaqCallback(void (*cb_stop_daq)(void)) { __callback_stop_daq = cb_stop_daq; }
void ApplXcpRegisterFreezeDaqCallback(uint8_t (*cb_freeze_daq)(uint8_t clear, uint16_t config_id)) { __callback_freeze_daq = cb_freeze_daq; }
void ApplXcpRegisterGetCalPageCallback(uint8_t (*cb_get_cal_page)(uint8_t segment, uint8_t mode)) { __callback_get_cal_page = cb_get_cal_page; }
void ApplXcpRegisterSetCalPageCallback(uint8_t (*cb_set_cal_page)(uint8_t segment, uint8_t page, uint8_t mode)) { __callback_set_cal_page = cb_set_cal_page; }
void ApplXcpRegisterFreezeCalCallback(uint8_t (*cb_freeze_cal)(void)) { __callback_freeze_cal = cb_freeze_cal; }
void ApplXcpRegisterInitCalCallback(uint8_t (*cb_init_cal)(uint8_t src_page, uint8_t dst_page)) { __callback_init_cal = cb_init_cal; }
void ApplXcpRegisterCheckCallback(uint8_t (*cb_check)(uint8_t ext, uint32_t addr, uint8_t size)) { __callback_check = cb_check; }
void ApplXcpRegisterReadCallback(uint8_t (*cb_read)(uint32_t src, uint8_t size, uint8_t *dst)) { __callback_read = cb_read; }
void ApplXcpRegisterWriteCallback(uint8_t (*cb_write)(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay)) { __callback_write = cb_write; }
void ApplXcpRegisterFlushCallback(uint8_t (*cb_flush)(void)) { __callback_flush = cb_flush; }

/**************************************************************************/
// General notifications from protocol layer
/**************************************************************************/

bool ApplXcpConnect(uint8_t mode) {
    DBG_PRINTF4("ApplXcpConnect mode=%u\n", mode);
#ifdef XCP_ENABLE_USER_COMMAND
    __write_delayed = false;
#endif
    if (__callback_connect != NULL)
        return __callback_connect(mode);
    return true;
}

void ApplXcpDisconnect(void) { DBG_PRINT4("ApplXcpDisconnect\n"); }

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
bool ApplXcpPrepareDaq(void) {
    DBG_PRINT4("ApplXcpPrepareDaq\n");
    if (__callback_prepare_daq != NULL) {
        if (!__callback_prepare_daq()) {
            DBG_PRINT_WARNING("DAQ start canceled by AppXcpPrepareDaq!\n");
            return false;
        };
    }
    return true;
}
#endif

void ApplXcpStartDaq(void) {
    DBG_PRINT4("ApplXcpStartDaq\n");
    if (__callback_start_daq != NULL)
        __callback_start_daq();
}

void ApplXcpStopDaq(void) {
    DBG_PRINT4("ApplXcpStartDaq\n");
    if (__callback_stop_daq != NULL)
        __callback_stop_daq();
}

/**************************************************************************/
// Clock
// Get clock for DAQ timestamps
// Get information about clock synchronization state and grandmaster UUID
/**************************************************************************/

static uint64_t (*__callback_get_clock)(void) = NULL;
void ApplXcpRegisterGetClockCallback(uint64_t (*cb_get_clock)(void)) { __callback_get_clock = cb_get_clock; }

uint64_t ApplXcpGetClock64(void) {

    /* Return value is clock with
        Clock timestamp resolution defined in xcp_cfg.h
        Clock must be monotonic !!!
    */
    if (__callback_get_clock != NULL)
        return __callback_get_clock();
    return clockGet();
}

static uint8_t (*__callback_get_clock_state)(void) = NULL;
void ApplXcpRegisterGetClockStateCallback(uint8_t (*cb_get_clock_state)(void)) { __callback_get_clock_state = cb_get_clock_state; }

uint8_t ApplXcpGetClockState(void) {

    /* Return value may be one of the following:
        CLOCK_STATE_SYNCH, CLOCK_STATE_SYNCH_IN_PROGRESS, CLOCK_STATE_FREE_RUNNING
    */
    if (__callback_get_clock_state != NULL)
        return __callback_get_clock_state();
    return CLOCK_STATE_FREE_RUNNING; // Clock is a free running counter
}

static bool (*__callback_get_clock_info_grandmaster)(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum) = NULL;
void ApplXcpRegisterGetClockInfoGrandmasterCallback(bool (*cb_get_clock_info_grandmaster)(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum)) {
    __callback_get_clock_info_grandmaster = cb_get_clock_info_grandmaster;
}

bool ApplXcpGetClockInfoGrandmaster(uint8_t *client_uuid, uint8_t *grandmaster_uuid, uint8_t *epoch, uint8_t *stratum) {

    /*
    Return value true, please set the following parameters:
        stratum: XCP_STRATUM_LEVEL_UNKNOWN, XCP_STRATUM_LEVEL_RTC,XCP_STRATUM_LEVEL_GPS
        epoch: XCP_EPOCH_TAI, XCP_EPOCH_UTC, XCP_EPOCH_ARB
    */

    if (__callback_get_clock_info_grandmaster != NULL)
        return __callback_get_clock_info_grandmaster(client_uuid, grandmaster_uuid, epoch, stratum);

    return false; // No PTP support
}

/**************************************************************************/
// Pointer - XCP/A2L address conversion for absolute addressing mode
/**************************************************************************/

// 64 Bit and 32 Bit platform pointer to XCP/A2L address conversions
// XCP memory access is limited to a 4GB address range (32 Bit)

// The XCP addresses with extension = 0 for Win32 and Win64 versions of XCPlite are defined as relative to the load address of the main module
// This allows using Microsoft linker PDB files for address update
// In Microsoft Visual Studio set option "Generate Debug Information" to "optimized for sharing and publishing (/DEBUG:FULL)"

const uint8_t *gXcpBaseAddr = NULL;
bool gXcpBaseAddrValid = false;

// Global module load address to optimize resolving relocated absolute addresses during runtime
// xcp_get_base_addr() uses gXcpBaseAddr directly, not ApplXcpGetBaseAddr(), assuming XCP has been initialized before

// Set the base address for absolute addressing mode, if the default base address is not suitable
void ApplXcpSetBaseAddr(const uint8_t *addr) {
    gXcpBaseAddr = addr;
    gXcpBaseAddrValid = true;
    DBG_PRINTF4("ApplXcpSetBaseAddr() = %p\n", (void *)gXcpBaseAddr);
}

// Get the A2L unsigned 32 bit address for a given pointer
// Value is positive offset to ApplXcpGetBaseAddr
uint32_t ApplXcpGetAddr(const uint8_t *p) {
    const uint8_t *b = ApplXcpGetBaseAddr();
    int64_t diff = (int64_t)(p) - (int64_t)(b);
    DBG_PRINTF6("ApplXcpGetAddr: base = %p, addr = %p, diff = %" PRId64 "\n", (void *)b, (void *)p, diff);
    if (diff < 0 || diff > 0xFFFFFFFF) { // Check XCP address range is sufficient
        DBG_PRINTF_ERROR("Address out of range! base = %p, addr = %p\n", (void *)b, (void *)p);
        assert(0);
        return 0;
    }
    return (uint32_t)diff;
}

// Get the XCP 8 bit address extension for a given pointer
uint8_t ApplXcpGetAddrExt(const uint8_t *p) {
    (void)p;
#ifdef OPTION_SHM_MODE // get address extension for absolute addressing in SHM mode
    // In SHM mode, use application specific address extension for absolute addressing to support multiple applications
    return XCP_ADDR_EXT_ABS + XcpShmGetAppId();
#else
    return XCP_ADDR_EXT_ABS;
#endif
}

//----------------------------
// Windows 64 or 32 bit
#ifdef _WIN

const uint8_t *ApplXcpGetModuleAddr(void) { return (uint8_t *)GetModuleHandle(NULL); }

// Get base pointer for the XCP address range
// This function is time sensitive, as it is called once on every XCP event
const uint8_t *ApplXcpGetBaseAddr(void) {

    if (gXcpBaseAddrValid)
        return gXcpBaseAddr;
    ApplXcpSetBaseAddr(ApplXcpGetModuleAddr()); // Set module addr as default base address
    assert(gXcpBaseAddrValid);
    return gXcpBaseAddr;
}

#endif // _WIN

//----------------------------
// Linux 64 bit or QNX 64 bit
#if (defined(_LINUX) || defined(_QNX)) && defined(PLATFORM_64BIT)

#ifdef _QNX

#include <sys/link.h>     /* dl_iterate_phdr, dl_phdr_info */
#include <sys/neutrino.h> /* _NTO_VERSION */
#if _NTO_VERSION >= 800
#include <qh/misc.h> /* qh_get_progname */
#endif

static int dump_phdr(const struct dl_phdr_info *pinfo, size_t size, void *data) {
    DBG_PRINTF3("name=%s (%d segments, addr=0x%p)\n", pinfo->dlpi_name, pinfo->dlpi_phnum, (void *)pinfo->dlpi_addr);

    // On QNX, the application module name is the full path to the executable
#if _NTO_VERSION >= 800
    // QNX 8.0 is the first version to introduce qh_get_progname()
    // Strip off the path from the module name and compare it to the retrieved program name to find the corresponding phdr entry
    const char *pName = strrchr(pinfo->dlpi_name, '/');
    const char *pAppName = qh_get_progname();
    if (NULL != pName) {
        pName += 1;
    } else {
        pName = pinfo->dlpi_name;
    }
    if (0 == strncmp(pName, pAppName, strlen(pAppName))) {
        ApplXcpSetBaseAddr((uint8_t *)pinfo->dlpi_addr);
    }
#else
    // On QNX 7.1 or less, there is no API to retrieve the name of the current program
    // Name must be forwarded from args[0]
    // Workaround for now: Assume that entry 0 always contains the application module
    ApplXcpSetBaseAddr((uint8_t *)pinfo->dlpi_addr);

#endif

    (void)size;
    (void)data;
    return 0;
}

#else

#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <link.h>

static const uint8_t *gModuleAddr = NULL;
static bool gModuleAddrValid = false;

static int dump_phdr(struct dl_phdr_info *pinfo, size_t size, void *data) {
    // DBG_PRINTF5("name=%s (%d segments)\n", pinfo->dlpi_name, pinfo->dlpi_phnum);
    if (0 == strlen(pinfo->dlpi_name)) { // Application module has no name
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        gModuleAddr = (uint8_t *)pinfo->dlpi_addr;
        gModuleAddrValid = true;
    }
    (void)size;
    (void)data;
    return 0;
}
#endif

const uint8_t *ApplXcpGetModuleAddr(void) {
    if (gModuleAddrValid)
        return gModuleAddr;
    dl_iterate_phdr(dump_phdr, NULL);
    assert(gModuleAddrValid);
    return gModuleAddr;
}

const uint8_t *ApplXcpGetBaseAddr(void) {

    if (gXcpBaseAddrValid)
        return gXcpBaseAddr;
    ApplXcpSetBaseAddr(ApplXcpGetModuleAddr()); // Set module addr as default base address
    assert(gXcpBaseAddrValid);
    return gXcpBaseAddr;
}

#endif // (defined(_LINUX) || defined(_QNX)) && defined(PLATFORM_64BIT)

//----------------------------
// MACOS 64 bit
#ifdef _MACOS

#if defined(PLATFORM_32BIT)
#error "32 bit platform not supported on MACOS"
#endif

#include <mach-o/dyld.h>

/*
static int dump_so(void) {
    uint32_t i;
    uint32_t count = _dyld_image_count();
    for (i = 0; i < count; i++) {
        const char* name = _dyld_get_image_name(i);
        const struct mach_header* header = _dyld_get_image_header(i);
        printf("Library %d: %s, Header at: %p\n", i, name, header);
    }
    return 0;
}
*/

const uint8_t *ApplXcpGetModuleAddr(void) { return (uint8_t *)_dyld_get_image_header(0); }

// Get the base address for absolute addressing mode
// Use default base address, if not explicitly set by ApplXcpSetBaseAddr() before
const uint8_t *ApplXcpGetBaseAddr(void) {
    if (!gXcpBaseAddrValid) {
        // dump_so();
        ApplXcpSetBaseAddr(ApplXcpGetModuleAddr()); // Set module addr
        assert(gXcpBaseAddrValid);
    }

    return gXcpBaseAddr;
}

#endif // _MACOS

//----------------------------
// Linux 32 bit

#if defined(_LINUX) && defined(PLATFORM_32BIT)

const uint8_t *ApplXcpGetModuleAddr(void) { return ((uint8_t *)0); }

// On 32 bit Linux platforms, the entire 4GB address space is available for XCP, so the base address is 0 and the address conversion is a simple cast
const uint8_t *ApplXcpGetBaseAddr(void) { return ApplXcpGetModuleAddr(); }

#endif // defined(_LINUX) && defined(PLATFORM_32BIT)

/**************************************************************************/
// Callbacks
/**************************************************************************/

// CANape specific user commands for atomic consistent calibration operations
// Called only, when internal calibration segment management is not used or not enabled
#ifdef XCP_ENABLE_USER_COMMAND
uint8_t ApplXcpUserCommand(uint8_t cmd) {

    switch (cmd) {
    case 0x01: // Begin atomic calibration operation
        __write_delayed = true;
        break;
    case 0x02: // End atomic calibration operation;
        __write_delayed = false;
#ifdef XCP_ENABLE_APP_ADDRESSING
        if (__callback_flush != NULL)
            return __callback_flush();
#endif
        break;
    default:
        return CRC_SUBCMD_UNKNOWN;
    }
    return CRC_CMD_OK;
}
#endif

// Verify memory access permissions for a given address and size
uint8_t ApplXcpCheckMemory(uint8_t ext, uint32_t addr, uint8_t size) {
    if (__callback_check != NULL)
        return __callback_check(ext, addr, size);
    return CRC_CMD_OK; // Allow all accesses by default, if callback is not set
}

// Access calibration memory segments
// Called for SEG addressing mode, only when internal calibration segment management is not used or not enabled
#ifdef XCP_ENABLE_APP_ADDRESSING
uint8_t ApplXcpReadMemory(uint32_t src, uint8_t size, uint8_t *dst) {
    if (__callback_read != NULL)
        return __callback_read(src, size, dst);
    return CRC_ACCESS_DENIED;
}
uint8_t ApplXcpWriteMemory(uint32_t dst, uint8_t size, const uint8_t *src) {
    if (__callback_write != NULL)
        return __callback_write(dst, size, src, __write_delayed);
    return CRC_ACCESS_DENIED;
}
#endif

/**************************************************************************/
// Operations on calibration memory segments
// Calibration page switching callbacks
/**************************************************************************/

// Called only when internal calibration segment management is not enabled
#ifdef XCP_ENABLE_CAL_PAGE

uint8_t ApplXcpGetCalPage(uint8_t segment, uint8_t mode) {
    if (__callback_get_cal_page != NULL)
        return __callback_get_cal_page(segment, mode); // return cal page number
    return XCP_CALPAGE_WORKING_PAGE;                   // page 0 = working page (RAM) is default
}

uint8_t ApplXcpSetCalPage(uint8_t segment, uint8_t page, uint8_t mode) {
    if (__callback_set_cal_page != NULL)
        return __callback_set_cal_page(segment, page, mode); // return CRC_CMD_xxx return code
    return CRC_CMD_UNKNOWN;
}

#ifdef XCP_ENABLE_COPY_CAL_PAGE
uint8_t ApplXcpCopyCalPage(uint8_t srcSeg, uint8_t srcPage, uint8_t dstSeg, uint8_t dstPage) {
    if (srcSeg != dstSeg && srcSeg > 0)
        return CRC_PAGE_NOT_VALID; // Only one segment supported
    if (__callback_init_cal != NULL)
        return __callback_init_cal(srcPage, dstPage); // return CRC_CMD_xxx return code
    return CRC_CMD_UNKNOWN;
}
#endif

#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
uint8_t ApplXcpCalFreeze(void) {
    if (__callback_freeze_cal != NULL)
        return __callback_freeze_cal(); // return CRC_CMD_xxx return code
    return CRC_CMD_UNKNOWN;
}
#endif

#endif // XCP_ENABLE_CAL_PAGE

/**************************************************************************/
// DAQ resume
/**************************************************************************/

// Cold start data acquisition
// Not implemented
#ifdef XCP_ENABLE_DAQ_RESUME
uint8_t ApplXcpDaqResumeStore(uint16_t config_id) {

    DBG_PRINTF3("ApplXcpResumeStore config-id=%u\n", config_id);
    (void)config_id;
    return CRC_CMD_IGNORED;
}
uint8_t ApplXcpDaqResumeClear(void) {

    DBG_PRINT3("ApplXcpResumeClear\n");
    return CRC_CMD_IGNORED;
}
#endif

/**************************************************************************/
// Functions for upload of A2L AND ELF file
/**************************************************************************/

static char gXcpA2lName[XCP_A2L_FILENAME_MAX_LENGTH + 1] = ""; // A2L filename (without extension .a2l)
static char gXcpElfName[XCP_A2L_FILENAME_MAX_LENGTH + 1] = ""; // ELF filename (NO extension)

// Set the A2L file (filename without extension .a2l) to be provided to the host for upload
void XcpSetA2lName(const char *name) {
    assert(name != NULL && STRNLEN(name, XCP_A2L_FILENAME_MAX_LENGTH + 1) <= XCP_A2L_FILENAME_MAX_LENGTH);
    strncpy(gXcpA2lName, name, XCP_A2L_FILENAME_MAX_LENGTH);

    // Remove the extension from the name, if it exists
    char *dot = strrchr(gXcpA2lName, '.');
    if (dot != NULL)
        *dot = '\0'; // Null-terminate the string at the dot
    gXcpA2lName[XCP_A2L_FILENAME_MAX_LENGTH] = '\0';
    DBG_PRINTF4("XcpSetA2lName set to '%s'\n", gXcpA2lName);
}

// Return the A2L name (without extension)
const char *XcpGetA2lName(void) { return gXcpA2lName; }

// Set the ELF file (complete path) to be provided to the host for upload
void XcpSetElfName(const char *name) {
    assert(name != NULL && STRNLEN(name, XCP_A2L_FILENAME_MAX_LENGTH + 1) <= XCP_A2L_FILENAME_MAX_LENGTH);
    strncpy(gXcpElfName, name, XCP_A2L_FILENAME_MAX_LENGTH);
    gXcpElfName[XCP_A2L_FILENAME_MAX_LENGTH] = '\0';
    DBG_PRINTF4("XcpSetElfName set to '%s'\n", gXcpElfName);
}

// Return the ELF name (without extension)
const char *XcpGetElfName(void) { return gXcpElfName; }

#if defined(XCP_ENABLE_IDT_A2L_UPLOAD) || defined(XCP_ENABLE_IDT_ELF_UPLOAD) // Enable GET_ID A2L or ELF content upload to host

static FILE *gXcpFile = NULL;       // file content
static uint32_t gXcpFileLength = 0; // file length

static void closeFile(void) {
    assert(gXcpFile != NULL);
    fclose(gXcpFile);
    gXcpFile = NULL;
    DBG_PRINT4("File closed\n");
}

static uint32_t openFile(const char *filename) {
    if (filename == NULL || filename[0] == 0) {
        DBG_PRINT_WARNING("File name not set, cannot upload file!\n");
        return 0; // file is not valid
    }

    assert(gXcpFile == NULL);
    gXcpFile = fopen(filename, "rb");
    if (gXcpFile == NULL) {
        DBG_PRINTF_ERROR("File %s not found!\n", filename);
        return 0;
    }

    fseek(gXcpFile, 0, SEEK_END);
    gXcpFileLength = (uint32_t)ftell(gXcpFile);
    fseek(gXcpFile, 0, SEEK_SET);
    assert(gXcpFileLength > 0);
    DBG_PRINTF4("File %s ready for upload, size=%u\n", filename, gXcpFileLength);
    return gXcpFileLength;
}

// Called by the protocol layer to read a chunk of a file for upload
bool ApplXcpReadFile(uint8_t size, uint32_t addr, uint8_t *data) {
    if (gXcpFile == NULL)
        return false;
    assert(gXcpFile != NULL);
    if (addr + size > gXcpFileLength || size != fread(data, 1, (uint32_t)size, gXcpFile)) {
        closeFile();
        DBG_PRINTF_ERROR("ApplXcpReadFile addr=%u size=%u exceeds file length=%u\n", addr, size, gXcpFileLength);
        return false;
    }
    if (addr + size == gXcpFileLength) {
        closeFile(); // Close file after complete sequential read
    }
    return true;
}

#endif

/**************************************************************************/
// Provide infos for GET_ID
// The XCP command GET_ID provides different types of identification information
// Returns the length in bytes or 0, when the requested information is not available
/**************************************************************************/

// Called by the protocol layer to get identification information
uint32_t ApplXcpGetId(uint8_t id, uint8_t *buf, uint32_t bufLen) {

    uint32_t len = 0;
    switch (id) {

    case IDT_ASCII: {
        const char *project_name = XcpGetProjectName();
        len = (uint32_t)strlen(project_name);
        if (buf) {
            if (len >= bufLen - 1)
                return 0; // Insufficient buffer space
            strncpy((char *)buf, project_name, bufLen);
            ((char *)buf)[bufLen - 1] = '\0';
        }
        DBG_PRINTF3("ApplXcpGetId GET_ID %u project_name=%s\n", id, project_name);
    } break;

    case IDT_ASAM_NAME: {
        if (gXcpA2lName[0] == 0)
            return 0;
        len = (uint32_t)strlen(gXcpA2lName);
        if (buf) {
            if (len >= bufLen - 1)
                return 0; // Insufficient buffer space
            strncpy((char *)buf, gXcpA2lName, bufLen);
            ((char *)buf)[bufLen - 1] = '\0'; // Ensure null termination
        }
        DBG_PRINTF3("ApplXcpGetId GET_ID %u A2L name=%s\n", id, gXcpA2lName);
    } break;

    case IDT_ASAM_PATH: {
        if (gXcpA2lName[0] == 0)
            return 0;
        len = (uint32_t)strlen(gXcpA2lName) + 4;
        if (buf) {
            if (len > bufLen - 1)
                return 0; // Insufficient buffer space
            SNPRINTF((char *)buf, bufLen, "%s.a2l", gXcpA2lName);
        }
        DBG_PRINTF3("ApplXcpGetId GET_ID %u A2L path=%s\n", id, buf);
    } break;

    case IDT_ASAM_EPK: {
        const char *epk = XcpGetEcuEpk();
        if (epk == NULL)
            return 0;
        len = (uint32_t)strlen(epk);
        if (buf) {
            if (len > bufLen - 1)
                return 0; // Insufficient buffer space
            strncpy((char *)buf, epk, bufLen);
            ((char *)buf)[bufLen - 1] = '\0'; // Ensure null termination
            DBG_PRINTF3("ApplXcpGetId GET_ID%u EPK=%s\n", id, epk);
        } else {
            DBG_PRINTF3("ApplXcpGetId GET_ID %u EPK as upload (len=%u,value=%s)\n", id, len, epk);
        }
    } break;

#ifdef XCP_ENABLE_IDT_A2L_UPLOAD
    case IDT_ASAM_UPLOAD: {
        if (buf != NULL)
            return 0; // A2L not available as response buffer
        // Add .a2l extension to the A2L name
        char filename[XCP_A2L_FILENAME_MAX_LENGTH + 5];
        SNPRINTF((char *)filename, XCP_A2L_FILENAME_MAX_LENGTH + 5, "%s.a2l", gXcpA2lName);
        len = openFile(filename);
        DBG_PRINTF3("ApplXcpGetId GET_ID %u A2L as upload (len=%u)\n", id, len);
    } break;
#endif

#ifdef XCP_ENABLE_IDT_ELF_UPLOAD
    case IDT_VECTOR_ELF_UPLOAD: {
        if (buf != NULL)
            return 0; // ELF not available as response buffer
        // Assuming gXcpA2lName is the name of the ELF file without extension
        len = openFile(gXcpElfName);
        DBG_PRINTF3("ApplXcpGetId GET_ID %02X ELF as upload (len=%u)\n", id, len);
    } break;
#endif

#ifdef XCP_ENABLE_IDT_A2L_HTTP_GET
    case IDT_ASAM_URL: {
        if (buf) {
            uint8_t addr[4];
            if (socketGetLocalAddr(NULL, addr)) {
                SNPRINTF((char *)buf, bufLen - 1, "http://%u.%u.%u.%u:%u/file/%s.a2l", addr[0], addr[1], addr[2], addr[3], gOptionHTTPPort, gXcpA2lName);
                len = (uint32_t)STRNLEN((char *)buf, bufLen);
            }
        }
    } break;
#endif

        /*
            case IDT_ASAM_ECU:
            case IDT_ASAM_SYSID:
            case IDT_VECTOR_MAPNAMES:
            case IDT_VECTOR_GET_A2LOBJECTS_FROM_ECU:
                // Not implemented
        */
    default:
        DBG_PRINTF_WARNING("ApplXcpGetId GET_ID%u not implemented\n", id);
    }
    return len;
}
