# XCPlite Shared Memory Transport Layer - Technical Details 

This document contains advanced technical information about XCPlite's multi application mode (SHM mode)


## Overview

When compiled in multi application mode, all application processes use a shared transmit queue, calibration RCU shared XCP state.  
Exaclty one application is the XCP server, this may be the first one running (XCP leader) or a dedicated application (XCP daemon).  
Even in the later case, the applications may start in any order.  
Requires a POSIX-compliant platform (Linux / macOS / QNX). Not supported on Windows.  


## Technical Details

This is possible, because the complete XCP state shared among threads in a single application is memory-safe and lock-less.  
The contention between threads in different application is exactly the same as the contention between threads in a single application, so no additional locking or synchronization is needed.  
The contention for read access to calibration parameters is zero, unless they would be shared between applications.  
In this simple implementation, all applications share the same transmit queue for measurement data acquisition, so the contention for write access to the transmit queue is caused by sharing a cache line for the queue head and tail offsets. This could easily changed by introducing an individial queue for every application.  

The shared memory mode is enabled by defining `XCP_SHM_MODE` in the configuration file `xcplib_cfg.h`.  

The shared memory has a header section with an application list, were multiple applications can register. Each application gets an application id. The first application creates the shared memory segment (leader), and subsequent applications attach to it (follower).  

Application processes can terminate and restart anytime and reattach to their previous state, even during running measurement, which allows simple first cycle measurement data acquisition with startup delay for XCP initialization far below a millisecond.  
  

There are new 3 new modes of operation, which can be specified when calling `XcpInit`:

```c

#define XCP_MODE_DEACTIVATE 0    // Initialize XCP without activating the protocol layer (passive/off)

// Compiled without XCP_SHM_MODE
#define XCP_MODE_LOCAL 1         // Initialize and activate XCP, all state resides in a single static memory block with a heap allocated transmit queue

// Compiled with XCP_SHM_MODE
#define XCP_MODE_SHM 0xFD        // Allocate all state and queue in POSIX shared memory (/xcpdata and /xcpqueue)
#define XCP_MODE_SHM_AUTO 0xFE   // Leader automatically becomes the XCP server
#define XCP_MODE_SHM_SERVER 0xFF // Always be the XCP server, assuming (no other application may be in auto mode)

XcpInit("MyProject" /* Project name*/, "V1.0.1" /* EPK version string*/,  XCP_MODE_SHM_AUTO /* activate XCP in multi application mode */);

```

Note:  
The server is never automatically migrated to another running application. If the server dies, the next application in mode XCP_MODE_SHM_AUTO or XCP_MODE_SHM_SERVER will become the new server and recreates the shared memory.  


## BIN persistence file usage

The persistence file manages event and calibration segment information for all applications, so they all shared the same number space for calibration segments and events. 

Using the binary persistence mode is mandatory for the shared memory mode, because the shared memory state is initialized from the persistence file by the leader.  
From BIN format V2.5, there is a new section in the persistence file for the application list. This allows to keep the application id assignment stable, regardless of startup order.  

When any application gets a new version (EPK), the BIN file is invalidated, which means the overall A2L file will be updated.  
The EPK written to the overall A2L file and represented by the XCP server, is a hash generated from all application EPKs. There is only one EPK memory segment managed by the server for all applications.  


## A2L file generation

A2L file generation works as usual. Each application creates an individual A2L file. All A2L files will be merged by the XCP server and provided for download via XCP as a single file. Application namespaces are created by prepending the applications name to each A2L object. The server can trigger the applications to freeze and finalize their A2L files.  



## Tools

There are tools included for working with or demonstrating the shared memory transport layer:

- `shmtool` - A helper tool to check and clear the state of the XCPlite shared memory and to trigger A2L file generation in the instrumented applications
- `xcpdaemon` - An XCP on Ethernet server, which can attach to multiple XCP on SHM instrumented applications. This is just another (empty) application in XCP_MODE_SHM_SERVER, assuming all user applications are in mode XCP_MODE_SHM.  
- `xcpclient` - An XCP client which supports XCP on ETH and XCP on SHM for testing, debugging, logging to local files and creating A2L files (taken from Rust xcp-lite)

See the readme files in the [tools](tools/) folder for more information about how to use these tools.


