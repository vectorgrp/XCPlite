/*----------------------------------------------------------------------------*/
#ifndef XCPTL_CFG_H
#define XCPTL_CFG_H
/*----------------------------------------------------------------------------*/
/**
  @brief
  Transport layer version
**/
#define XCP_TRANSPORT_LAYER_VERSION 0x0104

/**
  @brief
  Use transmit queue for command responses

  @details
**/
#define XCPTL_QUEUED_CRM

/**
  @brief
  CTO size
  
  @details
  Maximum size of a XCP command
**/
#define XCPTL_MAX_CTO_SIZE 8 // must be mod 4


  #define XCPTL_SEGMENT_SIZE 8
  #define XCPTL_MAX_DTO_SIZE 8 // DTO size must be mod 4

/**
  @brief
  DAQ transmit queue size

  @details
  Transmit queue size in segments, should at least be able to hold all data produced until the next call to HandleTransmitQueue
**/
#define XCPTL_QUEUE_SIZE (32)
/**
  @brief
  Transport layer header size

  @details
  This is fixed, no other options supported
**/
#define XCPTL_TRANSPORT_LAYER_HEADER_SIZE 0
/*----------------------------------------------------------------------------*/
#endif
/*----------------------------------------------------------------------------*/
