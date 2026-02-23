/**********************************************************************************************************************
 *  COPYRIGHT
 *  -------------------------------------------------------------------------------------------------------------------
 *  \verbatim
 *  Copyright (c) 2026 by Vector Informatik GmbH. All rights reserved.
 *
 *                This software is copyright protected and proprietary to Vector Informatik GmbH.
 *                Vector Informatik GmbH grants to you only those rights as set out in the license conditions.
 *                All other rights remain with Vector Informatik GmbH.
 *  \endverbatim
 *  -------------------------------------------------------------------------------------------------------------------
 *  FILE DESCRIPTION
 *  -----------------------------------------------------------------------------------------------------------------*/
/*!       \file   mc_logger.h
 *        \brief  MC Logger - Simple logging functions
 *
 *********************************************************************************************************************/

#ifndef LIB_REFERENCE_INCLUDE_MC_LOGGER_H_
#define LIB_REFERENCE_INCLUDE_MC_LOGGER_H_

/**********************************************************************************************************************
 *  INCLUDES
 *********************************************************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// ============================================================================
// Log Level Enum
// ============================================================================
typedef enum {
  MC_LOG_LEVEL_DEBUG = 0,
  MC_LOG_LEVEL_INFO = 1,
  MC_LOG_LEVEL_WARNING = 2,
  MC_LOG_LEVEL_ERROR = 3,
  MC_LOG_LEVEL_CRITICAL = 4,
} McLogLevel;

// ============================================================================
// Public Logging Functions
// ============================================================================
void mc_log_debug(const char* format, ...);
void mc_log_info(const char* format, ...);
void mc_log_warn(const char* format, ...);
void mc_log_error(const char* format, ...);
void mc_log_critical(const char* format, ...);

// ============================================================================
// Utility Functions
// ============================================================================
void mc_set_log_level(McLogLevel level);
McLogLevel mc_get_log_level(void);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // LIB_REFERENCE_INCLUDE_MC_LOGGER_H_
