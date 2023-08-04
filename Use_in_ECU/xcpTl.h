/*----------------------------------------------------------------------------*/
#ifndef XCP_TL_H
#define XCP_TL_H
/*----------------------------------------------------------------------------*/
void XcpTlInit( void );
void XcpTlCanReceive( uint8_t const a_Received[], uint8_t a_RcvdLen );

extern uint8_t* XcpTlGetTransmitBuffer(void** handlep, uint16_t size); /**< Get a buffer for a message with size **/
extern void XcpTlCommitTransmitBuffer(void* handle); /**< Commit a buffer from XcpTlGetTransmitBuffer **/
extern void XcpTlFlushTransmitBuffer(); /**< Finalize the current transmit packet **/
extern void XcpTlSendCrm(const uint8_t* packet, uint16_t packet_size); /**< Send or queue (depending on XCPTL_QUEUED_CRM) a command response **/
extern void XcpTlWaitForTransmitQueue(); /**< Wait (sleep) until transmit queue is ready for immediate response **/

void XcpTlTransmitThreadCycle( void );
/*----------------------------------------------------------------------------*/
#endif
/*----------------------------------------------------------------------------*/
