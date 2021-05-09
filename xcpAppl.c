/*----------------------------------------------------------------------------
| File:
|   xcpAppl.cpp
|
| Description:
|   Platform and implementation specific functions for the XCP driver
|   All other callbacks are implemented as macros
|   Demo for XCP on Ethernet (UDP)
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/


#include "xcpAppl.h"
#include "A2L.h"

// UDP server
#include "udpserver.h"


 /**************************************************************************/
 // XCP transport layer
 /**************************************************************************/

// Parameters
static unsigned int gFlushCycle = 100; // 100ms, send a DTO packet at least every 100ms
static unsigned int gServerWaitTimeout = 500; // 500 us, sleep timeout of server thread

#ifndef _WIN
// @@@@ TODO Change to event triggered
static unsigned int gCmdCycle = 10 * kApplXcpDaqTimestampTicksPerMs; // 10ms, check for commands every 10ms
static unsigned int gTransmitCycle = 500 * (kApplXcpDaqTimestampTicksPerMs/1000); // us, check for new transmit data in transmit queue every 500us
// Max transmit rate is approximtly limited to XCP_DAQ_QUEUE_SIZE * MTU%MAX_DTO * 1000000/gTransmitCycle = 2000*32*1250 = 80MByte/s
#endif

static unsigned int gTransmitTimer = 0;
static unsigned int gFlushTimer = 0;
static unsigned int gCmdTimer = 0;



// XCP transport layer task init
int xcpTransportLayerInit(void) {

    printf("Init XCP on UDP transport layer (UDP MTU = %u, DTO queue size is %u (%uKiB))\n", kXcpMaxMTU, XCP_DAQ_QUEUE_SIZE, sizeof(tXcpDtoBuffer) * XCP_DAQ_QUEUE_SIZE / 1024);

    // Create A2L parameters to control the XCP server
#ifdef XCP_ENABLE_A2L
    A2lCreateParameter(gFlushCycle, "ms", "DAQ queue flush cycle time");
#ifndef _WIN
    A2lCreateParameter(gCmdCycle, "us", "XCP command handler cycle time");
    A2lCreateParameter(gTransmitCycle, "us", "DAQ transmit cycle time");
#endif
    A2lCreateParameter(gServerWaitTimeout, "us", "Server thread sleep and wait timeout");
    A2lParameterGroup("Server_Parameters", 2, "gFlushCycle", "gServerWaitTimeout");
#endif

    return udpServerInit();
}

// XCP transport layer task
// Handle commands, transmit data, flush data
void* xcpTransportLayerThread(void* par) {

    printf("Start XCP server\n");

    // Server loop
    for (;;) {

        udpServerWaitForEvent(gServerWaitTimeout);
        ApplXcpGetClock();
        
#ifndef _WIN
        // Handle XCP commands every gCmdCycle time period
        if (gClock - gCmdTimer > gCmdCycle) {
            gCmdTimer = gClock;
            if (!udpServerHandleXCPCommands()) break;  
        } 
#else
        if (!udpServerHandleXCPCommands()) break;
#endif

        // If DAQ measurement is running
        if (gXcp.SessionStatus & SS_DAQ) {

#ifdef DTO_SEND_QUEUE 
#ifndef _WIN
            // Transmit all completed UDP packets from the transmit queue every gTransmitCycle time period
            if (gClock-gTransmitTimer>gTransmitCycle) {
                gTransmitTimer = gClock;
                udpServerHandleTransmitQueue();
            }
#else
            udpServerHandleTransmitQueue();
#endif
#endif
            // Every gFlushCycle in us time period
            // Cyclic flush of incomplete packets from transmit queue or transmit buffer to keep tool visualizations up to date
            // No priorisation of events implemented, no latency optimizations
            if (gFlushCycle>0 && gClock-gFlushTimer>gFlushCycle*1000) {
                gFlushTimer = gClock;
#ifdef DTO_SEND_QUEUE  
                udpServerFlushTransmitQueue();
#else
                udpServerFlushPacketBuffer();
#endif
            } // Flush

        } // DAQ

    } // for (;;)

    ApplXcpSleepNs(100000000);
    udpServerShutdown();
    return 0;
}


