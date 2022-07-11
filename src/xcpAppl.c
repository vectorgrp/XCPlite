/*----------------------------------------------------------------------------
| File:
|   xcpAppl.c
|
| Description:
|   Application specific functions for xcpLite
|   All other callbacks/dependencies are implemented as macros in xcpAppl.h
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"
#include "platform.h"
#include "options.h"
#include "util.h"
#include "xcpLite.h"
#include "xcpAppl.h"
#ifdef APP_CPP_Demo
#include "xcp.hpp"
#else
#ifdef OPTION_ENABLE_CAL_SEGMENT
#include "ecu.h"
#endif
#endif

    

/**************************************************************************/
// General Callbacks
/**************************************************************************/

#ifdef APP_CPP_Demo

BOOL ApplXcpConnect() {
    return Xcp::getInstance()->onConnect();
}

BOOL ApplXcpPrepareDaq() {
    return Xcp::getInstance()->onPrepareDaq();
}

BOOL ApplXcpStartDaq() {
    return Xcp::getInstance()->onStartDaq();
}

void ApplXcpStopDaq() {
    Xcp::getInstance()->onStopDaq();
}

#else

BOOL ApplXcpConnect() {
    XCP_DBG_PRINT1("XCP connect\n");
    return TRUE;
}

BOOL ApplXcpPrepareDaq() { 
    XCP_DBG_PRINT1("XCP prepare DAQ\n");
#if OPTION_ENABLE_PTP
    if (gOptionPTP) {
        return ptpClockPrepareDaq();
    }
    else {
        return TRUE;
    }
#else
    return TRUE;
#endif
}

BOOL ApplXcpStartDaq() {
    XCP_DBG_PRINT1("XCP start DAQ\n");
    return TRUE;
}

void ApplXcpStopDaq() {
    XCP_DBG_PRINT1("XCP stop DAQ\n");
#if OPTION_ENABLE_PTP
    if (gOptionPTP) {
        ptpClockStopDaq();
    }
#endif
}

#endif

/**************************************************************************/
// Clock
// Get clock for DAQ timestamps
/**************************************************************************/

// XCP server clock timestamp resolution defined in xcp_cfg.h
// Clock must be monotonic !!!

uint64_t ApplXcpGetClock64() { 

    return clockGet64();
}

uint8_t ApplXcpGetClockState() { 

    return CLOCK_STATE_FREE_RUNNING;
}

BOOL ApplXcpGetClockInfoGrandmaster(uint8_t* uuid, uint8_t* epoch, uint8_t* stratum) {

  (void)uuid;
  (void)epoch;
  (void)stratum;

    return FALSE;
}


/**************************************************************************/
// Pointer - Address conversion
/**************************************************************************/

// 64 Bit and 32 Bit platform pointer to XCP/A2L address conversions
// XCP memory access is limited to a 4GB address range

// The XCP addresses with extension = 0 for Win32 and Win64 versions of XCPlite are defined as relative to the load address of the main module
// This allows using Microsoft linker PDB files for address update
// In Microsoft Visual Studio set option "Generate Debug Information" to "optimized for sharing and publishing (/DEBUG:FULL)"


#define XCP_ENABLE_MEMORY_CHECK
#ifdef XCP_ENABLE_MEMORY_CHECK
BOOL check(uint8_t *p)
{
    (void)p;
    return TRUE;
}
#endif


uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr) {

    if (addr_ext != 0) return NULL;
    uint8_t* p;

#ifdef _WIN32 // on WIN32 check that XCP address is in range, because addr is relativ to baseaddr
    assert((uint64_t)ApplXcpGetBaseAddr() + addr <= 0xffffffff); 
#endif

    p = ApplXcpGetBaseAddr() + addr;
#ifdef XCP_ENABLE_CAL_PAGE
    p = ecuParAddrMapping(p);
#endif
    
#ifdef XCP_ENABLE_MEMORY_CHECK
    if (!check(p)) {
        XCP_DBG_PRINTF_ERROR("ERROR: Illegal address %08X!\n", addr);
        return NULL;
    }
#endif
    return p;
}


