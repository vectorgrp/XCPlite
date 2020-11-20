/*----------------------------------------------------------------------------
| File:
|   xcpAppl.cpp
|
| Description:
|   XCP protocol layer application callbacks
|   DAQ clock, all other callbacks are implemented as macros
|   Demo for XCP on Ethernet (UDP)
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/

#include "xcpAppl.h"

#define __USE_GNU
#include <link.h>

// Address information of loaded modules for XCP (application and shared libraries)
#define XCP_MAX_MODULE 1
struct
{
  const char* name;
  BYTEPTR baseAddr;
} gModuleProperties[XCP_MAX_MODULE] = {{}};

// Wall clock updated at every AppXcpTimer
volatile vuint32 gTimer = 0;


/**************************************************************************/
// ApplXcpTimer()
// ApplXcpTimerInit()
// Platform and implementation specific functions for the XCP driver
// DAQ clock
/**************************************************************************/

/* Compile with:   -lrt */

static struct timespec gts0;
static struct timespec gtr;

void ApplXcpTimerInit( void )
{    
    assert(sizeof(long long) == 8);
    clock_getres(CLOCK_REALTIME, &gtr);
    assert(gtr.tv_sec == 0);
    assert(gtr.tv_nsec == 1);
    clock_gettime(CLOCK_REALTIME, &gts0);

#if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel >= 1) {
        printf("clock resolution %lds,%ldns\n", gtr.tv_sec, gtr.tv_nsec);
        printf("clock %lds,%ldns\n", gts0.tv_sec, gts0.tv_nsec);
    }
#endif

}

// Free runing clock with 10ns tick
// 1ns with overflow every 4s is critical for CANape measurement start time offset calculation
vuint32 ApplXcpTimer(void) {

    struct timespec ts; 
    
    clock_gettime(CLOCK_REALTIME, &ts);
    gTimer = (vuint32)( ( (unsigned long long)(ts.tv_sec-gts0.tv_sec) * 1000000L ) + (ts.tv_nsec/1000) ); // us
    return gTimer;  
}

vuint8 ApplXcpGetExt(BYTEPTR addr)
{
    // Here we have the possibility to loop over the modules and find out the rigth extension
    (void) addr;
    return 0;
}

vuint32 ApplXcpGetAddr(BYTEPTR addr)
{
    vuint8 addr_ext = ApplXcpGetExt(addr);
    union {
       BYTEPTR ptr;
       vuint32 i;
    } rawAddr;
    rawAddr.ptr = (BYTEPTR) (addr - gModuleProperties[addr_ext].baseAddr);
    return rawAddr.i;
}

BYTEPTR ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr)
{
    BYTEPTR calculatedAddress = 0;
    if (addr_ext < XCP_MAX_MODULE) {
        calculatedAddress = gModuleProperties[addr_ext].baseAddr;
    }
    return calculatedAddress + addr;
}

int dump_phdr(struct dl_phdr_info* pinfo, size_t size, void* data)
{
#if defined ( XCP_ENABLE_TESTMODE_DEBUG )
  printf("0x%zx %-30.30s 0x%8.8x %d %d %d %d 0x%8.8x\n",
    pinfo->dlpi_addr, pinfo->dlpi_name, pinfo->dlpi_phdr, pinfo->dlpi_phnum,
    pinfo->dlpi_adds, pinfo->dlpi_subs, pinfo->dlpi_tls_modid,
    pinfo->dlpi_tls_data
  );
#endif

  const char prefix[] = "lib";
  const size_t prefixlen = strlen(prefix);
  if (0 < strlen(pinfo->dlpi_name))
  {
    // Here we could remember module information or something like that
  }
  else
  {
    printf("0: application: base addr: 0x%zx;\n", pinfo->dlpi_addr);
    gModuleProperties[0].baseAddr = (BYTEPTR) pinfo->dlpi_addr;
  }

  (void)size;
  (void)data;
  return 0;
}

void ApplXcpInitBaseAddressList()
{
  printf ("Address extensions for modules:\n");
  dl_iterate_phdr(dump_phdr, NULL);
}
