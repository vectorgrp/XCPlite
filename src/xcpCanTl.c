/*----------------------------------------------------------------------------
| File:
|   xcpCanTl.c
|
| Description:
|   XCP on CAN transport layer
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "options.h"
#include "util.h"
#include "xl_can.h"
#include "xcpLite.h" 

#if OPTION_ENABLE_CAN_TRANSPORT

 typedef struct {
  uint8_t commited;                // commited = TRUE
  uint8_t len;                     // CAN message length
  uint8_t msg[XCPTL_MAX_DTO_SIZE]; // CAN message payload
} tXcpMsgBuffer;


static struct {

  BOOL useCANFD;
  uint32_t CroId;
  uint32_t DtoId;
  uint32_t BitRate;

  int32_t lastError;
      
  // Transmit message queue
  tXcpMsgBuffer queue[XCPTL_QUEUE_SIZE];
  uint16_t queue_rp; // rp = read index
  uint16_t queue_len; // rp+len = write index (the next free entry), len=0 ist empty, len=XCPTL_QUEUE_SIZE is full

#if defined(_WIN) // Windows
  HANDLE queue_event;
#endif

  MUTEX Mutex_Queue;

} gXcpTl;


//------------------------------------------------------------------------------

// Transmit a CAN messafeUDP datagramm or TCP segment (contains multiple XCP DTO messages or a single CRM message (len+ctr+packet+fill))
// Must be thread safe, because it is called from CMD and from DAQ thread
// Returns -1 on would block, 1 if ok, 0 on error
static BOOL sendMessage(const uint8_t *data, uint8_t len) {

    //printf("sendMessage %u\n", len);

    if (!canTransmit(len, gXcpTl.DtoId, data)) {
        XCP_DBG_PRINTF_ERROR("ERROR: transmit failed!\n");
        gXcpTl.lastError = XCPTL_ERROR_SEND_FAILED;
        return FALSE; // Error
    }
    
    return TRUE; // Ok
}


//------------------------------------------------------------------------------
// XCP packet queue (DTO buffers)

// Notify transmit queue handler thread
// Not thread save!
static BOOL notifyTransmitQueueHandler() {

    // Windows only, Linux version uses polling
#if defined(_WIN) // Windows
    if (gXcpTl.queue_len > 0) {
      SetEvent(gXcpTl.queue_event);
      return TRUE;
    }
#endif
    return FALSE;
}


// Allocate a new message buffer
// Not thread save!
static  tXcpMsgBuffer* getMsgBuffer(uint16_t len) {

    tXcpMsgBuffer* b;

    assert(len > 0 && len <= XCPTL_MAX_DTO_SIZE);

    /* Check if there is space in the queue */
    if (gXcpTl.queue_len >= XCPTL_QUEUE_SIZE) {        
        return NULL; /* Queue overflow */
    }
    else {
        unsigned int i = gXcpTl.queue_rp + gXcpTl.queue_len;
        if (i >= XCPTL_QUEUE_SIZE) i -= XCPTL_QUEUE_SIZE;
        b = &gXcpTl.queue[i];       
        b->commited = FALSE;
        b->len = (uint8_t)len;
        gXcpTl.queue_len++;
        return b;
    }

}

// Clear and init transmit queue
static void XcpTlInitTransmitQueue() {

    mutexLock(&gXcpTl.Mutex_Queue);
    gXcpTl.queue_rp = 0;
    gXcpTl.queue_len = 0;
    mutexUnlock(&gXcpTl.Mutex_Queue);
}

// Transmit all completed and fully commited UDP frames
// Returns number of bytes sent or -1 on error
int32_t XcpTlHandleTransmitQueue( void ) {

    tXcpMsgBuffer* b = NULL;
    int32_t n = 0;

    //printf("XcpTlHandleTransmitQueue\n");

    for (;;) {

      // Check
      mutexLock(&gXcpTl.Mutex_Queue);
      if (gXcpTl.queue_len >= 1) {
        b = &gXcpTl.queue[gXcpTl.queue_rp];
        if (!b->commited) b = NULL; // return when reaching a not commited buffer 
      }
      else {
        b = NULL;
      }
      mutexUnlock(&gXcpTl.Mutex_Queue);
      if (b == NULL) return n; // Ok, queue is empty or first entry is not commited yet

      // Send this frame
      if (!sendMessage(&b->msg[0], b->len)) return -1;  // error
      n += b->len;

      // Free this buffer when succesfully sent
      mutexLock(&gXcpTl.Mutex_Queue);
      if (++gXcpTl.queue_rp >= XCPTL_QUEUE_SIZE) gXcpTl.queue_rp = 0;
      gXcpTl.queue_len--;
      mutexUnlock(&gXcpTl.Mutex_Queue);

    } // for (ever)
}


// Reserve space for a XCP packet in a transmit segment buffer and return a pointer to packet data and a handle for the segment buffer for commit reference
// Flush the transmit segment buffer, if no space left
uint8_t *XcpTlGetTransmitBuffer(void **handlep, uint16_t packet_size) {

    tXcpMsgBuffer* p;

    mutexLock(&gXcpTl.Mutex_Queue);
    p = getMsgBuffer(packet_size);
    mutexUnlock(&gXcpTl.Mutex_Queue);
    *handlep = (void*)p;
    if (p == NULL) return NULL; // Overflow
    return &p->msg[0]; // return pointer to XCP message DTO data
}

