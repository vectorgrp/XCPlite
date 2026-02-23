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
/*!       \file   mc_logger.c
 *        \brief  MC Logger Implementation
 *
 *********************************************************************************************************************/

/**********************************************************************************************************************
 *  INCLUDES
 *********************************************************************************************************************/
#include "mc_logger.h"
#include <stdarg.h>
#include <stdio.h>

// Global log level - default to INFO
static McLogLevel g_mc_log_level = MC_LOG_LEVEL_INFO;

// ============================================================================
// Internal Logging Function
// ============================================================================
static void mc_log_impl(McLogLevel level, const char* format, va_list args) {
  // Filter by log level
  if (level < g_mc_log_level) {
    return;
  }

  // Choose output stream
  FILE* output_stream = (level >= MC_LOG_LEVEL_ERROR) ? stderr : stdout;

  // Print log level prefix based on level
  switch (level) {
    case MC_LOG_LEVEL_DEBUG:
      fprintf(output_stream, "[DEBUG] ");
      break;
    case MC_LOG_LEVEL_INFO:
      fprintf(output_stream, "[INFO] ");
      break;
    case MC_LOG_LEVEL_WARNING:
      fprintf(output_stream, "[WARNING] ");
      break;
    case MC_LOG_LEVEL_ERROR:
      fprintf(output_stream, "[ERROR] ");
      break;
    case MC_LOG_LEVEL_CRITICAL:
      fprintf(output_stream, "[CRITICAL] ");
      break;
    default:
      fprintf(output_stream, "[UNKNOWN] ");
      break;
  }

  // Print the actual message
  vfprintf(output_stream, format, args);

  fflush(output_stream);
}

// ============================================================================
// Public Logging Functions
// ============================================================================
void mc_log_debug(const char* format, ...) {
  va_list args;
  va_start(args, format);
  mc_log_impl(MC_LOG_LEVEL_DEBUG, format, args);
  va_end(args);
}

void mc_log_info(const char* format, ...) {
  va_list args;
  va_start(args, format);
  mc_log_impl(MC_LOG_LEVEL_INFO, format, args);
  va_end(args);
}

void mc_log_warn(const char* format, ...) {
  va_list args;
  va_start(args, format);
  mc_log_impl(MC_LOG_LEVEL_WARNING, format, args);
  va_end(args);
}

void mc_log_error(const char* format, ...) {
  va_list args;
  va_start(args, format);
  mc_log_impl(MC_LOG_LEVEL_ERROR, format, args);
  va_end(args);
}

void mc_log_critical(const char* format, ...) {
  va_list args;
  va_start(args, format);
  mc_log_impl(MC_LOG_LEVEL_CRITICAL, format, args);
  va_end(args);
}

// ============================================================================
// Utility Functions
// ============================================================================
void mc_set_log_level(McLogLevel level) { g_mc_log_level = level; }

McLogLevel mc_get_log_level(void) { return g_mc_log_level; }
