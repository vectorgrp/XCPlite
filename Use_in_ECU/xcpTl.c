/*----------------------------------------------------------------------------*/
#include "main.h"
#include "xcpTl.h"
#include "xcptl_cfg.h"
#include "xcpLite.h"
/*----------------------------------------------------------------------------*/
#define CAST( am_Type, am_Value )    ( ( am_Type )( am_Value ) )
/*----------------------------------------------------------------------------*/
#if ((XCPTL_MAX_CTO_SIZE&0x03) != 0)
#error "XCPTL_MAX_CTO_SIZE should be aligned to 4!"
#endif
#if ((XCPTL_MAX_DTO_SIZE&0x03) != 0)
#error "XCPTL_MAX_DTO_SIZE should be aligned to 4!"
#endif
/*----------------------------------------------------------------------------*/
/**
  @details
  XCP on Ethernet Message (Frame) (see XCP_Book_V1.5_EN, page 57)

  @details
  XCP on Ethernet Message (Frame)
  - XCP Header
    - LEN == dlc
    - CTR == ctr
  - XCP Packet
    - packet[1]
  - XCP Tail
    - packet before being copyed into this struct is extended with filler bytes. These filler bytes make the XCP Tail
**/
typedef struct {
    uint8_t packet[1];  /**< message data **/
} tXcpMessage;

typedef struct {
    uint8_t packet[XCPTL_MAX_CTO_SIZE];
} tXcpCtoMessage;

/**
  @brief
  Transmit Segment

  @details
  Transport Layer:
  - segment = message 1 + message 2 ... + message n = concatenated transport layer messages
  - message = len + ctr + (protocol layer packet) + fill (XCP_Book_V1.5_EN's XCP on Ethernet Message (Frame))
**/
typedef struct {
    uint16_t uncommited;              /**< Number of uncommited messages in this segment **/
    uint16_t size;                    /**< Number of overall bytes in this segment **/
    uint8_t msg[XCPTL_SEGMENT_SIZE];  /**< Segment/MTU - concatenated transport layer messages **/
} tXcpMessageBuffer;

/**
  @brief
  Transport Layer instance

  @details
**/
struct tXcpTlInstance {
  tXcpMessageBuffer queue[XCPTL_QUEUE_SIZE]; /**< Transmit Segment Queue **/
  uint32_t queue_rp;                         /**< Transmit Segment Queue's read index (next to read) **/
  uint32_t queue_len;                        /**< Transmit Segment Queue's amount sendable/committable \n rp+len = write index (the next free entry), len=0 ist empty, len=XCPTL_QUEUE_SIZE is full **/
  tXcpMessageBuffer* msg_ptr;                /**< current incomplete or not fully commited segment **/
  uint16_t ctr;                              /**< next DAQ DTO data transmit message packet counter **/
};

