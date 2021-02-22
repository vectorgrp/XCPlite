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

// UDP server
#include "udpserver.h"
#include "udpraw.h"


 /**************************************************************************/
 // XCP server
 /**************************************************************************/

// Parameters
static unsigned short gSocketPort = 5555; // UDP port
static unsigned int gSocketTimeout = 0; // General socket timeout
static unsigned int gFlushCycle = 100 * kApplXcpDaqTimestampTicksPerMs; // send a DTO packet at least every 100ms
static unsigned int gCmdCycle = 10 * kApplXcpDaqTimestampTicksPerMs; // check for commands every 10ms

static unsigned int gFlushTimer = 0;
static unsigned int gCmdTimer = 0;

static int gTaskCycleTimerServer = 10000; // ns

void sleepns(int ns) {
    struct timespec timeout, timerem;
    timeout.tv_sec = 0;
    timeout.tv_nsec = ns;
    nanosleep(&timeout, &timerem);
}


// XCP command handler task
void* xcpServer(void* __par) {

    printf("Start XCP server\n");
    udpServerInit(gSocketPort, gSocketTimeout);

    // Server loop
    for (;;) {
        sleepns(gTaskCycleTimerServer);

        ApplXcpGetClock();
        if (gClock - gCmdTimer > gCmdCycle) {
            gCmdTimer = gClock;
            if (udpServerHandleXCPCommands() < 0) break;  // Handle XCP commands
        }

        if (gXcp.SessionStatus & SS_DAQ) {

#ifdef DTO_SEND_QUEUE                
            // Transmit completed UDP packets from the transmit queue
            udpServerHandleTransmitQueue();
#endif

            // Cyclic flush of incomlete packets from transmit queue or transmit buffer to keep tool visualizations up to date
            // No priorisation of events implemented, no latency optimizations
            if (gClock - gFlushTimer > gFlushCycle && gFlushCycle > 0) {
                gFlushTimer = gClock;
#ifdef DTO_SEND_QUEUE  
                udpServerFlushTransmitQueue();
#else
                udpServerFlushPacketBuffer();
#endif
            } // Flush

        } // DAQ

    } // for (;;)

    sleepns(100000000);
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


static struct timespec gts0;
static struct timespec gtr;

void ApplXcpClockInit( void )
{    
    assert(sizeof(long long) == 8);
    clock_getres(CLOCK_REALTIME, &gtr);
    assert(gtr.tv_sec == 0);
    assert(gtr.tv_nsec == 1);
    clock_gettime(CLOCK_REALTIME, &gts0);

    ApplXcpGetClock64();

#ifdef XCP_ENABLE_TESTMODE
    if (gXcpDebugLevel >= 1) {
        printf("clock resolution = %lds,%ldns\n", gtr.tv_sec, gtr.tv_nsec);
        //printf("clock now = %lds+%ldns\n", gts0.tv_sec, gts0.tv_nsec);
        //printf("clock year = %u\n", 1970 + gts0.tv_sec / 3600 / 24 / 365 );
        //printf("gClock64 = %lluus %llxh, gClock = %xh\n", gts0.tv_sec, gts0.tv_nsec, gClock64, gClock64, gClock);
    }
#endif

}

// Free runing clock with 10ns tick
// 1ns with overflow every 4s is critical for CANape measurement start time offset calculation
vuint32 ApplXcpGetClock(void) {

    struct timespec ts; 
    
    clock_gettime(CLOCK_REALTIME, &ts);
    gClock64 = ( ( (vuint64)(ts.tv_sec/*-gts0.tv_sec*/) * 1000000ULL ) + (vuint64)(ts.tv_nsec / 1000) ); // us
    gClock = (vuint32)gClock64;
    return gClock;  
}

vuint64 ApplXcpGetClock64(void) {
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    gClock64 = (((vuint64)(ts.tv_sec/*-gts0.tv_sec*/) * 1000000ULL) + (vuint64)(ts.tv_nsec / 1000)); // us
    gClock = (vuint32)gClock64;
    return gClock64;
}



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