/**************************************************************************/
// DAQ clock
/**************************************************************************/

/* Compile with:   -lrt */

// Wall clock updated at every AppXcpTimer
volatile vuint32 gClock = 0;
volatile vuint64 gClock64 = 0;

#ifndef _WIN // Linux

static struct timespec gts0;
static struct timespec gtr;

int ApplXcpClockInit( void )
{    
    assert(sizeof(long long) == 8);
    clock_getres(CLOCK_REALTIME, &gtr);
    assert(gtr.tv_sec == 0);
    assert(gtr.tv_nsec == 1);
    clock_gettime(CLOCK_REALTIME, &gts0);

    ApplXcpGetClock64();

#ifdef XCP_ENABLE_TESTMODE
    if (gXcpDebugLevel >= 1) {
        printf("system realtime clock resolution = %lds,%ldns\n", gtr.tv_sec, gtr.tv_nsec);
        //printf("clock now = %lds+%ldns\n", gts0.tv_sec, gts0.tv_nsec);
        //printf("clock year = %u\n", 1970 + gts0.tv_sec / 3600 / 24 / 365 );
        //printf("gClock64 = %lluus %llxh, gClock = %xh\n", gts0.tv_sec, gts0.tv_nsec, gClock64, gClock64, gClock);
    }
#endif

    return 1;
}

// Free running clock with 1us tick
vuint32 ApplXcpGetClock(void) {

    struct timespec ts; 
    
    clock_gettime(CLOCK_REALTIME, &ts);
    gClock64 = ( ( (vuint64)(ts.tv_sec/*-gts0.tv_sec*/) * 1000000ULL ) + (vuint64)(ts.tv_nsec / 1000) ); // us
    gClock = (vuint32)gClock64;
    return gClock;  
}

vuint64 ApplXcpGetClock64(void) {

    ApplXcpGetClock();
    return gClock64;
}

void ApplXcpSleepNs(unsigned int ns) {
    struct timespec timeout, timerem;
    timeout.tv_sec = 0;
    timeout.tv_nsec = (long)ns;
    nanosleep(&timeout, &timerem);
}

#else

static __int64 sFactor = 1;
static __int64 sOffset = 0;

int ApplXcpClockInit(void) {

    LARGE_INTEGER tF, tC;

    if (QueryPerformanceFrequency(&tF)) {
        if (tF.u.HighPart) {
            printf("error: Unexpected Performance Counter frequency\n");
            return 0;
        }
        sFactor = tF.u.LowPart; // Ticks pro s
        QueryPerformanceCounter(&tC);
        sOffset = (((__int64)tC.u.HighPart) << 32) | (__int64)tC.u.LowPart;
        ApplXcpGetClock64();
#ifdef XCP_ENABLE_TESTMODE
        if (gXcpDebugLevel >= 1) {
            printf("system realtime clock resolution = %I64u ticks per s\n", sFactor);
            printf("gClock64 = %I64u, gClock = %u\n", gClock64, gClock);
        }
#endif

    }
    else {
        printf("error: Performance Counter not available\n");
        return 0;
    }
    return 1;
}

// Free running clock with 1us tick
vuint32 ApplXcpGetClock(void) {
   
    LARGE_INTEGER t;
    __int64 td;

    QueryPerformanceCounter(&t);
    td = (((__int64)t.u.HighPart) << 32) | (__int64)t.u.LowPart;
    gClock64 = (vuint64)(((td - sOffset) * 1000000UL) / sFactor);
    gClock = (vuint32)gClock64; 
    return gClock;
}

// Free running clock with 1us in int64 which never overflows :-)
vuint64 ApplXcpGetClock64(void) {

    ApplXcpGetClock();
    return gClock64;
}


