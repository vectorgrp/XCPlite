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


#include <stdint.h>

 /* 8-Bit  */
typedef uint8_t  vuint8;
typedef int8_t    vsint8;

/* 16-Bit  */
typedef uint16_t vuint16;
typedef int16_t   vsint16;

/* 32-Bit  */
typedef uint32_t   vuint32;
typedef int32_t     vsint32;

/* 64-Bit  */
typedef uint64_t  vuint64;
typedef int64_t    vsint64;

typedef uint8_t  vbool;
#define TRUE 1
#define FALSE 0



 
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
        if (gDebugLevel >= 1) ApplXcpPrint("  ApplXcpGetBaseAddr() = 0x%I64X\n", (vuint64)baseAddr);
#endif
    }
    return baseAddr;
}

#endif

#ifdef _LINUX64

vuint8* baseAddr = NULL;
vuint8 baseAddrValid = FALSE;

vuint8* ApplXcpGetBaseAddr() {
  
  return baseAddr;
}

vuint32 ApplXcpGetAddr(vuint8* p)
{
  vuint32 a = (vuint64)p & 0xFFFFFFFF;
  if (baseAddrValid) {
assert(baseAddr == (vuint8*)((vuint64)p & 0xFFFFFFFF00000000));

} else {
    baseAddr = (vuint8*)((vuint64)p & 0xFFFFFFFF00000000);
    baseAddrValid = TRUE;
    ApplXcpPrint("BaseAddr = %lX\n",(vuint64)baseAddr);

}

return a;

}

vuint8* ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr)
{
    return baseAddr + addr;
}


#endif

 
/**************************************************************************/
// Pointer to XCP address conversions for LINUX shared objects
/**************************************************************************/

#ifdef XCPSIM_ENABLE_SO

#define __USE_GNU
#include <link.h>

// Address information of loaded modules for XCP (application and shared libraries)
// Index is XCP address extension
// Index 0 is application

static struct
{
    const char* name;
    vuint8* baseAddr;
} 
gModuleProperties[XCP_MAX_MODULE] = { {} };


vuint8 ApplXcpGetExt(vuint8* addr)
{
    // Here we have the possibility to loop over the modules and find out the extension
    (void)addr;
    return 0;
}

vuint32 ApplXcpGetAddr(vuint8* addr)
{
    vuint8 addr_ext = ApplXcpGetExt(addr);
    union {
        vuint8* ptr;
        vuint32 i;
    } rawAddr;
    rawAddr.ptr = (vuint8*)(addr - gModuleProperties[addr_ext].baseAddr);
    return rawAddr.i;
}

vuint8* ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr)
{
    vuint8* baseAddr = 0;
    if (addr_ext < XCP_MAX_MODULE) {
        baseAddr = gModuleProperties[addr_ext].baseAddr;
    }
    return baseAddr + addr;
}


static int dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data)
{
#ifdef XCP_ENABLE_TESTMODE
    if (gDebugLevel >= 1) {
        ApplXcpPrint("0x%zX %s 0x%X %d %d %d %d 0x%X\n",
            pinfo->dlpi_addr, pinfo->dlpi_name, pinfo->dlpi_phdr, pinfo->dlpi_phnum,
            pinfo->dlpi_adds, pinfo->dlpi_subs, pinfo->dlpi_tls_modid,
            pinfo->dlpi_tls_data);
    }
#endif

  // Modules
  if (0 < strlen(pinfo->dlpi_name)) {
    // Here we could remember module information or something like that
  }  

  // Application
  else  {

#ifdef XCP_ENABLE_TESTMODE
      if (gDebugLevel >= 1) {
          ApplXcpPrint("Application base addr = 0x%zx\n", pinfo->dlpi_addr);
      }
#endif

    gModuleProperties[0].baseAddr = (vuint8*) pinfo->dlpi_addr;
  }

  (void)size;
  (void)data;
  return 0;
}

