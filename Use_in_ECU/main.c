/*----------------------------------------------------------------------------*/
#include <stdio.h>
#include "xcpLite.h"
/*----------------------------------------------------------------------------*/
#define CAST( am_Type, am_Value )    ( ( am_Type )( am_Value ) )
/*----------------------------------------------------------------------------*/
uint8_t const APPL_MEMORY[ 4 ] = { 0x03, 0x05, 0x00, 0x06 };
/*----------------------------------------------------------------------------*/
uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr)
{
  /* tell XCP the memory pointer to ext,addr */
  /* return NULL to tell XCP access denied */
  /* return CAST( uint8_t *, addr ) for normal situations */
  return CAST( uint8_t *, ( addr != 0x01020304 ? NULL : APPL_MEMORY ) );
}
/*----------------------------------------------------------------------------*/
uint64_t ApplXcpGetClock64()
{
  return 0;
}
/*----------------------------------------------------------------------------*/
uint8_t *ApplXcpGetBaseAddr()
{
  return NULL;
}
/*----------------------------------------------------------------------------*/
BOOL ApplXcpConnect()
{
  // tell XCP that we accept the connection
  return TRUE;
}
/*----------------------------------------------------------------------------*/
BOOL ApplXcpPrepareDaq()
{
}
/*----------------------------------------------------------------------------*/
BOOL ApplXcpStartDaq()
{
}
/*----------------------------------------------------------------------------*/
void ApplXcpStopDaq()
{
}
/*----------------------------------------------------------------------------*/
uint8_t ApplXcpGetClockState()
{
  return 0;
}
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
struct tCanMsg
{
  uint8_t  t_Dlc;
  uint8_t  t_Data[ 8 ];
};
typedef struct tCanMsg  tCanMsg;
/*----------------------------------------------------------------------------*/
/* CAN messages sent by TESTER to ECU */
tCanMsg const XCP_CAN_CONNECT =        { 2, {                   CC_CONNECT, 0, 0 ,0,  0, 0, 0, 0 } };
tCanMsg const XCP_CAN_DISCONNECT =     { 2, {                CC_DISCONNECT, 0, 0 ,0,  0, 0, 0, 0 } };

/* addr_ext (byte 3) = 0 (standard memory access), addr = 0x1234 */
tCanMsg const XCP_CAN_SHORT_UPLOAD =       { 8, {         CC_SHORT_UPLOAD, 2, 0 , 0,  4, 3, 2, 1 } };

/* daq_count = byte 2,3 = 2 */
tCanMsg const XCP_CAN_ALLOQ_DAQ =             { 4, {            CC_ALLOC_DAQ, 0, 2 , 0,  0, 0, 0, 0 } };
/* daq_list = byte 2,3 = 1,  odt_count = byte 4 = 3 */
tCanMsg const XCP_CAN_ALLOQ_ODT =             { 5, {            CC_ALLOC_ODT, 0, 1 , 0,  3, 0, 0, 0 } };
/* daq_list = byte 2,3 = 1,  odt_num = byte 4 = 2, odt_count = byte 5 = 2 */
tCanMsg const XCP_CAN_ALLOQ_ODTENTRY =        { 6, {      CC_ALLOC_ODT_ENTRY, 0, 1 , 0,  2, 2, 0, 0 } };

/* daq_list = byte 2,3 = 1,  odt_num = byte 4 = 2, odt_count = byte 5 = 3 */
tCanMsg const XCP_CAN_SET_DAQ_PTR_0 =         { 6, {          CC_SET_DAQ_PTR, 0, 1 , 0,  2, 0, 0, 0 } };
/* bit_offset = byte 1 = 0, size = byte 2 = 1, addr_ext = byte 3 = 0, addr = byte 4..7 = 0x01020304 */
tCanMsg const XCP_CAN_WRITE_DAQ_0 =           { 8, {            CC_WRITE_DAQ, 0, 1 , 0,  4, 3, 2, 1 } };
/* bit_offset = byte 1 = 0, size = byte 2 = 2, addr_ext = byte 3 = 0, addr = byte 4..7 = 0x01020304 */
tCanMsg const XCP_CAN_WRITE_DAQ_1 =           { 8, {            CC_WRITE_DAQ, 0, 2 , 0,  4, 3, 2, 1 } };
/* bit_offset = byte 1 = 0, size = byte 2 = 4, addr_ext = byte 3 = 0, addr = byte 4..7 = 0x01020304 */
tCanMsg const XCP_CAN_WRITE_DAQ_2 =           { 8, {            CC_WRITE_DAQ, 0, 4 , 0,  4, 3, 2, 1 } };
/* mode = byte 1 = 1 = start, daq_list = byte 2,3 = 0 */
tCanMsg const XCP_CAN_START_STOP_DAQ_LIST_0 = { 4, {  CC_START_STOP_DAQ_LIST, 1, 0 , 0,  0, 0, 0, 0 } };
/* mode = byte 1 = 1 = start, daq_list = byte 2,3 = 1 */
tCanMsg const XCP_CAN_START_STOP_DAQ_LIST_1 = { 4, {  CC_START_STOP_DAQ_LIST, 1, 1 , 0,  0, 0, 0, 0 } };
/* mode = byte 1, daq_list = byte 2,3, event = byte 4,5, prescale = byte 6, prio = byte 7 */
tCanMsg const
XCP_CAN_SET_DAQ_LIST_MODE_0 =
{
  8,
  {
    CC_SET_DAQ_LIST_MODE,  /* PID */
    DAQ_FLAG_TIMESTAMP,    /* mode */
    0, 0,                  /* daq_list */
    0, 0,                  /* event = l_Event10ms */
    1,                     /* prescale */
    7                      /* prio */
  }
};
tCanMsg const
XCP_CAN_SET_DAQ_LIST_MODE_1 =
{
  8,
  {
    CC_SET_DAQ_LIST_MODE,  /* PID */
    DAQ_FLAG_TIMESTAMP,    /* mode */
    1, 0,                  /* daq_list */
    0, 0,                  /* event = l_Event10ms */
    1,                     /* prescale */
    7                      /* prio */
  }
};