#ifdef _WIN

static uint8_t* baseAddr = NULL;
static uint8_t baseAddrValid = 0;

// Get base pointer for the XCP address range
// This function is time sensitive, as it is called once on every XCP event
uint8_t* ApplXcpGetBaseAddr() {

    if (!baseAddrValid) {
        baseAddr = (uint8_t*)GetModuleHandle(NULL);
        baseAddrValid = 1;
        XCP_DBG_PRINTF1("ApplXcpGetBaseAddr() = 0x%I64X\n", (uint64_t)baseAddr);
    }
    return baseAddr;
}

uint32_t ApplXcpGetAddr(uint8_t* p) {

    assert(p >= ApplXcpGetBaseAddr());
#ifdef _WIN64
    assert(((uint64_t)p - (uint64_t)ApplXcpGetBaseAddr()) <= 0xffffffff); // be sure that XCP address range is sufficient
#endif
    return (uint32_t)(p - ApplXcpGetBaseAddr());
}

#endif

#ifdef _LINUX64

#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <link.h>

uint8_t* baseAddr = NULL;
uint8_t baseAddrValid = 0;

static int dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data)
{
    // XCP_DBG_PRINTF1("name=%s (%d segments)\n", pinfo->dlpi_name, pinfo->dlpi_phnum);

    // Application modules has no name
    if (0 == strlen(pinfo->dlpi_name)) {
        baseAddr = (uint8_t*)pinfo->dlpi_addr;
    }

    (void)size;
    (void)data;
    return 0;
}

uint8_t* ApplXcpGetBaseAddr() {

    if (!baseAddrValid) {
        dl_iterate_phdr(dump_phdr, NULL);
        assert(baseAddr != NULL);
        baseAddrValid = 1;
        XCP_DBG_PRINTF1("BaseAddr = %lX\n", (uint64_t)baseAddr);
    }

    return baseAddr;
}

uint32_t ApplXcpGetAddr(uint8_t* p)
{
    ApplXcpGetBaseAddr();
    return (uint32_t)(p - baseAddr);
}

#endif


#ifdef _LINUX32

uint8_t* ApplXcpGetBaseAddr()
{
    return ((uint8_t*)0);
}

uint32_t ApplXcpGetAddr(uint8_t* p)
{
    return ((uint32_t)(p));
}

#endif





/**************************************************************************/
// Pointer to XCP address conversions for LINUX shared objects
/**************************************************************************/

#ifdef XCP_ENABLE_SO

// Address information of loaded modules for XCP (application and shared libraries)
// Index is XCP address extension
// Index 0 is application

static struct
{
    const char* name;
    uint8_t* baseAddr;
}
gModuleProperties[XCP_MAX_MODULE] = { {} };


uint8_t ApplXcpGetExt(uint8_t* addr)
{
    // Here we have the possibility to loop over the modules and find out the extension
    ()addr;
    return 0;
}

uint32_t ApplXcpGetAddr(uint8_t* addr)
{
    uint8_t addr_ext = ApplXcpGetExt(addr);
    union {
        uint8_t* ptr;
        uint32_t i;
    } rawAddr;
    rawAddr.ptr = (uint8_t*)(addr - gModuleProperties[addr_ext].baseAddr);
    return rawAddr.i;
}

uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr)
{
    uint8_t* baseAddr = 0;
    if (addr_ext < XCP_MAX_MODULE) {
        baseAddr = gModuleProperties[addr_ext].baseAddr;
    }
    return baseAddr + addr;
}


