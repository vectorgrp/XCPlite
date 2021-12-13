/*----------------------------------------------------------------------------
| File:
|   xcpAppl.c
|
| Description:
|   Externals for xcpLite
|   Platform and implementation specific functions
|   All other callbacks/dependencies are implemented as macros in xcpAppl.h
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h"
#include "main_cfg.h"
#include "clock.h"
#include "xcpLite.h"
#include "ecu.h"



/**************************************************************************/
// Clock
// Get clock for DAQ timestamps
/**************************************************************************/

// XCP slave clock timestamp resolution defined in xcp_cfg.h
// Clock must be monotonic !!!



uint32_t ApplXcpGetClock() { return clockGet32(); }
uint64_t ApplXcpGetClock64() { return clockGet64(); }
int ApplXcpPrepareDaq() { return 1; }




#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO

uint8_t ApplXcpGetClockInfo(T_CLOCK_INFO_SLAVE* s, T_CLOCK_INFO_GRANDMASTER* m) {

    uint8_t uuid[8] = APP_DEFAULT_CLOCK_UUID;


        memcpy(s->UUID, uuid, 8);
        memcpy(m->UUID, uuid, 8);
#ifdef CLOCK_USE_UTC_TIME_NS
        s->stratumLevel = XCP_STRATUM_LEVEL_UTC;
        m->stratumLevel = XCP_STRATUM_LEVEL_UTC;
        m->epochOfGrandmaster = XCP_EPOCH_TAI;
#else
        s->stratumLevel = XCP_STRATUM_LEVEL_UNSYNC;
        m->stratumLevel = XCP_STRATUM_LEVEL_UNSYNC;
        m->epochOfGrandmaster = XCP_EPOCH_ARB;
#endif



    if (gDebugLevel >= 1) {
        printf("  Slave-UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X Grandmaster-UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",
            s->UUID[0], s->UUID[1], s->UUID[2], s->UUID[3], s->UUID[4], s->UUID[5], s->UUID[6], s->UUID[7],
            m->UUID[0], m->UUID[1], m->UUID[2], m->UUID[3], m->UUID[4], m->UUID[5], m->UUID[6], m->UUID[7]);
    }

    return 1;
}

#endif


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

uint8_t* baseAddr = NULL;
uint8_t baseAddrValid = 0;

uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr) {

    return ApplXcpGetBaseAddr() + addr;
}

uint32_t ApplXcpGetAddr(uint8_t* p) {

    assert(p >= ApplXcpGetBaseAddr());
#ifdef _WIN64
    assert(((uint64_t)p - (uint64_t)ApplXcpGetBaseAddr()) <= 0xffffffff); // be sure that XCP address range is sufficient
#endif
    return (uint32_t)(p - ApplXcpGetBaseAddr());
}

// Get base pointer for the XCP address range
// This function is time sensitive, as it is called once on every XCP event
uint8_t* ApplXcpGetBaseAddr() {

    if (!baseAddrValid) {
        baseAddr = (uint8_t*)GetModuleHandle(NULL);
        baseAddrValid = 1;
#ifdef XCP_ENABLE_TESTMODE
        if (gDebugLevel >= 1) printf("ApplXcpGetBaseAddr() = 0x%I64X\n", (uint64_t)baseAddr);
#endif
    }
    return baseAddr;
}

#endif

#ifdef _LINUX64

#define __USE_GNU
#include <link.h>

uint8_t* baseAddr = NULL;
uint8_t baseAddrValid = 0;

static int dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data)
{
  // printf("name=%s (%d segments)\n", pinfo->dlpi_name, pinfo->dlpi_phnum);

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
    assert(baseAddr!=NULL);
    baseAddrValid = 1;
    printf("BaseAddr = %lX\n",(uint64_t)baseAddr);
  }

  return baseAddr;
}

uint32_t ApplXcpGetAddr(uint8_t* p)
{
  ApplXcpGetBaseAddr();
  return (uint32_t)(p - baseAddr);
}

uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr)
{
  ApplXcpGetBaseAddr();
  return baseAddr + addr;
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

uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr)
{
    return ((uint8_t*)(addr));
}

#endif



/**************************************************************************/
// Calibration page handling
/**************************************************************************/

#ifdef XCP_ENABLE_CAL_PAGE

uint8_t calPage = 0; // RAM = 0, FLASH = 1

uint8_t ApplXcpGetCalPage(uint8_t segment, uint8_t mode) {

    return calPage;
}

uint8_t ApplXcpSetCalPage(uint8_t segment, uint8_t page, uint8_t mode) {
    // Calibration page switch code here
    calPage = page;
    return 0;
  }
#endif





/**************************************************************************/
// Infos for GET_ID
/**************************************************************************/

uint8_t ApplXcpGetSlaveId(char** p, uint32_t* n) {

    *p = APP_NAME;
    *n = APP_NAME_LEN;
    return 1;
}



/**************************************************************************/
// Read A2L to memory accessible by XCP
/**************************************************************************/

#ifdef XCP_ENABLE_A2L_NAME // Enable GET_ID A2L name upload to host

static char gA2LFilename[100]; // Name without extension

// A2L base name for GET_ID
static char gA2LPathname[MAX_PATH + 100 + 4]; // Full path + name +extension


uint8_t ApplXcpGetA2LFilename(char** p, uint32_t* n, int path) {

    // Create a unique A2L file name for this build
    sprintf(gA2LFilename, APP_NAME "-%08X-%u", ApplXcpGetAddr((uint8_t*)&ecuPar), XcpTlGetSlavePort()); // Generate version specific unique A2L file name
    sprintf(gA2LPathname, "%s.A2L", gA2LFilename);

    if (path) {
        if (p != NULL) *p = gA2LPathname;
        if (n != NULL) *n = (uint32_t)strlen(gA2LPathname);
    }
    else {
        if (p != NULL) *p = gA2LFilename;
        if (n != NULL) *n = (uint32_t)strlen(gA2LFilename);
    }
    return 1;
}

#endif


#ifdef XCP_ENABLE_FILE_UPLOAD // Enable GET_ID A2L content upload to host

static uint8_t* gXcpFile = NULL; // file content
static uint32_t gXcpFileLength = 0; // file length
#ifdef _WIN
static HANDLE hFile, hFileMapping;
#endif

uint8_t ApplXcpReadFile(uint8_t type, uint8_t** p, uint32_t* n) {

    const char* filename = NULL;

    switch (type) {
    case IDT_ASAM_UPLOAD:
        filename = gA2LPathname; break;

    default: return 0;
    }

#ifdef XCP_ENABLE_TESTMODE
        if (gDebugLevel >= 1) printf("Load %s\n", filename);
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
        gXcpFileLength = (uint32_t)GetFileSize(hFile, NULL)-2;
        hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, gXcpFileLength, NULL);
        if (hFileMapping == NULL) return 0;
        gXcpFile = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
        if (gXcpFile == NULL) return 0;
#endif
#ifdef XCP_ENABLE_TESTMODE
            if (gDebugLevel >= 1) printf("  file %s ready for upload, size=%u, mta=%p\n\n", filename, gXcpFileLength, gXcpFile);
#endif


    *n = gXcpFileLength;
    *p = gXcpFile;
    return 1;
}

#endif
