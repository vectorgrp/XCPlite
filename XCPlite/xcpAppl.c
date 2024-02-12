/*----------------------------------------------------------------------------
| File:
|   xcpAppl.c
|
| Description:
|   Application specific functions and callbacks for XCP driver
|   
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "dbg_print.h"
#include "xcpLite.h"
    

#if OPTION_ENABLE_DBG_PRINTS
unsigned int gDebugLevel = OPTION_DEBUG_LEVEL;
#endif

/**************************************************************************/
// General Callbacks from XCPlite.c
/**************************************************************************/


BOOL ApplXcpConnect() {
    DBG_PRINT1("XCP connect\n");
    return TRUE;
}

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
BOOL ApplXcpPrepareDaq() { 
    DBG_PRINT1("XCP prepare DAQ\n");
    return TRUE;
}
#endif

BOOL ApplXcpStartDaq() {
    DBG_PRINT1("XCP start DAQ\n");
    return TRUE;
}

void ApplXcpStopDaq() {
    DBG_PRINT1("XCP stop DAQ\n");
}




/**************************************************************************/
// Clock
// Get clock for DAQ timestamps
/**************************************************************************/

// XCP server clock timestamp resolution defined in xcp_cfg.h
// Clock must be monotonic !!!

uint64_t ApplXcpGetClock64() { 

    return clockGet();
}

uint8_t ApplXcpGetClockState() { 

    return CLOCK_STATE_FREE_RUNNING; // Clock is a free running counter 
}

BOOL ApplXcpGetClockInfoGrandmaster(uint8_t* uuid, uint8_t* epoch, uint8_t* stratum) {

    (void)uuid;
    (void)epoch;
    (void)stratum;

    return FALSE; // No PTP support implemented
}


/**************************************************************************/
// Pointer - Address conversion
/**************************************************************************/

// 64 Bit and 32 Bit platform pointer to XCP/A2L address conversions
// XCP memory access is limited to a 4GB address range (32 Bit)

// The XCP addresses with extension = 0 for Win32 and Win64 versions of XCPlite are defined as relative to the load address of the main module
// This allows using Microsoft linker PDB files for address update
// In Microsoft Visual Studio set option "Generate Debug Information" to "optimized for sharing and publishing (/DEBUG:FULL)"



uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr) {

    if (addr_ext != 0) return NULL;
    return ApplXcpGetBaseAddr() + addr;
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
        DBG_PRINTF4("ApplXcpGetBaseAddr() = 0x%I64X\n", (uint64_t)baseAddr);
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

#if defined(_LINUX64) && !defined(__APPLE__)

#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <link.h>

uint8_t* baseAddr = NULL;
uint8_t baseAddrValid = 0;

static int dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data)
{
    // DBG_PRINTF1("name=%s (%d segments)\n", pinfo->dlpi_name, pinfo->dlpi_phnum);

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
        DBG_PRINTF1("BaseAddr = %lX\n", (uint64_t)baseAddr);
    }

    return baseAddr;
}

uint32_t ApplXcpGetAddr(uint8_t* p)
{
    ApplXcpGetBaseAddr();
    return (uint32_t)(p - baseAddr);
}

#endif


#ifdef __APPLE__

static uint8_t __base_addr_val = 0;

uint8_t* ApplXcpGetBaseAddr()
{
    return ((uint8_t*)((uint64_t)(&__base_addr_val)&0xffffffff00000000));
}