static int dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data)
{
    XCP_DBG_PRINTF1("0x%zX %s 0x%X %d %d %d %d 0x%X\n",
            pinfo->dlpi_addr, pinfo->dlpi_name, pinfo->dlpi_phdr, pinfo->dlpi_phnum,
            pinfo->dlpi_adds, pinfo->dlpi_subs, pinfo->dlpi_tls_modid,
            pinfo->dlpi_tls_data);

  // Modules
  if (0 < strlen(pinfo->dlpi_name)) {
    // Here we could remember module information or something like that
  }

  // Application
  else  {

     XCP_DBG_PRINTF1("Application base addr = 0x%zx\n", pinfo->dlpi_addr);
     gModuleProperties[0].baseAddr = (uint8_t*) pinfo->dlpi_addr;
  }

  ()size;
  ()data;
  return 0;
}

void ApplXcpInitBaseAddressList()
{
    XCP_DBG_PRINTF1("Module List:\n");
    dl_iterate_phdr(dump_phdr, NULL);
}

#endif




/**************************************************************************/
// Calibration page handling
/**************************************************************************/

#ifdef OPTION_ENABLE_CAL_SEGMENT

// segment = 0
// RAM = page 0, FLASH = page 1

uint8_t ApplXcpGetCalPage(uint8_t segment, uint8_t mode) {
    (void)mode;
    if (segment > 0) return CRC_PAGE_NOT_VALID;
    return ecuParGetCalPage();
}

uint8_t ApplXcpSetCalPage(uint8_t segment, uint8_t page, uint8_t mode) {
    if (segment > 0) return CRC_SEGMENT_NOT_VALID;
    if (page > 1) return CRC_PAGE_NOT_VALID;
    if ((mode & (CAL_PAGE_MODE_ECU | CAL_PAGE_MODE_XCP)) != (CAL_PAGE_MODE_ECU | CAL_PAGE_MODE_XCP)) return CRC_PAGE_MODE_NOT_VALID;
    ecuParSetCalPage(page);
    return 0;
}

#endif


/**************************************************************************/
// Read A2L to memory accessible by XCP
/**************************************************************************/

#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable GET_ID A2L content upload to host

/**************************************************************************/
// Infos for GET_ID
/**************************************************************************/

static uint8_t* gXcpFile = NULL; // file content
static uint32_t gXcpFileLength = 0; // file length


uint32_t ApplXcpGetId(uint8_t id, uint8_t* buf, uint32_t bufLen) {

    uint32_t len = 0;
    switch (id) {

    case IDT_ASCII:
    case IDT_ASAM_NAME:
        len = (uint32_t)strlen(APP_NAME);
        if (buf) {
            if (len > bufLen) return 0; // Insufficient buffer space
            strncpy((char*)buf, APP_NAME, len);
        }
        break;

    case IDT_ASAM_PATH:
        len = (uint32_t)strlen(OPTION_A2L_FILE_NAME);
        if (buf) {
            if (len > bufLen) return 0; // Insufficient buffer space
            strncpy((char*)buf, OPTION_A2L_FILE_NAME, len);
        }
        break;

    case IDT_ASAM_EPK:
        // Not implemented
        break;

#ifdef XCP_ENABLE_IDT_A2L_UPLOAD
    case IDT_ASAM_UPLOAD:
        if (NULL==(gXcpFile=loadFile(OPTION_A2L_FILE_NAME,&gXcpFileLength))) return 0;
        len = gXcpFileLength;
        break;
#endif

#ifdef XCP_ENABLE_IDT_A2L_HTTP_GET
    case IDT_ASAM_URL:
        if (buf) {
            uint8_t addr[4];
            if (socketGetLocalAddr(NULL, addr)) {
                SNPRINTF((char*)buf, bufLen, "http://%u.%u.%u.%u:%u/file/" OPTION_A2L_FILE_NAME, addr[0], addr[1], addr[2], addr[3], gOptionHTTPPort);
                len = (uint32_t)strlen((char*)buf);
            }
        }
        break;
#endif

    }
    return len;
}


BOOL ApplXcpReadA2L(uint8_t size, uint32_t addr, uint8_t* data) {
    if (addr + size > gXcpFileLength) return FALSE;
    memcpy(data, gXcpFile + addr, size);
    return TRUE;
}


#endif





