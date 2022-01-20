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
#include "platform.h"
#include "clock.h"
#include "xcpLite.h"


 /**************************************************************************/
 // General Callbacks
 /**************************************************************************/

BOOL ApplXcpConnect() { 
    return TRUE; 
}

BOOL ApplXcpPrepareDaq() { 
    return TRUE;

}

BOOL ApplXcpStartDaq() { 
    return TRUE; 
}

BOOL ApplXcpStopDaq() { 
    return TRUE; 
}


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
    return LOCAL_CLOCK_STATE_FREE_RUNNING;
}

#ifdef XCP_ENABLE_PTP
BOOL ApplXcpGetClockInfoGrandmaster(uint8_t* uuid, uint8_t* epoch, uint8_t* stratum) {
    return FALSE;
}
#endif


/**************************************************************************/
// Pointer - Address conversion
/**************************************************************************/

#ifdef XCP_ENABLE_CAL_PAGE
#include "ecu.h"
#endif

 // 64 Bit and 32 Bit platform pointer to XCP/A2L address conversions
 // XCP memory access is limited to a 4GB address range

 // The XCP addresses for Win32 and Win64 versions of XCPlite are defined as relative to the load address of the main module
 // This allows using Microsoft linker PDB files for address update
 // In Microsoft Visual Studio set option "Generate Debug Information" to "optimized for sharing and publishing (/DEBUG:FULL)"
 // In CANape select "Microsoft PDB extented"

uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr) {
    (void)addr_ext;
#ifdef XCP_ENABLE_CAL_PAGE
    return ecuParAddrMapping(ApplXcpGetBaseAddr() + addr);
#else
    return ApplXcpGetBaseAddr() + addr;
#endif
}


#ifdef _WIN

uint8_t* baseAddr = NULL;
uint8_t baseAddrValid = 0;

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
        if (ApplXcpGetDebugLevel() >= 1) printf("ApplXcpGetBaseAddr() = 0x%I64X\n", (uint64_t)baseAddr);
#endif
    }
    return baseAddr;
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
#ifdef XCP_ENABLE_TESTMODE
    if (gDebugLevel >= 1) {
        printf("0x%zX %s 0x%X %d %d %d %d 0x%X\n",
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
          printf("Application base addr = 0x%zx\n", pinfo->dlpi_addr);
      }
#endif

    gModuleProperties[0].baseAddr = (uint8_t*) pinfo->dlpi_addr;
  }

  ()size;
  ()data;
  return 0;
}

void ApplXcpInitBaseAddressList()
{
#ifdef XCP_ENABLE_TESTMODE
    if (gDebugLevel >= 1) printf("Module List:\n");
#endif

    dl_iterate_phdr(dump_phdr, NULL);
}

#endif




/**************************************************************************/
// Calibration page handling
/**************************************************************************/

#ifdef XCP_ENABLE_CAL_PAGE

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
// Infos for GET_ID
/**************************************************************************/

#ifdef XCP_ENABLE_IDT_A2L_NAME // Enable GET_ID A2L filename without extension

const char* ApplXcpGetA2lName() {
    return ApplXcpGetName();
}


const char* ApplXcpGetA2lFileName() {
    static char a2lPath[MAX_PATH + 100 + 4]; // Full path + name + extension
    sprintf(a2lPath, "%s.a2l", ApplXcpGetA2lName());
    return a2lPath;
}
#endif

const char* ApplXcpGetName() {

        return (char*)"XCPlite";
}

/**************************************************************************/
// Read A2L to memory accessible by XCP
/**************************************************************************/

#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable GET_ID A2L content upload to host

static uint8_t* gXcpFile = NULL; // file content
static uint32_t gXcpFileLength = 0; // file length
#ifdef _WIN
static HANDLE hFile, hFileMapping;
#endif

uint8_t ApplXcpGetA2lUpload(uint8_t** p, uint32_t* n) {

    const char *filename = ApplXcpGetA2lFileName();
   
#ifdef XCP_ENABLE_TESTMODE
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
        gXcpFileLength = (uint32_t)GetFileSize(hFile, NULL)-2;
        hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, gXcpFileLength, NULL);
        if (hFileMapping == NULL) return 0;
        gXcpFile = (uint8_t*)MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
        if (gXcpFile == NULL) return 0;
#endif
#ifdef XCP_ENABLE_TESTMODE
            if (ApplXcpGetDebugLevel() >= 1) printf("  file %s ready for upload, size=%u\n\n", filename, gXcpFileLength);
#endif


    *n = gXcpFileLength;
    *p = gXcpFile;
    return 1;
}

#endif
