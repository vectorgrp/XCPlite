# XCPlite Shared Memory Transport Layer - Technical Details 

This document contains advanced technical information about XCPlite's shared memory transport layer implementation.  


## Overview

In addition to XCP on Ethernet, XCPlite V2.0.0 has an alternative, non standard shared memory transport layer for extremely low latency and high throughput. An XCP over SHM client can run on the local machine, attach to multiple instrumented applications and forward to a single local XCP on Ethernet server or to other logging protocols, such as CMP or DLT. It can even store data in a local file for later analysis.  


## Technical Details

The shared memory transport layer can be enabled by defining `XCP_SHM_MODE` in the configuration file `xcplib_cfg.h`.  

The shared memory has a header section were multiple applications can register. Each application gets an application id. The first application creates the shared memory segment (leader), and subsequent applications attach to it (follower). Application processes can terminate and restart to attach to their previous state, even during running measurement, which allows simple first cycle measurement data acquisition.  

The leader creates a single lock-less transport layer queue to receive measurement data and command responses,and a lock-less and wait-free calibration block named "xcp_mailbox' shared among all applications to receive XCP commands. Each application continuously polls the mailbox for new commands and processes them as if they were received via Ethernet. So both communication directions are completely free of interference.  

There are 3 new modes of operation, which can be enabled when calling `XcpInit`:

```c

#define XCP_MODE_DEACTIVATE 0    // Initialize XCP without activating the protocol layer (passive/off)
#define XCP_MODE_LOCAL 1         // Initialize and activate XCP, allocate state in a single local heap memory block
#define XCP_MODE_SHM 0xFD        // Initialize and activate XCP, allocate state in POSIX shared memory /xcpdata and /xcpqueue
#define XCP_MODE_SHM_AUTO 0xFE   // As XCP_MODE_SHM and automatically becomes the server if there is none, use XcpEthServerInit as usual
#define XCP_MODE_SHM_SERVER 0xFF // As XCP_MODE_SHM and create an XCP on Ethernet server

XcpInit("MyProject" /* Project name*/, "V1.0.1" /* EPK version string*/, true /* activate XCP */);

```

Note:  
The server is never automatically migrated to another running application. If the server dies, the next application in mode XCP_MODE_SHM_AUTO or XCP_MODE_SHM_SERVER will become the new server.  



## A2L file generation

A2L file generation works as usual with the shared memory transport layer. Each application creates an individual A2L file. All A2L files will be merged by the XCP on Ethernet server and provided for download via XCP as a single file. Application namespaces are created by prepending the applications name to each A2L object. The server can trigger the applications to freeze and finalize their A2L files.  



## Tools

There are tools included for working with or demonstrating the shared memory transport layer:

- `shmtool` - A helper tool to check and clear the state of the XCPlite shared memory and to trigger A2L file generation in the instrumented applications
- `xcpdaemon` - An XCP on Ethernet server, which can attach to multiple XCP on SHM instrumented applications. This is just another (empty) application in XCP_MODE_SHM_SERVER, assuming all user applications are in mode XCP_MODE_SHM.  
- `xcpclient` - An XCP client which supports XCP on ETH and XCP on SHM for testing, debugging, logging to local files and creating A2L files (taken from Rust xcp-lite)
- `cmpdaemon` - An SHM client which provides measurement via the ASAM CMP protocol. It has a REST API to control which applications and variables are measured and forwarded to CMP clients or simply measure everything.

See the readme files in the [tools](tools/) folder for more information about how to use these tools.


