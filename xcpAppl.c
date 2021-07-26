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

#include "main.h"

#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO


 /**************************************************************************/
 // Clock
 /**************************************************************************/

vuint8 ApplXcpGetClockInfo(T_CLOCK_INFO_SLAVE* s, T_CLOCK_INFO_GRANDMASTER* m) {
       
    vuint8 res = 0;
    
     {
        memcpy(s->UUID, gXcpTl.SlaveUUID, 8);
        memcpy(m->UUID, gXcpTl.SlaveUUID, 8);
#ifdef CLOCK_USE_UTC_TIME_NS
        s->stratumLevel = XCP_STRATUM_LEVEL_UTC;
        m->stratumLevel = XCP_STRATUM_LEVEL_UTC;
        m->epochOfGrandmaster = XCP_EPOCH_TAI;
#else
        s->stratumLevel = XCP_STRATUM_LEVEL_UNSYNC;
        m->stratumLevel = XCP_STRATUM_LEVEL_UNSYNC;
        m->epochOfGrandmaster = XCP_EPOCH_ARB;
#endif
    }

    if (gDebugLevel >= 1) {
        ApplXcpPrint("  Slave-UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X Grandmaster-UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",
            s->UUID[0], s->UUID[1], s->UUID[2], s->UUID[3], s->UUID[4], s->UUID[5], s->UUID[6], s->UUID[7],
            m->UUID[0], m->UUID[1], m->UUID[2], m->UUID[3], m->UUID[4], m->UUID[5], m->UUID[6], m->UUID[7]);
    }
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

vuint8* baseAddr = NULL;
vuint8 baseAddrValid = FALSE;

vuint8* ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr) {

    return ApplXcpGetBaseAddr() + addr;
}

vuint32 ApplXcpGetAddr(vuint8* p) {

    assert(p >= ApplXcpGetBaseAddr());
#ifdef _WIN64
    assert(((vuint64)p - (vuint64)ApplXcpGetBaseAddr()) <= 0xffffffff); // be sure that XCP address range is sufficient
#endif
    return (vuint32)(p - ApplXcpGetBaseAddr());
}

// Get base pointer for the XCP address range
// This function is time sensitive, as it is called once on every XCP event
vuint8* ApplXcpGetBaseAddr() {

    if (!baseAddrValid) {
        baseAddr = (vuint8*)GetModuleHandle(NULL);
        baseAddrValid = TRUE;
#if defined ( XCP_ENABLE_TESTMODE )
        if (gDebugLevel >= 1) ApplXcpPrint("ApplXcpGetBaseAddr() = 0x%I64X\n", (vuint64)baseAddr);
#endif
    }
    return baseAddr;
}

#endif

#ifdef _LINUX64

#define __USE_GNU
#include <link.h>

vuint8* baseAddr = NULL;
vuint8 baseAddrValid = FALSE;

static int dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data)
{
  // printf("name=%s (%d segments)\n", pinfo->dlpi_name, pinfo->dlpi_phnum);

  // Application modules has no name
  if (0 == strlen(pinfo->dlpi_name)) {
    baseAddr = (vuint8*)pinfo->dlpi_addr;
  }

  (void)size;
  (void)data;
  return 0;
}

vuint8* ApplXcpGetBaseAddr() {
  
  if (!baseAddrValid) {
    dl_iterate_phdr(dump_phdr, NULL);
    assert(baseAddr!=NULL);
    baseAddrValid = TRUE;
    ApplXcpPrint("BaseAddr = %lX\n",(vuint64)baseAddr);
  }

  return baseAddr;
}

vuint32 ApplXcpGetAddr(vuint8* p)
{
  ApplXcpGetBaseAddr();
  return (vuint32)(p - baseAddr);
}

vuint8* ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr)
{
  ApplXcpGetBaseAddr();
  return baseAddr + addr;
}


#endif





/**************************************************************************/
// Calibration page handling
/**************************************************************************/

#ifdef XCP_ENABLE_CAL_PAGE

vuint8 calPage = 0; // RAM = 0, FLASH = 1

vuint8 ApplXcpGetCalPage(vuint8 segment, vuint8 mode) {
    return calPage;
}

vuint8 ApplXcpSetCalPage(vuint8 segment, vuint8 page, vuint8 mode) {
    calPage = page;
    return 0;
  }
#endif


/**************************************************************************/
// Eventlist
/**************************************************************************/

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

vuint16 ApplXcpEventCount = 0;
tXcpEvent ApplXcpEventList[XCP_MAX_EVENT];