/*----------------------------------------------------------------------------*/
int
main( int argc, char *argv[] )
{
  uint16_t  l_Event10ms;

  printf( "/* ECU setup */\n" );
  XcpInit();
  XcpTlInit();
  XcpStart();

  printf( "/* ECU has one XCP event */\n" );
  l_Event10ms = XcpCreateEvent( "10ms", 10000000, 1, 0, 1 );

  printf( "/* ECU receives CONNECT, SHORT UPLOAD */\n" );
  XcpTlCanReceive( XCP_CAN_CONNECT.t_Data, XCP_CAN_CONNECT.t_Dlc );
  XcpTlCanReceive( XCP_CAN_SHORT_UPLOAD.t_Data, XCP_CAN_SHORT_UPLOAD.t_Dlc );

  printf( "/* ECU receives DAQ configuration */\n" );
  /*
    DAQ 0  ->  nothing
    DAQ 1  ->  ODT 0  ->  nothing
           ->  ODT 1  ->  nothing
           ->  ODT 2  ->  ODT_ENTRY 1 = siz,ext,addr = 2,0,0x01020304
                      ->  ODT_ENTRY 2 = siz,ext,addr = 4,0,0x01020304
  */
  XcpTlCanReceive( XCP_CAN_ALLOQ_DAQ.t_Data, XCP_CAN_ALLOQ_DAQ.t_Dlc );
  XcpTlCanReceive( XCP_CAN_ALLOQ_ODT.t_Data, XCP_CAN_ALLOQ_ODT.t_Dlc );
  XcpTlCanReceive( XCP_CAN_ALLOQ_ODTENTRY.t_Data, XCP_CAN_ALLOQ_ODTENTRY.t_Dlc );
  /* SET_DAQ_PTR to ODT_ENTRY 0 */
  XcpTlCanReceive( XCP_CAN_SET_DAQ_PTR_0.t_Data, XCP_CAN_SET_DAQ_PTR_0.t_Dlc );
  /* fill the pointed-to ODT_ENTRY */
  XcpTlCanReceive( XCP_CAN_WRITE_DAQ_1.t_Data, XCP_CAN_WRITE_DAQ_1.t_Dlc );
  /* SET_DAQ_PTR auto increments to next ODT_ENTRY */
  XcpTlCanReceive( XCP_CAN_WRITE_DAQ_2.t_Data, XCP_CAN_WRITE_DAQ_2.t_Dlc );
  /* tell DAQ 0 that it must be active at event 0 */
  XcpTlCanReceive( XCP_CAN_SET_DAQ_LIST_MODE_0.t_Data, XCP_CAN_SET_DAQ_LIST_MODE_0.t_Dlc );
  /* tell DAQ 1 that it must be active at event 0 */
  XcpTlCanReceive( XCP_CAN_SET_DAQ_LIST_MODE_1.t_Data, XCP_CAN_SET_DAQ_LIST_MODE_1.t_Dlc );
  printf( "/* start DAQ's, then TESTER waits until event occurs in ECU */\n" );
  XcpTlCanReceive( XCP_CAN_START_STOP_DAQ_LIST_0.t_Data, XCP_CAN_START_STOP_DAQ_LIST_0.t_Dlc );
  XcpTlCanReceive( XCP_CAN_START_STOP_DAQ_LIST_1.t_Data, XCP_CAN_START_STOP_DAQ_LIST_1.t_Dlc );

  printf( "/* ECU signals another 10ms loop passage */\n" );
  XcpEvent( l_Event10ms );
  XcpTlTransmitThreadCycle();

  printf( "/* ECU receives DISCONNECT */\n" );
  XcpTlCanReceive( XCP_CAN_DISCONNECT.t_Data, XCP_CAN_DISCONNECT.t_Dlc );

  return 0;
}/*----------------------------------------------------------------------------*/
