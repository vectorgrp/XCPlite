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
#include "udpraw.h"


 /**************************************************************************/
 // XCP server
 /**************************************************************************/

// Parameters
static unsigned short gSocketPort = 5555; // UDP port
static unsigned int gSocketTimeout = 0; // General socket timeout
static unsigned int gFlushCycle = 100 * kApplXcpDaqTimestampTicksPerMs; // ms, send a DTO packet at least every 100ms
static unsigned int gCmdCycle = 10 * kApplXcpDaqTimestampTicksPerMs; // ms, check for commands every 10ms
static unsigned int gTransmitCycle = 500 * (kApplXcpDaqTimestampTicksPerMs/1000); // us, check for new transmit data in transmit queue every 500us
// Max transmit rate is approximtly limited to XCP_DAQ_QUEUE_SIZE * MTU%MAX_DTO * 1000000/gTransmitCycle = 2000*32*1250 = 80MByte/s

static unsigned int gTaskCycleTimerServer = 50000; // ns, sleep time of server thread

static unsigned int gTransmitTimer = 0;
static unsigned int gFlushTimer = 0;
static unsigned int gCmdTimer = 0;


// XCP server task init
void xcpServerInit(void) {

    // Create A2L parameters to control the XCP server
#ifdef XCP_ENABLE_A2L
    A2lCreateParameter(gFlushCycle, "us", "DAQ queue flush cycle time");
    A2lCreateParameter(gCmdCycle, "us", "XCP command handler cycle time");
    A2lCreateParameter(gTransmitCycle, "us", "DAQ transmit cycle time");
    A2lCreateParameter(gTaskCycleTimerServer, "ns", "Server thread sleep time");
    A2lParameterGroup("Server_Parameters", 4, "gFlushCycle", "gCmdCycle", "gTransmitCycle", "gTaskCycleTimerServer");
#endif

    printf("Init XCP on UDP server\n");
    udpServerInit(gSocketPort, gSocketTimeout);

}

// XCP server task
// Handle command, transmit data, flush data server thread
void* xcpServerThread(void* __par) {

    printf("Start XCP server\n");
    printf("  cmd cycle = %u, transmit cycle = %d, flush cycle = %d\n", gCmdCycle, gTransmitCycle, gFlushCycle);

    // Server loop
    for (;;) {

        ApplXcpSleepNs(gTaskCycleTimerServer);
        ApplXcpGetClock();

        // Handle XCP commands every gCmdCycle time period
        if (gClock - gCmdTimer > gCmdCycle) {
            gCmdTimer = gClock;
            if (udpServerHandleXCPCommands() < 0) break;  
        } // Handle

        // If DAQ measurement is running
        if (gXcp.SessionStatus & SS_DAQ) {

#ifdef DTO_SEND_QUEUE                
            // Transmit all completed UDP packets from the transmit queue every gTransmitCycle time period
            if (gClock - gTransmitTimer > gTransmitCycle) {
                gTransmitTimer = gClock;
                udpServerHandleTransmitQueue();
            } // Transmit
#endif
            // Every gFlushCycle time period
            // Cyclic flush of incomplete packets from transmit queue or transmit buffer to keep tool visualizations up to date
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

#ifndef XCP_WI

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

void ApplXcpSleepNs(unsigned int ns) {
    struct timespec timeout, timerem;
    timeout.tv_sec = 0;
    timeout.tv_nsec = (long)ns;
    nanosleep(&timeout, &timerem);
}

#else

void ApplXcpClockInit(void)
{
}

// Free runing clock with 10ns tick
// 1ns with overflow every 4s is critical for CANape measurement start time offset calculation
vuint32 ApplXcpGetClock(void) {
    return gClock;
}

vuint64 ApplXcpGetClock64(void) {
    return gClock64;
}

void ApplXcpSleepNs(unsigned int ns) {
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

