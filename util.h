/* util.h */
/*
| Code released into public domain, no attribution required
*/

#ifndef __UTIL_H_ 
#define __UTIL_H_

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

extern int socketOpen(SOCKET* sp, int nonBlocking, int reuseaddr);
extern int socketBind(SOCKET sock, uint16_t port);
extern int socketJoin(SOCKET sock, uint8_t* multicastAddr);
extern int socketRecv(SOCKET sock, uint8_t* buffer, uint16_t bufferSize);
extern int socketRecvFrom(SOCKET sock, uint8_t* buffer, uint16_t bufferSize, uint8_t *addr, uint16_t *port);
extern int socketClose(SOCKET *sp);


//-------------------------------------------------------------------------------
// Keyboard

#ifdef _LINUX

#include <termios.h>

extern int _getch();
extern int _kbhit();

#endif


#endif
