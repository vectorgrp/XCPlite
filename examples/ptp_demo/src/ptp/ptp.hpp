#pragma once

/* ptp.hpp */

#include <stdbool.h>
#include <stdint.h>

#define OPTION_ENABLE_XCP
#undef OPTION_ENABLE_XCP // Disable XCP for initial conversion

#define PTP_MODE_OBSERVER 0x01
#define PTP_MODE_MASTER 0x02

// Forward declarations for internal types
struct tPtp;
struct tPtpC;
struct tPtpM;

class Ptp {
  public:
    // Constructor (replaces ptpInit)
    Ptp(uint8_t mode, uint8_t domain, uint8_t *uuid, uint8_t *bindAddr, char *interface, uint8_t debugLevel);

    // Destructor (replaces ptpShutdown)
    ~Ptp();

    // Public methods (replaces global functions)
    bool task();
    void reset();
    void printStatusInfo();

  private:
    // Instance state (formerly global variables)
    uint8_t debugLevel;
    uint8_t mode;
    struct tPtp *ptp;   // Main PTP communication state
    struct tPtpC *ptpC; // Observer state
    struct tPtpM *ptpM; // Master state

    // Private helper methods for observer
    void observerInit(uint8_t domain);
    void observerPrintMaster(const void *m);
    void observerPrintState();
    void observerUpdate(uint64_t t1_in, uint64_t correction, uint64_t t2_in);
    bool observerHandleFrame(void *sock, int n, void *ptp, uint8_t *addr, uint64_t timestamp);
    bool observerTask();

    // Private helper methods for master
    void masterInit(uint8_t domain, uint8_t *uuid);
    void masterPrintState();
    bool masterTask();
    bool masterHandleFrame(void *sock, int n, void *ptp, uint8_t *addr, uint64_t timestamp);

    // PTP message sending (master)
    bool ptpSendAnnounce();
    bool ptpSendSync(uint64_t *sync_txTimestamp);
    bool ptpSendSyncFollowUp(uint64_t sync_txTimestamp);
    bool ptpSendDelayResponse(void *req, uint64_t delayreg_rxTimestamp);

    // Client list management (master)
    void initClientList();
    void printClient(uint16_t i);
    uint16_t lookupClient(uint8_t *addr, uint8_t *uuid);
    uint16_t addClient(uint8_t *addr, uint8_t *uuid, uint8_t domain);

    // Helper for PTP header initialization
    void initHeader(void *h, uint8_t type, uint16_t len, uint16_t flags, uint16_t sequenceId);

    // Frame printing utility
    void printFrame(char *prefix, void *ptp, uint8_t *addr, uint64_t rx_timestamp);
};