void ApplXcpSleepNs(unsigned int ns) {

    vuint64 t1, t2;
    t1 = t2 = ApplXcpGetClock64();

    vuint32 us = ns / 1000;
    vuint32 ms = us / 1000;

    // Start sleeping at 1800us, shorter sleeps are more precise but need significant CPU time
    if (us >= 1800) { 
        if (ms >= 2) ms--; // PC always sleeps +1 ms  ????
        Sleep(ms);  
    }
    // Busy wait
    else {
        vuint64 te = t1 + us;
        for (;;) {
            t2 = ApplXcpGetClock64();
            if (t2 >= te) break;
            if (te - t2 > 0) Sleep(0);  
        }
    }
}


#endif


/**************************************************************************/
// Read A2L to memory accessible by XCP
/**************************************************************************/

#ifdef XCP_ENABLE_A2L

char* gXcpA2L = NULL; // A2L file content
unsigned int gXcpA2LLength = 0; // A2L file length

int ApplXcpReadA2LFile(char** p, unsigned int* n) {

    if (gXcpA2L == NULL) {

#ifndef _WIN // Linux
        FILE* fd;
        fd = fopen(kXcpA2LFilenameString, "r");
        if (fd == NULL) return 0;
        struct stat fdstat;
        stat(kXcpA2LFilenameString, &fdstat);
        gXcpA2L = malloc((size_t)(fdstat.st_size + 1));
        gXcpA2LLength = fread(gXcpA2L, sizeof(char), (size_t)fdstat.st_size, fd);
        fclose(fd);

        //free(gXcpA2L);
#else
        HANDLE hFile, hFileMapping;
        hFile = CreateFile(TEXT(kXcpA2LFilenameString), GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return 0;
        gXcpA2LLength = GetFileSize(hFile, NULL);
        hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, gXcpA2LLength + sizeof(WCHAR), NULL);
        gXcpA2L = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
        if (gXcpA2L == NULL) return 0;

        //UnmapViewOfFile(gXcpA2L);
        //CloseHandle(hFileMapping);
        //CloseHandle(hFile);
#endif
#if defined ( XCP_ENABLE_TESTMODE )
            if (gXcpDebugLevel >= 1) {
                ApplXcpPrint("A2L file %s ready for upload, size=%u, mta=%p\n", kXcpA2LFilenameString, gXcpA2LLength, gXcpA2L);
            }
#endif
        
    }
    *n = gXcpA2LLength;
    *p = gXcpA2L;
    return 1;
}
#endif



/**************************************************************************/
// Pointer to XCP address conversions
/**************************************************************************/

#ifdef XCP_ENABLE_SO

#define __USE_GNU
#include <link.h>

// Address information of loaded modules for XCP (application and shared libraries)
// Index is XCP address extension
// Index 0 is application

static struct
{
    const char* name;
    BYTEPTR baseAddr;
} 
gModuleProperties[XCP_MAX_MODULE] = { {} };


vuint8 ApplXcpGetExt(BYTEPTR addr)
{
    // Here we have the possibility to loop over the modules and find out the extension
    (void)addr;
    return 0;
}

vuint32 ApplXcpGetAddr(BYTEPTR addr)
{
    vuint8 addr_ext = ApplXcpGetExt(addr);
    union {
        BYTEPTR ptr;
        vuint32 i;
    } rawAddr;
    rawAddr.ptr = (BYTEPTR)(addr - gModuleProperties[addr_ext].baseAddr);
    return rawAddr.i;
}

BYTEPTR ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr)
{
    BYTEPTR baseAddr = 0;
    if (addr_ext < XCP_MAX_MODULE) {
        baseAddr = gModuleProperties[addr_ext].baseAddr;
    }
    return baseAddr + addr;
}


static int dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data)
{
#ifdef XCP_ENABLE_TESTMODE
    if (gXcpDebugLevel >= 1) {
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
      if (gXcpDebugLevel >= 1) {
          printf("Application base addr = 0x%zx\n", pinfo->dlpi_addr);
      }
#endif

    gModuleProperties[0].baseAddr = (BYTEPTR) pinfo->dlpi_addr;
  }

  (void)size;
  (void)data;
  return 0;
}

void ApplXcpInitBaseAddressList()
{
#ifdef XCP_ENABLE_TESTMODE
    if (gXcpDebugLevel >= 1) printf ("Module List:\n");
#endif

    dl_iterate_phdr(dump_phdr, NULL);
}

#endif