void ApplXcpInitBaseAddressList()
{
#ifdef XCP_ENABLE_TESTMODE
    if (gDebugLevel >= 1) ApplXcpPrint("Module List:\n");
#endif

    dl_iterate_phdr(dump_phdr, NULL);
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

// A2L base name for GET_ID 
static char gA2LFilename[FILENAME_MAX] = "XCPlite"; // Name without extension
static char gA2LPathname[MAX_PATH + FILENAME_MAX] = "XCPlite.A2L"; // Full path name extension


vuint8 ApplXcpGetA2LFilename(vuint8** p, vuint32* n, int path) {

    // Create a unique A2L file name for this version. Used for generation and for GET_ID A2L name
#ifdef _WIN
    sprintf_s(gA2LFilename, sizeof(gA2LFilename), "XCPlite-%08X", (vuint32)((vuint64)&gDebugLevel + (vuint64)&channel1)+ gOptionsSlavePort); // Generate version specific unique A2L file name
    sprintf_s(gA2LPathname, sizeof(gA2LPathname), "%s%s.A2L", gOptionA2L_Path, gA2LFilename);

#ifdef XCPSIM_ENABLE_A2L_GEN
    // If A2L name ist requested, generate if not exists
    FILE* f;
    if (fopen_s(&f, gA2LPathname, "r")) {
        // Create A2L file if not existing
        createA2L(gA2LPathname);
    }
    else {
        if (f != NULL) fclose(f);
    }
#endif

#endif

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


#ifdef XCP_ENABLE_A2L_UPLOAD // Enable GET_ID A2L content upload to host

static vuint8* gXcpA2L = NULL; // A2L file content
static vuint32 gXcpA2LLength = 0; // A2L file length

vuint8 ApplXcpReadA2LFile(vuint8** p, vuint32* n) {

    if (gXcpA2L == NULL) {

#ifdef _WIN
#ifdef XCPSIM_ENABLE_A2L_GEN
        ApplXcpGetA2LFilename(NULL,NULL,0); // Generate if not yet exists
#endif
#endif

#if defined ( XCP_ENABLE_TESTMODE )
        if (gDebugLevel >= 1) ApplXcpPrint("Load A2L %s\n", gA2LPathname);
#endif

#ifdef _LINUX // Linux
        FILE* fd;
        fd = fopen(gA2LPathname, "r");
        if (fd == NULL) {
            ApplXcpPrint("A2L file %s not found! Use -a2l to generate\n", gA2LPathname);
            return 0;
        }
        struct stat fdstat;
        stat(gA2LPathname, &fdstat);
        gXcpA2L = (vuint8*)malloc((size_t)(fdstat.st_size + 1));
        gXcpA2LLength = fread(gXcpA2L, sizeof(char), (size_t)fdstat.st_size, fd);
        fclose(fd);

        //free(gXcpA2L);
#else
        HANDLE hFile, hFileMapping;
        wchar_t filename[256] = { 0 };
        MultiByteToWideChar(0, 0, gA2LPathname, (int)strlen(gA2LPathname), filename, (int)strlen(gA2LPathname));
        hFile = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            ApplXcpPrint("A2L file %s not found! Use -a2l to generate\n", gA2LPathname);
            return 0;
        }
        gXcpA2LLength = GetFileSize(hFile, NULL)-2;
        hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, gXcpA2LLength, NULL);
        if (hFileMapping == NULL) return 0;
        gXcpA2L = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
        if (gXcpA2L == NULL) return 0;

        //UnmapViewOfFile(gXcpA2L);
        //CloseHandle(hFileMapping);
        //CloseHandle(hFile);
#endif
#if defined ( XCP_ENABLE_TESTMODE )
            if (gDebugLevel >= 1) ApplXcpPrint("  A2L file ready for upload, size=%u, mta=%p\n\n", gXcpA2LLength, gXcpA2L);
#endif
        
    }
    *n = gXcpA2LLength;
    *p = gXcpA2L;
    return 1;
}

#endif




