#pragma once

/*----------------------------------------------------------------------------
| File:
|   cmp_rest.h
|
| Description:
|   Minimal read only REST interface of the emulated Capture Module (12.3).
|
|   A Data Sink uses it to find us and, decisively for this demo, to discover that we
|   support transmission: 7.2.2 says "Support for transmission is optional in the Capture
|   Module, the data sink can use the REST API to detect if transmission is supported or
|   not". That detection is the Transmitter object of GET /asam-cmp/v1/interfaces
|   (12.3.4, Table 88) - without it a tool may never send us a Transmit Data Message and
|   XCP could never connect through the tunnel.
|
|   Implemented (all read only):
|     GET /asam-cmp/version-info        12.3.1
|     GET /asam-cmp/v1/identification   12.3.2
|     GET /asam-cmp/v1/interfaces       12.3.4  <- advertises transmission support
|     GET /asam-cmp/v1/measurement      12.3.6
|
|   Not implemented: everything that changes configuration (PUT), time synchronisation
|   (12.3.3, we are never synchronised), mDNS/DNS-SD discovery (12.2.2) and the XCP based
|   discovery of 12.1. Section 12 permits "Static configuration without Capture Module
|   Discovery", which is what this demo uses.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

// Start the REST server on its own thread. 12.3 says the interface "should run on
// standard HTTP port 80"; the demo defaults to 8080 so it needs no privileges.
// Returns false if the port cannot be bound.
bool cmpRestStart(uint16_t port);

// Stop the REST server and join its thread. Safe to call if it was never started.
void cmpRestStop(void);