/**
  @brief
  Transport Layer instance

  @details
**/
static struct tXcpTlInstance gXcpTl;
/*----------------------------------------------------------------------------*/
#define CAN_RCVD    TRUE
#define CAN_SENT    FALSE
void
DisplayCanTraffic( BOOL a_IsRcvd, uint8_t const a_Msg[], uint8_t a_Dlc )
{
  printf(
    "CAN %s ECU: len = %d, data = %2x, %2x, %2x, %2x,   %2x, %2x, %2x, %2x\n",
    ( a_IsRcvd ? "->" : "<-" ),
    a_Dlc,
    a_Msg[ 0 ], a_Msg[ 1 ], a_Msg[ 2 ], a_Msg[ 3 ],
    a_Msg[ 4 ], a_Msg[ 5 ], a_Msg[ 6 ], a_Msg[ 7 ] );
}
/*----------------------------------------------------------------------------*/
/**
  @brief
  Transmit a UDP datagramm or TCP segment (contains multiple XCP DTO messages or a single CRM message (len+ctr+packet+fill))

  @details
  Must be thread safe, because it is called from CMD and from DAQ thread

  @return
  Returns -1 on would block, 1 if ok, 0 on error
**/
static int sendDatagram(const uint8_t *data, uint16_t size ) {
  DisplayCanTraffic( CAN_SENT, data, size );

  return 1;
}
/*----------------------------------------------------------------------------*/
void XcpTlInit( void )
{
  gXcpTl.queue_rp = 0;
  gXcpTl.queue_len = 0;
  gXcpTl.msg_ptr = NULL;
  gXcpTl.ctr = 0;
}
/*----------------------------------------------------------------------------*/
/**
  @brief
  Allocate a fresh segment in the queue

  @details
  Not thread save!
**/
static void getSegmentBuffer() {

    tXcpMessageBuffer* b;

    /* Check if there is space in the queue */
    if (gXcpTl.queue_len >= XCPTL_QUEUE_SIZE) {
        /* Queue overflow */
        gXcpTl.msg_ptr = NULL;
    }
    else {
        unsigned int i = gXcpTl.queue_rp + gXcpTl.queue_len;
        if (i >= XCPTL_QUEUE_SIZE)
        {
          i -= XCPTL_QUEUE_SIZE;
        }
        b = &gXcpTl.queue[i];
        b->size = 0;
        b->uncommited = 0;
        gXcpTl.msg_ptr = b;
        gXcpTl.queue_len++;
    }
}
/*----------------------------------------------------------------------------*/
/**
  @brief
  Reserve space for a XCP packet in a transmit buffer and return a pointer to packet data and a handle for the segment buffer for commit reference

  @details
  - reserve space for alignment (XCP Message's XCP Tail, see XCP_Book_V1.5_EN, page 57)
  - if Packet does not fit anymore in current segment (msg_ptr->size + msg_size), get a fresh segment
  - treat the segment as a @ref tXcpMessage
  - update the ctr,dlc with the Packet's size
**/
uint8_t* XcpTlGetTransmitBuffer(void** handlep, uint16_t packet_size)
{
    tXcpMessage* p;
    uint16_t msg_size;

 #if XCPTL_PACKET_ALIGNMENT==2
    packet_size = (uint16_t)((packet_size + 1) & 0xFFFE); // Add fill
#endif
#if XCPTL_PACKET_ALIGNMENT==4
    packet_size = (uint16_t)((packet_size + 3) & 0xFFFC); // Add fill
#endif
    msg_size = (uint16_t)(packet_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);

    // Get another message buffer from queue, when active buffer ist full
    // if ( ( gXcpTl.msg_ptr == NULL ) || ( (uint16_t)(gXcpTl.msg_ptr->size + msg_size) > XCPTL_SEGMENT_SIZE ) ) {
    {
        if( msg_size > XCPTL_SEGMENT_SIZE ) {
            gXcpTl.msg_ptr = NULL;
        }
        else {
            getSegmentBuffer();
        }
    }

    if (gXcpTl.msg_ptr != NULL) {

        p = (tXcpMessage*)&gXcpTl.msg_ptr->msg[gXcpTl.msg_ptr->size];
        gXcpTl.msg_ptr->size = (uint16_t)(gXcpTl.msg_ptr->size + msg_size);
        *((tXcpMessageBuffer**)handlep) = gXcpTl.msg_ptr;
        gXcpTl.msg_ptr->uncommited++;

    }
    else {
        p = NULL; // Overflow
    }

    if (p == NULL) return NULL; // Overflow
    return &p->packet[0]; // return pointer to XCP message DTO data
}
/*----------------------------------------------------------------------------*/
/**
  @brief
  Protocol indicates that it copied the XCP Packet in the XCP Message (Frame)

  @details
  Mark the XCP Message as filled (committed)
**/
void XcpTlCommitTransmitBuffer(void* handle)
{
    tXcpMessageBuffer* p = (tXcpMessageBuffer*)handle;
    if (handle != NULL) {
        p->uncommited--;
    }
}
/*----------------------------------------------------------------------------*/
/**
  @brief
  Mark current segment as filled and allocate a fresh segment
**/
void XcpTlFlushTransmitBuffer()
{
    if (gXcpTl.msg_ptr != NULL && gXcpTl.msg_ptr->size > 0) {
        getSegmentBuffer();
    }
}
/*----------------------------------------------------------------------------*/
void XcpTlSendCrm(const uint8_t* packet, uint16_t packet_size)
{
#ifdef XCPTL_QUEUED_CRM

    void* handle = NULL;
    uint8_t* p;
    int r = 0;

    // If transmit queue is empty, save the space and transmit instantly
    if (gXcpTl.queue_len < 1 && ( (gXcpTl.msg_ptr == NULL) || (gXcpTl.msg_ptr->size == 0))) {
        // Send the response
        // Build XCP CTO message (ctr+dlc+packet)
        uint16_t msg_size;
        msg_size = packet_size;
#if (XCPTL_PACKET_ALIGNMENT==2)
        msg_size = (uint16_t)((msg_size + 1) & 0xFFFE); // Add fill
#endif
#if (XCPTL_PACKET_ALIGNMENT==4)
        msg_size = (uint16_t)((msg_size + 3) & 0xFFFC); // Add fill
#endif
        msg_size = (uint16_t)(msg_size + XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
        r = sendDatagram(packet, msg_size);
    }
    if (r == 1) return; // ok

    // Queue the response packet
    if ((p = XcpTlGetTransmitBuffer(&handle, packet_size)) != NULL) {
        memcpy(p, packet, packet_size);
        XcpTlCommitTransmitBuffer(handle);
        XcpTlFlushTransmitBuffer();
    }
    else { // Buffer overflow
        // @@@@ Todo
    }

#else
  sendDatagram( data, n );
#endif
}
/*----------------------------------------------------------------------------*/
void XcpTlWaitForTransmitQueue()
{
    XcpTlFlushTransmitBuffer();
#ifndef XCPTL_QUEUED_CRM
    do {
        sleepMs(2);
    } while (gXcpTl.queue_len > 1) ;
#endif
}
/*----------------------------------------------------------------------------*/
void
XcpTlCanReceive( uint8_t const a_Received[], uint8_t a_RcvdLen )
{
  DisplayCanTraffic( CAN_RCVD, a_Received, a_RcvdLen );
  XcpCommand( CAST( uint32_t const *, a_Received ), a_RcvdLen );
}
/*----------------------------------------------------------------------------*/
/**
  @brief
  Transmit all completed and fully commited UDP frames

  @details
  see @ref PAG_TransportLayer

  @return
  Returns 1 ok, 0 error
**/
static int XcpTlHandleTransmitQueue( void ) {
  tXcpMessageBuffer* b;

  for (;;) {
    // Check
    if (gXcpTl.queue_len >= 1) {
      b = &gXcpTl.queue[gXcpTl.queue_rp];
      if (b->uncommited > 0) b = NULL;
    }
    else {
      b = NULL;
    }
    if (b == NULL) break;
    if (b->size == 0)
    {
        if (++gXcpTl.queue_rp >= XCPTL_QUEUE_SIZE) gXcpTl.queue_rp = 0;
        gXcpTl.queue_len--;
        continue; // This should not happen @@@@
    }

    // Send this frame
    int r = sendDatagram(&b->msg[0], b->size);
    if (r == (-1)) return 1; // Ok, would block (-1)
    if (r == 0) return 0; // Nok, error (0)

    // Free this buffer when succesfully sent
    if (++gXcpTl.queue_rp >= XCPTL_QUEUE_SIZE) gXcpTl.queue_rp = 0;
    gXcpTl.queue_len--;
  } // for (;;)

  return 1; // Ok, queue empty now
}
/*----------------------------------------------------------------------------*/
void XcpTlTransmitThreadCycle( void )
{
  if (gXcpTl.queue_len >= 1)
  {
    XcpTlHandleTransmitQueue();
  }
}
/*----------------------------------------------------------------------------*/
