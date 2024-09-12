#pragma once
/* platform.h */
/*
| Code released into public domain, no attribution required
*/

#ifndef __MAIN_CFG_H__
#error "Include dependency error!"
#endif

//-------------------------------------------------------------------------------
// Keyboard

#ifdef PLATFORM_ENABLE_KEYBOARD
#ifdef _LINUX

#include <termios.h>

extern int _getch();
extern int _kbhit();

#endif
#endif // PLATFORM_ENABLE_KEYBOARD


//-------------------------------------------------------------------------------
// Safe sprintf

#if defined(_WIN) // Windows
#define SPRINTF(dest,format,...) sprintf_s((char*)dest,sizeof(dest),format,__VA_ARGS__)
#define SNPRINTF(dest,len,format,...) sprintf_s((char*)dest,len,format,__VA_ARGS__)
#elif defined(_LINUX) // Linux
#define SPRINTF(dest,format,...) snprintf((char*)dest,sizeof(dest),format,__VA_ARGS__)
#define SNPRINTF(dest,len,format,...) snprintf((char*)dest,len,format,__VA_ARGS__)
#endif


//-------------------------------------------------------------------------------
// Delay

// Delay based on clock 
extern void sleepNs(uint32_t ns);

// Delay - Less precise and less CPU load, not based on clock, time domain different
extern void sleepMs(uint32_t ms);


//-------------------------------------------------------------------------------
// Mutex


#if defined(_WIN) // Windows

#define MUTEX CRITICAL_SECTION
#define mutexLock EnterCriticalSection
#define mutexUnlock LeaveCriticalSection

#elif defined(_LINUX) // Linux

#define MUTEX pthread_mutex_t
#define MUTEX_INTIALIZER PTHREAD_MUTEX_INITIALIZER
#define mutexLock pthread_mutex_lock
#define mutexUnlock pthread_mutex_unlock

#endif

void mutexInit(MUTEX* m, BOOL recursive, uint32_t spinCount);
void mutexDestroy(MUTEX* m);


//-------------------------------------------------------------------------------
// Threads

#if defined(_WIN) // Windows

typedef HANDLE tXcpThread;
#define create_thread(h,t) *h = CreateThread(0, 0, t, NULL, 0, NULL)
#define join_thread(h) WaitForSingleObject(h, INFINITE);
#define cancel_thread(h) { TerminateThread(h,0); WaitForSingleObject(h,1000); CloseHandle(h); }

#elif defined(_LINUX) // Linux

typedef pthread_t tXcpThread;
#define create_thread(h,t) pthread_create(h, NULL, t, NULL);
#define join_thread(h) pthread_join(h,NULL);
#define cancel_thread(h) { pthread_detach(h); pthread_cancel(h); }

#endif


//-------------------------------------------------------------------------------
// Platform independant socket functions

#if defined(XCPTL_ENABLE_UDP) || defined(XCPTL_ENABLE_TCP)

#ifdef _LINUX // Linux sockets

#define SOCKET int
#define INVALID_SOCKET (-1)

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#define SOCKET_ERROR_ABORT    EBADF
#define SOCKET_ERROR_RESET    EBADF
#define SOCKET_ERROR_INTR     EBADF
#define SOCKET_ERROR_WBLOCK   EAGAIN

#undef htonll
#define htonll(val) ((((uint64_t)htonl((uint32_t)val)) << 32) + htonl((uint32_t)(val >> 32)))


#define socketGetLastError() errno

#endif

#if defined(_WIN) // Windows // Windows sockets or XL-API

#include <winsock2.h>
#include <ws2tcpip.h>

#define SOCKET_ERROR_OTHER    1
#define SOCKET_ERROR_WBLOCK   WSAEWOULDBLOCK
#define SOCKET_ERROR_ABORT    WSAECONNABORTED
#define SOCKET_ERROR_RESET    WSAECONNRESET
#define SOCKET_ERROR_INTR     WSAEINTR

#endif

// Timestamp mode
#define SOCKET_TIMESTAMP_NONE    0 // No timestamps
#define SOCKET_TIMESTAMP_HW      1 // Hardware clock
#define SOCKET_TIMESTAMP_HW_SYNT 2 // Hardware clock syntonized to PC clock
#define SOCKET_TIMESTAMP_PC      3 // PC clock

// Clock mode
#define SOCKET_TIMESTAMP_FREE_RUNNING 0 
#define SOCKET_TIMESTAMP_SOFTWARE_SYNC 1

extern BOOL socketStartup();
extern int32_t socketGetLastError();
extern void socketCleanup();
extern BOOL socketOpen(SOCKET* sp, BOOL useTCP, BOOL nonBlocking, BOOL reuseaddr, BOOL timestamps);
extern BOOL socketBind(SOCKET sock, uint8_t* addr, uint16_t port);
extern BOOL socketJoin(SOCKET sock, uint8_t* maddr);
extern BOOL socketListen(SOCKET sock);
extern SOCKET socketAccept(SOCKET sock, uint8_t addr[]);
extern int16_t socketRecv(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, BOOL waitAll);
extern int16_t socketRecvFrom(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t* srcAddr, uint16_t* srcPort, uint64_t *time);
extern int16_t socketSend(SOCKET sock, const uint8_t* buffer, uint16_t bufferSize);
extern int16_t socketSendTo(SOCKET sock, const uint8_t* buffer, uint16_t bufferSize, const uint8_t* addr, uint16_t port, uint64_t *time);
extern BOOL socketShutdown(SOCKET sock);
extern BOOL socketClose(SOCKET* sp);
#ifdef PLATFORM_ENABLE_GET_LOCAL_ADDR
extern BOOL socketGetLocalAddr(uint8_t* mac, uint8_t* addr);
#endif

#endif

//-------------------------------------------------------------------------------
// Clock

// Clock resolution and epoch
#if !defined(CLOCK_USE_UTC_TIME_NS) && !defined(CLOCK_USE_APP_TIME_US)
  // Default
  #define CLOCK_USE_UTC_TIME_NS // Use ns timestamps relative to 1.1.1970 (TAI monotonic - no backward jumps)
  //#define CLOCK_USE_APP_TIME_US // Use arbitrary us timestamps relative to application start
#endif

#ifdef CLOCK_USE_UTC_TIME_NS

#define CLOCK_TICKS_PER_M  (1000000000ULL*60)
#define CLOCK_TICKS_PER_S  1000000000
#define CLOCK_TICKS_PER_MS 1000000
#define CLOCK_TICKS_PER_US 1000
#define CLOCK_TICKS_PER_NS 1

#else

#define CLOCK_TICKS_PER_S  1000000
#define CLOCK_TICKS_PER_MS 1000
#define CLOCK_TICKS_PER_US 1

#endif

// Clock
extern BOOL clockInit();
extern uint64_t clockGet();
extern uint64_t clockGetLast();
extern char* clockGetString(char* s, uint32_t l, uint64_t c);
extern char* clockGetTimeString(char* s, uint32_t l, int64_t c);
