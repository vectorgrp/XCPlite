/*----------------------------------------------------------------------------
| File:
|   main.cpp
|   V1.0 23.9.2020
|
| Project:
|   Demo for XCP on Ethernet (UDP)
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/



extern "C" {

// XCP driver
#include "xcpLite.h"

// XCP handler
#include "xcp.h"

// UDP server
#include "udpserver.h"
#include "udpraw.h"

// ECU simulation (C demo)
#include "ecu.h"



// Parameters

// Cycles times
volatile vuint32 gTaskCycleTimerECU = 1* kApplXcpDaqTimestampTicksPerMs; // 1ms
volatile vuint32 gTaskCycleTimerECUpp = 1 * kApplXcpDaqTimestampTicksPerMs; // 1ms
volatile vuint32 gFlushCycle = 100 * kApplXcpDaqTimestampTicksPerMs; // 100ms send a DTO packet at least every 100ms

volatile unsigned short gSocketPort = 5555; // UDP port
volatile unsigned int gSocketTimeout = 0; // Receive timeout, determines the polling rate of transmit queue


}



// ECU simulation (C++ demo)
#include "ecupp.hpp"
ecu* gEcu = 0;

extern "C" {

       
    static void sleepns(unsigned int ns) {
        struct timespec timeout, timerem;
        timeout.tv_sec = 0;
        timeout.tv_nsec = ns;
        nanosleep(&timeout, &timerem);
    }
    

    // XCP command handler task
    void* xcpServer(void* par) {

        static vuint32 gFlushTimer = 0;

        printf("Start XCP server\n");
        udpServerInit(gSocketPort,gSocketTimeout);

        // Server loop
        for (;;) {

            if (udpServerHandleXCPCommands() < 0) break;  // Handle XCP commands
            
            sleepns(10000);

            if (gXcp.SessionStatus & SS_DAQ) {

                // Transmit completed UDP packets from the transmit queue
                udpServerHandleTransmitQueue();

                // Cyclic flush of incomlete packets from transmit queue to keep tool visualizations up to date
                // No priorisation of events implemented, no latency optimizations
                ApplXcpTimer();
                if (gTimer - gFlushTimer > gFlushCycle) {
                    gFlushTimer = gTimer;
                    udpServerFlushTransmitQueue();
                }
            }

        } // for (;;)

        printf("XCP server shutdown\n");
        udpServerShutdown();
        return 0;
    }



    // ECU cyclic (1ms) demo task 
    // Calls C ECU demo code
    void* ecuTask(void* par) {

        printf("Start ecuTask\n");

        for (;;) {

            sleepns(gTaskCycleTimerECU);
            
            // C demo
            ecuCyclic();
            
        }
        return 0;
    }
    
    // ECU cyclic (1ms) demo task 
    // Calls C++ ECU demo code
    void* ecuppTask(void* par) {

        printf("Start ecuppTask\n");

        for (;;) {

            sleepns(gTaskCycleTimerECUpp);
            
            // C++ demo
            gEcu->task();
            
        }
        return 0;
    }

}


extern "C" {


#if 0
    void testraw(void) {


        int n, i;
        unsigned char buffer[2000];;

        printf("testraw\n");

        struct sockaddr_in src;
        src.sin_family = AF_INET;
        src.sin_port = htons(5555);
        inet_pton(AF_INET, "172.31.31.194", &src.sin_addr);

        char tmp[32];
        inet_ntop(AF_INET, &src.sin_addr, tmp, sizeof(tmp));
        printf("src = sin_family=%u, addr=%s, port=%u\n", src.sin_family, tmp, ntohs(src.sin_port));
        
        udpRawInit(&src);

        for (;;) {
            
            if (xcp.SessionStatus & SS_CONNECTED) {

                unsigned char buf[] = { 0xFE,0x00 };
                udpRawSend(&gClientAddr,buf,2);
                
            }
            else {
                //printf("wait connect\n");
            }

            struct timespec timeout, timerem;
            timeout.tv_sec = 1;
            timeout.tv_nsec = 0;
            nanosleep(&timeout, &timerem);

        }

    }
#endif

}




// C++ main
int main(void)
{
    int a1, a2, a3;
    pthread_t t1, t2, t3;
    
    printf(
        "\nRaspberryPi XCP on UDP Demo (Lite Version) \n"
        "V1.0\n"
        "Build " __DATE__ " " __TIME__ "\n"
        );
      
    // Initialize clock for DAQ event time stamps
    ApplXcpTimerInit();

    // Initialize XCP driver
    XcpInit();
    gXcpDebugLevel = 1;

    // Initialize ECU demo (C)
    ecuInit();
    
    // Initialize ECU demo (C++)
    gEcu = new ecu();
            

    // Create the CMD and the ECU threads
    //pthread_create(&t1, NULL, xcpServer, (void*)&a1);
    //pthread_create(&t2, NULL, ecuTask, (void*)&a2); 
    //pthread_create(&t3, NULL, ecuppTask, (void*)&a3);

    xcpServer(0);

    //pthread_join(t1, NULL); // xcpServer may fail, join here 
    //pthread_cancel(t2); // and stop the other tasks
    //pthread_cancel(t3);

    return 0;
}