uint32_t ApplXcpGetAddr(uint8_t* p)
{
    return ((uint32_t)((uint64_t) p)& 0xffffffff);
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
// Calibration page switching
/**************************************************************************/

// Not implemented

uint8_t ApplXcpGetCalPage(uint8_t segment, uint8_t mode) {
  (void)segment;
  (void)mode;
  return CRC_PAGE_NOT_VALID;
}

uint8_t ApplXcpSetCalPage(uint8_t segment, uint8_t page, uint8_t mode) {
  (void)segment;
  (void)page;
  (void)mode;
  return CRC_PAGE_NOT_VALID;
}



/**************************************************************************/
// Provide infos for GET_ID
// The XCP command GET_ID provides different type of identification
// information to the XCP client 
// Returns 0, when the information is not available
/**************************************************************************/


#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable GET_ID A2L content upload to host

static uint8_t* gXcpFile = NULL; // file content
static uint32_t gXcpFileLength = 0; // file length

BOOL ApplXcpReadA2L(uint8_t size, uint32_t addr, uint8_t* data) {
  if (addr + size > gXcpFileLength) return FALSE;
  memcpy(data, gXcpFile + addr, size);
  return TRUE;
}

void releaseFile(uint8_t* file) {

  if (file != NULL) {
    free(file);
  }
}

uint8_t* loadFile(const char* filename, uint32_t* length) {

  uint8_t* fileBuf = NULL; // file content
  uint32_t fileLen = 0; // file length

  DBG_PRINTF1("Load %s\n", filename);

#if defined(_LINUX) // Linux

  FILE* fd;
  fd = fopen(filename, "r");
  if (fd == NULL) {
    DBG_PRINTF_ERROR("ERROR: file %s not found!\n", filename);
    return NULL;
  }
  struct stat fdstat;
  stat(filename, &fdstat);
  fileBuf = (uint8_t*)malloc((size_t)(fdstat.st_size + 1));
  if (fileBuf == NULL) return NULL;
  fileLen = (uint32_t)fread(fileBuf, 1, (uint32_t)fdstat.st_size, fd);
  fclose(fd);

#elif defined(_WIN) // Windows

  wchar_t wcfilename[256] = { 0 };
  MultiByteToWideChar(0, 0, filename, (int)strlen(filename), wcfilename, (int)strlen(filename));
  HANDLE hFile = CreateFileW((wchar_t*)wcfilename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    DBG_PRINTF_ERROR("file %s not found!\n", filename);
    return NULL;
  }
  fileLen = (uint32_t)GetFileSize(hFile, NULL);
  fileBuf = (uint8_t*)malloc(fileLen + 1);
  if (fileBuf == NULL) {
    DBG_PRINTF_ERROR("Error: out of memory!\n");
    CloseHandle(hFile);
    return NULL;
  }
  if (!ReadFile(hFile, fileBuf, fileLen, NULL, NULL)) {
    DBG_PRINTF_ERROR("Error: could not read from %s!\n", filename);
    free(fileBuf);
    CloseHandle(hFile);
    return NULL;
  }
  fileBuf[fileLen] = 0;
  CloseHandle(hFile);

#endif

  DBG_PRINTF3("  file %s ready for upload, size=%u\n\n", filename, fileLen);

  *length = fileLen;
  return fileBuf;
}

#endif


uint32_t ApplXcpGetId(uint8_t id, uint8_t* buf, uint32_t bufLen) {

    uint32_t len = 0;
    switch (id) {

#ifdef OPTION_A2L_NAME
    case IDT_ASCII:
    case IDT_ASAM_NAME:
      len = (uint32_t)strlen(OPTION_A2L_NAME);
      if (buf) {
        if (len > bufLen) return 0; // Insufficient buffer space
        strncpy((char*)buf, OPTION_A2L_NAME, len);
      }
      break;
#endif

#ifdef OPTION_A2L_FILE_NAME
    case IDT_ASAM_PATH:
      len = (uint32_t)strlen(OPTION_A2L_FILE_NAME);
      if (buf) {
        if (len > bufLen) return 0; // Insufficient buffer space
        strncpy((char*)buf, OPTION_A2L_FILE_NAME, len);
      }
      break;
#endif

    case IDT_ASAM_EPK:
      // Not implemented
      break;

#if defined(XCP_ENABLE_IDT_A2L_UPLOAD) && defined(OPTION_A2L_FILE_NAME)
    case IDT_ASAM_UPLOAD:
      if (NULL == (gXcpFile = loadFile(OPTION_A2L_FILE_NAME, &gXcpFileLength))) return 0;
      len = gXcpFileLength;
      break;
#endif

    }
    return len;
}