void XcpTlCommitTransmitBuffer(void *handle, BOOL flush) {

    (void)flush;

    if (handle != NULL) {
        tXcpMsgBuffer* p = (tXcpMsgBuffer*)handle;
        p->commited = TRUE;
        notifyTransmitQueueHandler();
    }
}

// Flush the current transmit buffer, used on high prio event data
void XcpTlFlushTransmitBuffer() {

}

// Wait until transmit queue is empty, used when measurement is stopped  
void XcpTlWaitForTransmitQueueEmpty() {

    uint16_t timeout = 0;
    do {
        sleepMs(20);
        timeout++;
    } while (gXcpTl.queue_len > 1 || timeout>=50); // Wait max 1s until the transmit queue is empty
}


//------------------------------------------------------------------------------

// Transmit XCP response or event packet
// No error handling in protocol layer
// If transmission fails, tool times out, retries or take appropriate action
// Note: CANape cancels measurement, when answer to GET_DAQ_CLOCK times out
void XcpTlSendCrm(const uint8_t* packet, uint16_t packet_size) {

    void* handle = NULL;
    uint8_t* p;
    int r = 0;

    //printf("XcpTlSendCrm %u\n", packet_size);

    // Queue the response packet
    if ((p = XcpTlGetTransmitBuffer(&handle, packet_size)) != NULL) {
        memcpy(p, packet, packet_size);
        XcpTlCommitTransmitBuffer(handle, TRUE /* flush */);
    }
    else { // Buffer overflow
      printf("ERROR: Buffer overlow!\n"); // Should never happen
      assert(0);
    }

}



// Handle incoming XCP commands
// Blocking with timeout_ms [0..XCPTL_TIMEOUT_INFINITE]
// Returns FALSE on error
BOOL XcpTlHandleCommands(uint32_t timeout_ms) {
  
    uint8_t msg[XCPTL_MAX_CTO_SIZE];
    uint8_t len = XCPTL_MAX_CTO_SIZE;
    uint32_t id;

    int res = canReceive(&len, &id, msg, timeout_ms);
    if (res < 0) return FALSE;
    if (res>0) {
        if (id != gXcpTl.CroId) {
        printf("WARNING: message id %x ignored!\n", id);
        return TRUE; // Ignore other IDs than CRO_ID
      }
      if (len == 0 || len > XCPTL_MAX_CTO_SIZE) {
        printf("WARNING: message %x with len %u ignored!\n",id,len);
        return TRUE; // Ignore illegal length messages
      }
      XcpCommand((const uint32_t*)&msg[0], len); // Handle command

    }
    return TRUE;
}



//-------------------------------------------------------------------------------------------------------


BOOL XcpCanTlInit(BOOL useCANFD, uint32_t croId, uint32_t dtoId, uint32_t bitRate) {

    XCP_DBG_PRINTF1("\nInit XCP on %s transport layer, croId=%u, dtoId=%u\n", useCANFD?"CANFD":"CAN",croId, dtoId);
    XCP_DBG_PRINTF1("  QUEUE_SIZE=%u, %uKiB memory used\n", XCPTL_QUEUE_SIZE, (unsigned int)sizeof(gXcpTl) / 1024);

    gXcpTl.useCANFD = useCANFD;
    gXcpTl.CroId = croId;
    gXcpTl.DtoId = dtoId;
   
    // Initialize XL-API
    if (!canInit(useCANFD, bitRate,croId)) {
      printf("ERROR: canInit failed!\n");
      return FALSE;
    }

    mutexInit(&gXcpTl.Mutex_Queue, 0, 1000);
    XcpTlInitTransmitQueue();
#if defined(_WIN) // Windows
    gXcpTl.queue_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(gXcpTl.queue_event!=NULL); 
#endif

    return TRUE;
}


void XcpTlShutdown() {

  // Shutdown the CAN driver
  canShutdown();

  mutexDestroy(&gXcpTl.Mutex_Queue);
  CloseHandle(gXcpTl.queue_event);
}


//-------------------------------------------------------------------------------------------------------


// Wait for outgoing data or timeout after timeout_us
// Return FALSE in case of timeout
BOOL XcpTlWaitForTransmitData(uint32_t timeout_ms) {

  assert(timeout_ms >= 1);

#if defined(_WIN) // Windows 
    // Use event triggered for Windows
    if (WAIT_OBJECT_0 == WaitForSingleObject(gXcpTl.queue_event, timeout_ms)) {
      ResetEvent(gXcpTl.queue_event);
      return TRUE;
    }
    return FALSE;
#elif defined(_LINUX) // Linux
    // Use polling for Linux
    #define XCPTL_QUEUE_TRANSMIT_POLLING_TIME_MS 1
    uint32_t t = 0;
    while (gXcpTl.queue_len <= 1) {
        sleepMs(XCPTL_QUEUE_TRANSMIT_POLLING_TIME_MS); 
        t = t + XCPTL_QUEUE_TRANSMIT_POLLING_TIME_MS;
        if (t >= timeout_ms) return FALSE;
      }
      return TRUE;
#endif
}


//-------------------------------------------------------------------------------------------------------

int32_t XcpTlGetLastError() {
    return gXcpTl.lastError;
}


#endif // #if OPTION_ENABLE_CAN_TRANSPORT
