#pragma once
#define __MAIN_CFG_H__


/* main.h */
/*
| Code released into public domain, no attribution required
*/

// Windows or Linux ?
#if defined(_WIN32) || defined(_WIN64)
  #define _WIN
  #if defined(_WIN32) && defined(_WIN64)
    #undef _WIN32
  #endif
  #if defined(_LINUX) || defined(_LINUX64)|| defined(_LINUX32)
    #error
  #endif
#else
  #define _LINUX
  #if defined (_ix64_) || defined (__x86_64__) || defined (__aarch64__)
    #define _LINUX64
  #else
    #define _LINUX32
  #endif
  #if defined(_WIN) || defined(_WIN64)|| defined(_WIN32)
    #error
  #endif
#endif


#ifdef _WIN
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#else
#define _DEFAULT_SOURCE
#endif


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <assert.h>

#ifndef _WIN // Linux

#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#else // Windows

#include <windows.h>
#include <time.h>
#include <conio.h>

#endif


#ifdef __cplusplus
#include <typeinfo>
#include <thread>
#include <string>
#include <vector>
#endif

#define BOOL uint8_t
#define FALSE 0
#define TRUE 1

#include "main_cfg.h"

