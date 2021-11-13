/* platform.h */
/*
| Code released into public domain, no attribution required
*/

#ifndef __PLATFORM_H_ 
#define __PLATFORM_H_


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
#if defined (_ix64_) || defined (__x86_64__)
#define _LINUX64
#else
#define _LINUX32
#endif
#define _LINUX
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
#include <math.h>

#ifdef _WIN
#define M_PI 3.14159265358979323846
#endif
#define M_2PI (M_PI*2)

#include <assert.h>

#ifndef _WIN // Linux

#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h> 

#include <ifaddrs.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MAX_PATH 256

#else // Windows

#include <windows.h>
#include <time.h>
#include <conio.h>
#ifdef __cplusplus
#include <thread>
#endif

// Need to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>

#endif


#ifdef __cplusplus
extern "C" {
#endif

    //-------------------------------------------------------------------------------
    // Mutex


#ifdef _LINUX

#define MUTEX pthread_mutex_t
#define MUTEX_INTIALIZER PTHREAD_MUTEX_INITIALIZER
#define mutexLock pthread_mutex_lock
#define mutexUnlock pthread_mutex_unlock

#else 

#define MUTEX CRITICAL_SECTION
#define MUTEX_INTIALIZER 0
#define mutexLock EnterCriticalSection
#define mutexUnlock LeaveCriticalSection

#endif

    void mutexInit(MUTEX* m, int recursive, uint32_t spinCount);
    void mutexDestroy(MUTEX* m);


    //-------------------------------------------------------------------------------
    // Threads

#ifdef _WIN
    typedef HANDLE tXcpThread;
#define create_thread(h,t) *h = CreateThread(0, 0, t, NULL, 0, NULL)
#define cancel_thread(h) { TerminateThread(h,0); WaitForSingleObject(h,1000); CloseHandle(h); }
#else
    typedef pthread_t tXcpThread;
#define create_thread(h,t) pthread_create(h, NULL, t, NULL);
#define cancel_thread(h) pthread_cancel(h);
#endif


    //-------------------------------------------------------------------------------
    // Sockets

#ifdef _LINUX // Linux sockets

#define SOCKET int
#define INVALID_SOCKET (-1)

#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR struct sockaddr

#define socketGetLastError() errno
#define SOCKET_ERROR_CLOSED   EBADF
#define SOCKET_ERROR_WBLOCK   EAGAIN

#define htonll(val) ((((uint64_t)htonl((uint32_t)val)) << 32) + htonl((uint32_t)(val >> 32)))

#endif 

#ifdef _WIN // Windows sockets or XL-API

#define socketGetLastError() (WSAGetLastError())
#define SOCKET_ERROR_CLOSED   10004 // Wsaabtr
#define SOCKET_ERROR_WBLOCK   WSAEWOULDBLOCK

#endif

    extern int socketStartup();
    extern void socketShutdown();

    extern int socketOpen(SOCKET* sp, int nonBlocking, int reuseaddr);
    extern int socketBind(SOCKET sock, uint8_t* addr, uint16_t port);
    extern int socketJoin(SOCKET sock, uint8_t* multicastAddr);
    extern int16_t socketRecv(SOCKET sock, uint8_t* buffer, uint16_t bufferSize);
    extern int16_t socketRecvFrom(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t* addr, uint16_t* port);
    extern int socketSendTo(SOCKET sock, const uint8_t* buffer, uint16_t bufferSize, const uint8_t* addr, uint16_t port);
    extern int socketClose(SOCKET* sp);
    extern int socketGetLocalAddr(uint8_t* mac, uint8_t* addr);


    //-------------------------------------------------------------------------------
    // Keyboard

#ifdef _LINUX

#include <termios.h>

    extern int _getch();
    extern int _kbhit();

#endif


#ifdef __cplusplus
}
#endif

#endif // __PLATFORM_H_
