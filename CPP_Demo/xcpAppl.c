/*----------------------------------------------------------------------------
| File:
|   xcpAppl.c
|
| Description:
|   
|   Platform and implementation specific functions used by the XCP implementation
|   All other callbacks/dependencies are implemented as macros in xcpAppl.h
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"
#include "../src/platform.h"
#include "../src/util.h"
#include "../src/xcp.h"
#include "../src/xcp.hpp"


    
/**************************************************************************/
// Test
/**************************************************************************/

#ifndef ApplXcpGetDebugLevel
uint32_t ApplXcpGetDebugLevel() {
    return gDebugLevel;
}
#endif

/**************************************************************************/
// General Callbacks
/**************************************************************************/

BOOL ApplXcpConnect() {
    return Xcp::getInstance()->onConnect();
}

BOOL ApplXcpPrepareDaq() {
    return Xcp::getInstance()->onPrepareDaq();
}

BOOL ApplXcpStartDaq() {
    return Xcp::getInstance()->onStartDaq();
}

BOOL ApplXcpStopDaq() {
    return Xcp::getInstance()->onStopDaq();
}


/**************************************************************************/
// Clock
// Get clock for DAQ timestamps
/**************************************************************************/

// XCP slave clock timestamp resolution defined in xcp_cfg.h
// Clock must be monotonic !!!

uint64_t ApplXcpGetClock64() {
    return clockGet64();
}

uint8_t ApplXcpGetClockState() {
    return LOCAL_CLOCK_STATE_FREE_RUNNING;
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

    // The XCP addresses for Win32 and Win64 versions of XCPlite are defined as relative to the load address of the main module
    // This allows using Microsoft linker PDB files for address update
    // In Microsoft Visual Studio set option "Generate Debug Information" to "optimized for sharing and publishing (/DEBUG:FULL)"
    // In CANape select "Microsoft PDB extented"

#ifdef _WIN

static uint8_t* baseAddr = NULL;
static uint8_t baseAddrValid = 0;

// Get base pointer for the XCP address range
// This function is time sensitive, as it is called once on every XCP event
uint8_t* ApplXcpGetBaseAddr() {

    if (!baseAddrValid) {
        baseAddr = (uint8_t*)GetModuleHandle(NULL);
        baseAddrValid = 1;
#ifdef XCP_ENABLE_DEBUG_PRINTS
        if (ApplXcpGetDebugLevel() >= 1) printf("Set global addressing base (ApplXcpGetBaseAddr() = 0x%I64X)\n", (uint64_t)baseAddr);
#endif
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

uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr) {
    (void)addr_ext;
    return ApplXcpGetBaseAddr() + addr;
}
#endif


#ifdef _LINUX32

uint8_t* ApplXcpGetBaseAddr()
{
    return ((uint8_t*)0);
}

uint32_t ApplXcpGetAddr(uint8_t* p)
{
    assert((uint64_t)p <= 0xFFFFFFFF);
    return ((uint32_t)p);
}

uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr) {
    (void)addr_ext;
    return (uint8_t*)addr;
}

#endif


/**************************************************************************/
// Infos for GET_ID
/**************************************************************************/

#ifdef XCP_ENABLE_IDT_A2L_NAME // Enable GET_ID A2L filename without extension

const char* ApplXcpGetName() {
    return (char*)"CPP_Demo";
}

const char* ApplXcpGetA2lName() {
    return ApplXcpGetName();
}

const char* ApplXcpGetA2lFileName() {
    static char a2lPath[MAX_PATH + 100 + 4]; // Full path + name + extension
    sprintf(a2lPath, "%s.a2l", ApplXcpGetA2lName());
    return a2lPath;
}
#endif


/**************************************************************************/
// Read A2L to memory accessible by XCP
/**************************************************************************/

#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable GET_ID A2L content upload to host

static uint8_t* gXcpFile = NULL; // file content
static uint32_t gXcpFileLength = 0; // file length
#ifdef _WIN
static HANDLE hFile, hFileMapping;
#endif

BOOL ApplXcpGetA2lUpload(uint8_t** p, uint32_t* n) {

    const char* filename = ApplXcpGetA2lFileName();

#ifdef XCP_ENABLE_DEBUG_PRINTS
    if (ApplXcpGetDebugLevel() >= 1) printf("Load %s\n", filename);
#endif

#ifdef _LINUX // Linux
    if (gXcpFile) free(gXcpFile);
    FILE* fd;
    fd = fopen(filename, "r");
    if (fd == NULL) {
        printf("ERROR: file %s not found!\n", filename);
        return 0;
    }
    struct stat fdstat;
    stat(filename, &fdstat);
    gXcpFile = (uint8_t*)malloc((size_t)(fdstat.st_size + 1));
    gXcpFileLength = (uint32_t)fread(gXcpFile, 1, (uint32_t)fdstat.st_size, fd);
    fclose(fd);
#else
    wchar_t wcfilename[256] = { 0 };
    if (gXcpFile) {
        UnmapViewOfFile(gXcpFile);
        CloseHandle(hFileMapping);
        CloseHandle(hFile);
    }
    MultiByteToWideChar(0, 0, filename, (int)strlen(filename), wcfilename, (int)strlen(filename));
    hFile = CreateFileW((wchar_t*)wcfilename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("file %s not found!\n", filename);
        return 0;
    }
    gXcpFileLength = (uint32_t)GetFileSize(hFile, NULL) - 2;
    hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, gXcpFileLength, NULL);
    if (hFileMapping == NULL) return 0;
    gXcpFile = (uint8_t*)MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    if (gXcpFile == NULL) return 0;
#endif
#ifdef XCP_ENABLE_DEBUG_PRINTS
    if (ApplXcpGetDebugLevel() >= 1) printf("  file %s ready for upload, size=%u\n\n", filename, gXcpFileLength);
#endif


    * n = gXcpFileLength;
    *p = gXcpFile;
    return 1;
}

#endif