// Create event, <rate> in us, 0 = sporadic 
vuint16 XcpCreateEvent(const char* name, vuint16 cycleTime /*ms */, vuint16 sampleCount, vuint32 size) {

    // Convert to ASAM coding time cycle and time unit
    // RESOLUTION OF TIMESTAMP "UNIT_1US" = 3,"UNIT_10US" = 4,"UNIT_100US" = 5,"UNIT_1MS" = 6,"UNIT_10MS" = 7,"UNIT_100MS" = 8, 
    vuint8 timeUnit = 3;
    while (cycleTime >= 256) {
        cycleTime /= 10;
        timeUnit++;
    }

    if (ApplXcpEventCount >= XCP_MAX_EVENT) return (vuint16)0xFFFF; // Out of memory 
    ApplXcpEventList[ApplXcpEventCount].name = name;
    ApplXcpEventList[ApplXcpEventCount].timeUnit = timeUnit;
    ApplXcpEventList[ApplXcpEventCount].timeCycle = (vuint8)cycleTime;
    ApplXcpEventList[ApplXcpEventCount].sampleCount = sampleCount;
    ApplXcpEventList[ApplXcpEventCount].size = size;

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel>=1) ApplXcpPrint("Event %u: %s unit=%u cycle=%u samplecount=%u\n", ApplXcpEventCount, ApplXcpEventList[ApplXcpEventCount].name, ApplXcpEventList[ApplXcpEventCount].timeUnit, ApplXcpEventList[ApplXcpEventCount].timeCycle, ApplXcpEventList[ApplXcpEventCount].sampleCount);
#endif

    return ApplXcpEventCount++; // Return XCP event number
}

#endif



/**************************************************************************/
// Read A2L to memory accessible by XCP
/**************************************************************************/

#ifdef XCP_ENABLE_A2L_NAME // Enable GET_ID A2L name upload to host

static char gA2LFilename[100]; // Name without extension

// A2L base name for GET_ID 
static char gA2LPathname[MAX_PATH + 100 + 4]; // Full path + name +extension


vuint8 ApplXcpGetA2LFilename(char** p, vuint32* n, int path) {

    // Create a unique A2L file name for this build
    sprintf(gA2LFilename, APP_NAME "-%08X-%u", ApplXcpGetAddr((vuint8*)&ecuPar),gOptionSlavePort); // Generate version specific unique A2L file name
    sprintf(gA2LPathname, "%s%s.A2L", gOptionA2L_Path, gA2LFilename);

    if (path) {
        if (p != NULL) *p = (vuint8*)gA2LPathname;
        if (n != NULL) *n = (vuint32)strlen(gA2LPathname);
    }
    else {
        if (p != NULL) *p = (vuint8*)gA2LFilename;
        if (n != NULL) *n = (vuint32)strlen(gA2LFilename);
    }
    return 1;
}

#endif


#ifdef XCP_ENABLE_FILE_UPLOAD // Enable GET_ID A2L content upload to host

static vuint8* gXcpFile = NULL; // file content
static vuint32 gXcpFileLength = 0; // file length
#ifdef _WIN
static HANDLE hFile, hFileMapping;
#endif

vuint8 ApplXcpReadFile(vuint8 type, vuint8** p, vuint32* n) {

    const char* filename = gA2LPathname;

#if defined ( XCP_ENABLE_TESTMODE )
        if (gDebugLevel >= 1) ApplXcpPrint("Load %s\n", filename);
#endif

#ifdef _LINUX // Linux
        if (gXcpFile) free(gXcpFile);
        FILE* fd;
        fd = fopen(filename, "r");
        if (fd == NULL) {
            ApplXcpPrint("ERROR: file %s not found!\n", filename);
            return 0;
        }
        struct stat fdstat;
        stat(filename, &fdstat);
        gXcpFile = (vuint8*)malloc((size_t)(fdstat.st_size + 1));
        gXcpFileLength = (vuint32)fread(gXcpFile, 1, (uint32_t)fdstat.st_size, fd);
        fclose(fd);
#else
        wchar_t wcfilename[256] = { 0 };
        if (gXcpFile) {
            UnmapViewOfFile(gXcpFile);
            CloseHandle(hFileMapping);
            CloseHandle(hFile);
        }
        MultiByteToWideChar(0, 0, filename, (int)strlen(filename), wcfilename, (int)strlen(filename));
        hFile = CreateFile(wcfilename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            ApplXcpPrint("file %s not found!\n", filename);
            return 0;
        }
        gXcpFileLength = (vuint32)GetFileSize(hFile, NULL)-2;
        hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, gXcpFileLength, NULL);
        if (hFileMapping == NULL) return 0;
        gXcpFile = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
        if (gXcpFile == NULL) return 0;
#endif
#if defined ( XCP_ENABLE_TESTMODE )
            if (gDebugLevel >= 1) ApplXcpPrint("  file %s ready for upload, size=%u, mta=%p\n\n", filename, gXcpFileLength, gXcpFile);
#endif
        
    
    *n = gXcpFileLength;
    *p = gXcpFile;
    return 1;
}

#endif




