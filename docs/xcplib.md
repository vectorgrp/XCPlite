# XCPlite Library API Documentation

This document contains the API documentation for `xcplib.h` - all functions, types, and macros that have `///` documentation comments.

Generated automatically from the source code.

## Table of Contents

- [Functions](#functions)

---

## Functions

### 1. `XcpEthServerInit`

```c
bool XcpEthServerInit(uint8_t const *address, uint16_t port, bool use_tcp, uint32_t measurement_queue_size)
```

**Description:**

Initialize the XCP on Ethernet server singleton.
- **Precondition**: User has called XcpInit.
- **address**: Address to bind to.
- **port**: Port to bind to.
- **use_tcp**: Use TCP if true, otherwise UDP.
- **measurement_queue_size**: Measurement queue size in bytes. Includes the bytes occupied by the queue header and some space needed for alignment.
- **Returns**: true on success, otherwise false.

---

### 2. `XcpEthServerShutdown`

```c
bool XcpEthServerShutdown(void)
```

**Description:**

Shutdown the XCP on Ethernet server.

---

### 3. `XcpEthServerStatus`

```c
bool XcpEthServerStatus(void)
```

**Description:**

Get the XCP on Ethernet server instance status.
- **Returns**: true if the server is running, otherwise false.

---

### 4. `XcpEthServerGetInfo`

```c
void XcpEthServerGetInfo(bool *out_is_tcp, uint8_t *out_mac, uint8_t *out_address, uint16_t *out_port)
```

**Description:**

Get information about the XCP on Ethernet server instance address.
- **Precondition**: The server instance is running.
- **out_is_tcp**: Optional out parameter to query if TCP or UDP is used.
True if TCP, otherwise UDP.
Pass NULL if not required.
- **out_mac**: Optional out parameter to query the MAC address of the interface used in the server instance.
Pass NULL if not required.
- **out_address**: Optional out parameter to query the IP address used in the server instance.
Pass NULL if not required.
- **out_port**: Optional out parameter to query the port address used in the server instance.
Pass NULL if not required.

---

### 5. `XcpCreateCalSeg`

```c
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t size)
```

**Description:**

Create a calibration segment and add it to the list of calibration segments.
With 2 pages, a default page (reference page, FLASH) and a working page (RAM).
- **name**: Name of the calibration segment.
- **default_page**: Pointer to the default page.
- **size**: Size of the calibration page in bytes.
- **Returns**: the handle or XCP_UNDEFINED_CALSEG when out of memory.

---

### 6. `*XcpGetCalSegName`

```c
const char *XcpGetCalSegName(tXcpCalSegIndex calseg)
```

**Description:**

Get the name of the calibration segment
- **Returns**: the name of the calibration segment or NULL if the index is invalid.

---

### 7. `*XcpLockCalSeg`

```c
uint8_t const *XcpLockCalSeg(tXcpCalSegIndex calseg)
```

**Description:**

Lock a calibration segment and return a pointer to the ECU page

---

### 8. `XcpUnlockCalSeg`

```c
void XcpUnlockCalSeg(tXcpCalSegIndex calseg)
```

**Description:**

Unlock a calibration segment

---

### 9. `XcpCreateEvent`

```c
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/)
```

**Description:**

Add a measurement event to the event list, return event number (0..XCP_MAX_EVENT_COUNT-1)
- **name**: Name of the event.
- **cycleTimeNs**: Cycle time in nanoseconds. 0 means sporadic event.
- **priority**: Priority of the event. 0 means normal, >=1 means realtime.
- **Returns**: The event id or XCP_UNDEFINED_EVENT_ID if out of memory.

---

### 10. `XcpCreateEventInstance`

```c
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycleTimeNs /* ns */, uint8_t priority /* 0-normal, >=1 realtime*/)
```

**Description:**

Add a measurement event to event list, return event number (0..XCP_MAX_EVENT_COUNT-1), thread safe, if name exists, an instance id is appended to the name

---

### 11. `XcpFindEvent`

```c
tXcpEventId XcpFindEvent(const char *name, uint16_t *count)
```

**Description:**

Get event id by name, returns XCP_UNDEFINED_EVENT_ID if not found
- **name**: Name of the event.
- **count**: Optional out parameter to return the number of events with the same name.
If not NULL, the count of events with the same name is returned.
If NULL, only the first event with the given name is returned.
- **Returns**: The event id or XCP_UNDEFINED_EVENT_ID if not found.
If multiple events with the same name exist, the first one is returned.

---

### 12. `XcpGetEventIndex`

```c
uint16_t XcpGetEventIndex(tXcpEventId event)
```

**Description:**

Get the event index (1..), return 0 if not found
- **event**: Event id.
- **Returns**: The event index (1..), or 0 if no indexed event instance.

---

### 13. `DaqEvent`

```c
#define DaqEvent(name)                                                                                                                                                             \ {                                                                                                                                                                              \ static THREAD_LOCAL tXcpEventId daq_event_stackframe_##name##_ = XCP_UNDEFINED_EVENT_ID                                                                                   \ if (daq_event_stackframe_##name##_ == XCP_UNDEFINED_EVENT_ID) {                                                                                                            \ daq_event_stackframe_##name##_ = XcpFindEvent(#name, NULL)                                                                                                            \ if (daq_event_stackframe_##name##_ == XCP_UNDEFINED_EVENT_ID) {                                                                                                        \ assert(false)                                                                                                                                                     \ }                                                                                                                                                                      \ } else {                                                                                                                                                                   \ XcpEventDynRelAt(daq_event_stackframe_##name##_, get_stack_frame_pointer(), get_stack_frame_pointer(), 0)                                                             \ }                                                                                                                                                                          \ }  #define DaqEvent_s(name)                                                                                                                                                           \ {                                                                                                                                                                              \ static THREAD_LOCAL tXcpEventId daq_event_stackframe__ = XCP_UNDEFINED_EVENT_ID                                                                                           \ if (daq_event_stackframe__ == XCP_UNDEFINED_EVENT_ID) {                                                                                                                    \ daq_event_stackframe__ = XcpFindEvent(name, NULL)                                                                                                                     \ if (daq_event_stackframe__ == XCP_UNDEFINED_EVENT_ID) {                                                                                                                \ assert(false)                                                                                                                                                      \ }                                                                                                                                                                      \ } else {                                                                                                                                                                   \ XcpEventDynRelAt(daq_event_stackframe__, get_stack_frame_pointer(), get_stack_frame_pointer(), 0)                                                                     \ }                                                                                                                                                                          \ }  #define DaqEvent_i(event_id) XcpEventDynRelAt(event_id, get_stack_frame_pointer(), get_stack_frame_pointer(), 0)
```

**Description:**

Trigger the XCP event 'name' for stack relative or absolute addressing
Cache the event name lookup
assert if the event does not exist

---

### 14. `XcpSetLogLevel`

```c
void XcpSetLogLevel(uint8_t level)
```

**Description:**

Set log level
- **level**: 0 = no logging, 1 = error, 2 = warning, 3 = info, 4 = debug

---

### 15. `XcpInit`

```c
void XcpInit(void)
```

**Description:**

Initialize the XCP singleton, must be called befor starting the server

---

### 16. `ApplXcpSetA2lName`

```c
#define XCP_A2L_FILENAME_MAX_LENGTH 255 // Maximum length of A2L filename with extension void ApplXcpSetA2lName(const char *name)
```

**Description:**

Set the A2L file name (for GET_ID IDT_ASAM_NAME, IDT_ASAM_NAME and for IDT_ASAM_UPLOAD)

---

### 17. `XcpSetEpk`

```c
#define XCP_EPK_MAX_LENGTH 32 // Maximum length of EPK string void XcpSetEpk(const char *epk)
```

**Description:**

Set software version identifier (EPK)

---

### 18. `XcpDisconnect`

```c
void XcpDisconnect(void)
```

**Description:**

Force Disconnect, stop DAQ, flush queue, flush pending calibrations

---

### 19. `XcpSendTerminateSessionEvent`

```c
void XcpSendTerminateSessionEvent(void)
```

**Description:**

Send terminate session event to the XCP client

---

### 20. `XcpPrint`

```c
void XcpPrint(const char *str)
```

**Description:**

Send a message to the XCP client

---

### 21. `ApplXcpGetClock64`

```c
uint64_t ApplXcpGetClock64(void)
```

**Description:**

Get the current DAQ clock value
- **Returns**: time in CLOCK_TICKS_PER_S units

---

