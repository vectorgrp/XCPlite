# XCPlite Technical Details

This document contains advanced technical information about XCPlite's implementation, addressing modes, and configuration options.

## Resource consumption

The libxcplite release build is currently about 160 KB in size, without debug prints enabled.
Memory consumption depends on the configuration and usage of the library.  
The following values are estimated for the hello_xcp example using the libxcplite default configuration:


| Resource | Consumption |
| --- | --- |
| Static memory | ~10 KB (for DAQ tables, calibration segment pages) |
| Heap memory | ~32 KB (for for transport layer queue) |
| Stack memory | ~1 KB per thread (for XCP receive and transmit threads ) |

```
sizeof(tXcpData)=9672  sizeof(tXcpLocalData)=304
XcpEthServerInit: queue_size=32768, sizeof(gXcpServer)=48
sizeof(gXcpTl)=104
```

The transmit queue is allocated with malloc/alligned_alloc. The size of the queue is a runtime parameter, which should be large enough to cover at least 10ms of expected traffic. There are no other direct heap allocations in libxcplite. The remaining state is statically allocated.



## Instrumentation Cost and Side Effects

Keeping code instrumentation side effects as small as possible was one of the major goals, but of course there are effects caused by the code instrumentation.  

### Data Acquisition Trigger and Data Transfer

The measurement data acquisition trigger and data transfer is a lock-free implementation for the producer. It may be switched to simpler a mutex based implementation, if the platform requirements can not be met. DAQ trigger also needs an external call to get the current time.

Some of the DAQ trigger macros do a lazy event lookup by name at the first time (for the convenience not to care about event handles), and cache the result in static or thread local memory.

The API functions to create events may use a mutex lock against other simultaneous event creations.

### Measurement of Function Parameters and local Variables

Measurement of function parameters and local variables has the side effect that the compiler will spill the parameters from registers to stack frame and always keeps local variables on the stack frame. This is a side effect of the in scope registration macros, so it will work even with optimization level > -O0. There is no undefined behavior caused by compiler optimizations.

Check the benchmark results in daq_test.  

### Calibration

The instrumentation to create calibration parameter segments use a mutex lock against other simultaneous segment creations.

Calibration segment access is thread safe and lock less.

Check the benchmark results in calseg_test.  



## On-target A2L File Generation

The A2L generation simply uses the file system. There is no need for memory. It opens 4 different files, which will be merged on A2L finalization.

The A2l generation macros are not thread safe and don't have an underlying once pattern. It is up to the user to take care for locking and one time execution. There are helper functions and macros to make this easy.

### One-Time Execution Patterns

The overall concept often relies on one time execution patterns. If this is not acceptable, the application has to take care for creating events and calibration segments in a controlled way or define them globally.  
The A2l address generation for measurement variables on stack needs to be done once in local scope, there is no other option yet. Also the different options to create thread local instances of measurement data.


### Option 1: Runtime Generation (Volatile)

The A2L file is always created during application runtime. The A2L may be volatile, which means it may change on each restart of the application. This happens when there are race conditions in registering segments and events. The A2L file is just downloaded again by the XCP client.  
Note: XCP used the term `upload` in the sense of upload to the client, which is a bit confusing, because the file is created on the target and then downloaded by the client.

To avoid A2L changes on each restart, the creation order of events and segments just has to be deterministic.

If the A2L file is not stable, it is up to the user to provide an EPK version string which reflects this, otherwise it could create undefined behavior.

### Option 2: Persistent Generation with Freeze Support

The A2l file is created only once during the first run of a new build of the application.

A copy of all calibration parameter segments and events definitions and of the parameter data is stored in a binary .bin file to achieve the same ordering in the next application start. BIN and A2L file get a unique name based on the software version string. The EPK software version string is used to check validity of the A2l and BIN file.

The existing A2L file is provided for download to the XCP client or may be provided to the tool by copying it.

As a side effect, calibration segment persistence (freeze command) is supported.

---

## Offline A2L Generation

The A2L file can be generated offline from the ELF file of the application with the XCPlite specific ELF/DWARF to A2L generator in the
`xcpclient` tool. The workflow, the rules for the application code, the naming of types and variables, the supported types and the
diagnostics are described in [OFFLINE_A2L.md](OFFLINE_A2L.md). The information the instrumentation macros leave in the ELF file for
this purpose is described in the next section.

## Instrumentation Markers for Offline A2L Tools

The instrumentation macros leave static data and named variables in the ELF file, from which an A2L creator or an XCP tool can build the
A2L file from the linker map and the debug information only, without on-target A2L generation. The markers make calibration segments,
events, the EPK, metadata and the scope in which an event is triggered detectable in the ELF/DWARF file. The xcpclient tool reads them
for C and C++ applications, see [OFFLINE_A2L.md](OFFLINE_A2L.md).

This section is the contract between the macros and such tools: a changed section name, marker name or descriptor layout requires a
change of the tool.

### ELF sections

| Section | Content | Emitted by |
|---|---|---|
| `xcp_evts` | one `tXcpEventDescriptor` (16 bytes: name, cycle time, priority) per event in link order, the position is the event id | `DaqCreateEvent`, `DaqCreateEventExt`, `DaqCreateAndTriggerEvent` |
| `xcp_cals` | one `tXcpCalSegDescriptor` (32 bytes: name, default page address, index variable, size, type) per calibration segment or block | `CalSegDecl`, `CalSegDeclRef`, `CalSegCreate` and the calibration block macros |
| `xcp_epk` | the EPK software version string | `XcpCreateEpk` |
| `xcp_meta` | the metadata constants `xcp_meta__<kind>__<name>` | `XCP_UNIT`, `XCP_LIMITS`, `XCP_COMMENT`, `XCP_READ_WRITE` |

On macOS the sections are named `__DATA,xcp_evts` etc. On platforms without section support (`XCP_EVENT_SECTION_ATTR` empty) the
events and segments are registered at runtime only.

### Marker variables

```c
// Addressing scheme signature (libxcplite), the value is the driver version, see Addressing Modes
const uint16_t XCPLITE__CASDD;                  // or XCPLITE__ACSDD, XCPLITE__AXSDD, XCPLITE__CXSDD

// Calibration segment descriptor and index, from CalSegDecl(name), CalSegCreate(name)
static const tXcpCalSegDescriptor calseg__name; // in section xcp_cals, calblk__name for calibration blocks
static tXcpCalSegIndex calseg_id_name;

// Event descriptor and id, from DaqCreateEvent(name), DaqCreateEventExt(name, cycle, prio), DaqCreateAndTriggerEvent(name)
static const tXcpEventDescriptor evt__name;     // in section xcp_evts
static tXcpEventId evt_id_name;
static THREAD_LOCAL tXcpEventId evt__dynname;   // DaqCreateEventInstance(name), one event instance per thread

// Event trigger anchor, from DaqTriggerEvent(name), DaqTriggerEventExt(name, base), DaqEventVar(name, ...), DaqTriggerEventCapture(name, ...), see below
static tXcpEventId trg__<modes>__name;          // in the function which triggers the event

// Metadata, from XCP_COMMENT(name, text), XCP_UNIT(name, unit), XCP_LIMITS(name, min, max), XCP_READ_WRITE(name)
static const char xcp_meta__comment__name[];    // in section xcp_meta, also xcp_meta__unit__, xcp_meta__min__, xcp_meta__max__, xcp_meta__read_write__

// Capture struct, from DaqTriggerEventCapture(event, var, ...), one member per captured variable,
// named like the variable with a trailing underscore, which an A2L tool removes again
struct { __typeof__(var) var_; ... } cap__event;
```

### `xcp_evts` section — event descriptors

Every call to `DaqCreateEvent(name)` or `DaqCreateAndTriggerEvent(name)` emits a
`tXcpEventDescriptor` constant into the `xcp_evts` section (`.rodata` on ELF targets, `__DATA,xcp_evts` on macOS):

```c
// What DaqCreateEvent(task) expands to (simplified):
static const tXcpEventDescriptor evt__task
    __attribute__((section("xcp_evts"), used)) = { "task", 0, 0 };
```

`tXcpEventDescriptor` contains the event name string, cycle time, and priority.
A tool iterates all entries in `xcp_evts` to discover **every event** defined in the
firmware, regardless of whether that code path has executed at the time of A2L generation.

### `xcp_cals` section — calibration segment descriptors

Every `CalSegDecl(name)` + `CalSegCreate(name)` pair (or `CalSegDecl(name)` at file scope) emits a `tXcpCalSegDescriptor` constant into the `xcp_cals` section:

```c
// What CalSegDecl(params) + CalSegCreate(params) expands to (simplified):
static const tXcpCalSegDescriptor calseg__params
    __attribute__((section("xcp_cals"), used)) = {
        "params",           // name
        &params,            // pointer to the default/reference page
        &calseg_id_params,  // pointer to the runtime index variable
        sizeof(params),     // size in bytes
        XCP_CALSEG_TYPE_SEGMENT
    };
```

`tXcpCalSegDescriptor` contains the segment name, the address of the default page, its size, and
the type (segment vs. block). A tool reads these to discover all calibration segments and
their exact layout in memory — without any A2L registration calls in the application code.

### Trigger point DWARF scope anchors — `trg__<modes>__<event>`

Every event trigger macro emits a **named static local variable** whose name encodes
the set of addressing modes active at that trigger point. An A2L tool reads this name from
the DWARF to know how to decode the XCP address for each measurement variable. The base address the macros pass for the stack
frame relative addressing mode is `xcp_get_frame_addr()`: the frame base the compiler uses in the DWARF locations of the local
variables (`DW_AT_frame_base`) minus `XCP_FRAME_ADDR_OFFSET`, see `inc/xcplib.h`. This is `__builtin_frame_address(0)` under clang,
which describes the local variables relative to the frame pointer register, and `__builtin_dwarf_cfa()` under GCC, which describes
them relative to the canonical frame address (CFA). The offsets from the DWARF are used without correction.

#### Naming convention

The letters between `trg__` and the trailing `__name` form a sequence where
**each letter's position equals the XCP address extension (`AddrExt`) value** it represents:

| Letter | AddrExt position | Addressing mode |
|--------|------------------|-----------------|
| `A` | 0 or 1 | **Absolute** — address offset from `ApplXcpGetBaseAddr()` |
| `C` | any | **Calibration-segment relative** — offset within a named `CalSeg` |
| `S` | 2 | **Stack frame relative** — offset from `xcp_get_frame_addr()` |
| `D` | 3+ | **Dynamic** — offset from an individually supplied base pointer; supports both synchronous and asynchronous access |
| `R` | 3 | **Capture struct relative** — offset of a member in the capture struct `cap__<event>` which the trigger passes as base pointer, a `D` slot with a known layout. The member names carry a trailing underscore |

The trailing `__name` (double underscore) identifies the event and separates it from the
mode sequence so a tool can split them unambiguously.

#### Anchor variants in the codebase

| Anchor name | Emitted by | AddrExt layout |
|-------------|-----------|----------------|
| `trg__AAS__name` | `DaqTriggerEvent`, `DaqCreateAndTriggerEvent`, `DaqEventVar` (C) | ext=0,1: Absolute — ext=2: Stack |
| `trg__AASD__name` | `DaqTriggerEventExt` | ext=0,1: Absolute — ext=2: Stack — ext=3: Dynamic base pointer |
| `trg__AASDD__name` | `DaqEventVar`, `DaqEventAtVar` (C++) | ext=0,1: Absolute — ext=2: Stack — ext=3+: Dynamic (one slot per measurement variable) |
| `trg__AASR__name` | `DaqTriggerEventCapture`, `DaqTriggerEventCaptureAt`, `DaqCreateAndTriggerEventCapture` | ext=0,1: Absolute — ext=2: Stack — ext=3: the capture struct `cap__name` |

---

**`trg__AAS__name`** — global/stack measurements only; `DaqTriggerEvent` and `DaqCreateAndTriggerEvent`:

```c
// DaqTriggerEvent(task) expands to (simplified):
static tXcpEventId trg__AAS__task = XCP_UNDEFINED_EVENT_ID;
XcpEventExt_Var(trg__AAS__task, 1 /*base count*/, xcp_get_frame_addr());
// AddrExt=0,1: global/static variables (absolute)
// AddrExt=2:   local variables on the stack at this trigger point
```

`trg__AAS__task` is a **named static local variable**. The DWARF debug info records its
address and the lexical scope it lives in — which is the same scope as the local variables
on the stack. A tool finds `trg__AAS__name` in the DWARF, walks all variables whose live
range covers that location, and creates A2L entries for them with the correct addressing mode.

`DaqCreateAndTriggerEvent(name)` does the same in one macro — it writes the
`tXcpEventDescriptor` into `xcp_evts` **and** emits the `trg__AAS__name` anchor:

```c
// DaqCreateAndTriggerEvent(foo) in function foo():
static const tXcpEventDescriptor evt__foo XCP_EVENT_SECTION_ATTR = {"foo", 0, 0};
static tXcpEventId trg__AAS__foo = XCP_UNDEFINED_EVENT_ID;
// ... trigger ...
```

---

**`trg__AASD__name`** — adds heap/instance data via an explicit base pointer;
`DaqTriggerEventExt(name, base_addr)`:

```c
// DaqTriggerEventExt(my_event, obj_ptr) expands to (simplified):
static tXcpEventId trg__AASD__my_event = XCP_UNDEFINED_EVENT_ID;
XcpEventExt_Var(trg__AASD__my_event, 2 /*base count*/, xcp_get_frame_addr(), (const uint8_t *)obj_ptr);
// AddrExt=0,1: absolute  — AddrExt=2: stack  — AddrExt=3: dynamic (obj_ptr)
```

Use this variant to measure member variables of a heap-allocated struct or class instance
alongside any stack-local and global variables of the same event.

---

**`trg__AASDD__name`** — C++ variadic `DaqEventVar` / `DaqEventAtVar`; each measurement
variable gets its own dynamic extension slot:

```cpp
// DaqEventVar(calc, A2L_MEAS_PHYS(x, ...), A2L_MEAS_PHYS(y, ...)) — simplified:
static tXcpEventId trg__AASDD__calc = XCP_UNDEFINED_EVENT_ID;
// bases = { base_addr, base_addr, frame_addr, &x, &y }
// AddrExt=0,1: absolute — AddrExt=2: stack — AddrExt=3: addr(&x) — AddrExt=4: addr(&y)
```


## Addressing Modes

XCPlite makes intensive use of relative addressing.

The addressing mode is indicated by the address extension:

```
Address extensions and addressing modes:

XCPlite absolute addressing: XCPLITE__CASDD (default)
0x00        - Calibration segment relative addressing mode (XCP_ADDR_EXT_SEG with u16 offset)
0x01        - Absolute addressing mode (XCP_ADDR_EXT_ABS)
0x02        - Stackframe relative (Event based relative addressing mode with asynchronous access)
0x03.       - Pointer relative (Event based relative addressing mode with asynchronous access)
...
0x0F
0xFD        - File upload memory space (XCP_ADDR_EXT_FILE)
0xFE        - MTA pointer address space (XCP_ADDR_EXT_PTR)
0xFF        - Undefined address extension (XCP_UNDEFINED_ADDR_EXT)

XCPlite relative addressing: XCPLITE__ACSDD (for use cases with external A2L generation)
0x00        - Absolute addressing mode (XCP_ADDR_EXT_ABS)
0x01        - Calibration segment relative addressing mode (XCP_ADDR_EXT_SEG)
... same as above

XCPlite absolute addressing without calibration segment management: XCPLITE__AXSDD (XCP_ENABLE_CALSEG_LIST not defined)
0x00        - Absolute addressing mode (XCP_ADDR_EXT_ABS)
0x01        - Memory access via application callbacks (XCP_ADDR_EXT_APP)
... same as above

XCPlite multi application absolute addressing: XCP_ADDRESS_MODE_XCPLITE__CXSDD (for SHM mode)
0x00        - Absolute addressing mode (XCP_ADDR_EXT_ABS)
0x01        - Memory access via application callbacks
... same as above
0x80 + app_id - Absolute addressing mode for application with id app_id (XCP_ADDR_EXT_ABS + app_id)

```

### CASDD vs ACSDD

Depending on `#define OPTION_CAL_SEGMENTS_ABS` in `xcplib_cfg.h`, address extension 0 is either the absolute addressing mode or the segment relative addressing mode.  
The 2 modes are named **CASDD** and **ACSDD**. The A2L variable `project_no` is used to indicate the addressing mode to A2L creators or updaters.  
This is important, because CANape does not support address extensions >0 for parameters in calibration segments.  
Parameters in calibration segments may be accessed by their segment relative address or by their absolute address, using the corresponding address extension.  
Note that this requires that the default page address given to the `XcpCreateCalSeg` function is in the 32 bit address range and has static lifetime.



### User specific addressing mode (XCP_ENABLE_APP_ADDRESSING)

The application callbacks for memory access are used, when address extensions is XCP_ADDR_EXT_ABS.  
This can be customized in xcp_cfg.h, by redefining the function like macro: 

```
#define XcpAddrIsApp(addr_ext) ((addr_ext) == XCP_ADDR_EXT_APP)
```

Asynchronous memory access is then redirected over the application callbacks:

```
void ApplXcpRegisterReadCallback(uint8_t (*cb_read)(uint32_t src, uint8_t size, uint8_t *dst));
void ApplXcpRegisterWriteCallback(uint8_t (*cb_write)(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay));
```

The delayed write parameter is set to true, when CANape is in indirect calibration mode for consistent calibration writes and the user specific commands for begin and end calibration are enabled (CC_USER_CMD 0xF1, subcmd 0x01=begin and 0x02=end atomic calibration).  

The flush operation for delayed calibration writes is redirected over the application callback:

```
void ApplXcpRegisterFlushCallback(uint8_t (*cb_flush)(void));
```


## EPK - ECU Software Version

To check compatibility of target ECU, A2L and binary parameter files, the so called EPK is used. It is a software version string specified in the A2L file MOD_PAR section, with an additional address where it is located in the ECU.  

The EPK does not have an explicit address extension, which means it defaults to 0. However the address extension 0 is defined in XCPlite (as absolute or segment relative), the EPK may be accessed by its memory address. In addition, there is a special XCP info command `GET_ID` mode=5 to obtain the EPK from the ECU.

### EPK and Binary Parameter Files

To be able to check the compatibility of binary parameter files, which store only parameter data in calibration parameter segments, an EPK memory segment is needed. This is important, because if CANape persists and caches calibration parameter segments in binary files and if the EPK is not in the address range of a memory segment, there is no way to check compatibility of the binary files.

In XCPlite, the EPK may be specified with an API function or is generated from build time and date when calibration segment persistence mode is enabled.



## Platform and Language Standard Requirements

### Language Features

- **_Generic and declspec** for A2L generation type detection
  - Requires C11 and C++17

### System Resources

**File system:** `fopen`, `fprintf`
- Used for A2L generation and optional parameter persistence to a binary file

**Heap allocations:** `aligned_alloc`, `malloc`, `free`
- Transmit queue (XcpEthServerInit parameter queue_size)
- DAQ table memory (`XcpInit`, `OPTION_DAQ_MEM_SIZE` in `xcplib_cfg.h`)
- Calibration segment page memory (`XcpCreateCalSeg`, one allocation for 4 copies (default, reference, working and RCU swap)
- Socket abstraction for XCP on Ethernet transport layer, one allocation for each socket

**Atomics:** C11 `stdatomic.h`
- Requires: `atomic_uintptr_t`, `atomic_uint_fast8_t`, `atomic_uint_fast64_t`, `exchange`, `compare_exchange`, `fetch_sub`, `fetch_add`
- Used for: lock free queue (`queue64`), lock free calibration parameter segments, DYN address mode cmd pending state, DAQ running state

**THREAD:** Linux: `pthread_create`, `pthread_join`, `pthread_cancel`
- Used for XCP transmit and receive thread

**THREAD_LOCAL:** C11:`_Thread_local`
- Used for the DaqTriggerEvent macros and A2L generation for per thread variable instances

**MUTEX:** Linux: `pthread_mutex_lock`, `pthread_mutex_unlock`
- Used for: 32 Bit Queue acquire, queue consumer incrementing the transport layer counter, thread safe creating event and calseg, thread safe lazy A2L registration

**Sleep:** Linux: `nanosleep`
- Used for receive thread polling loop

**Clock:** Linux: `clock_gettime`
- Used as DAQ timestamp clock

**Sockets:** Linux: `socket`, ...
- Used for XCP on Ethernet transport layer

## Known Issues

### CANape-Specific Issues

- **COPY_CAL_PAGE:** CANape initialize RAM is executed only on the first memory segment. **Workaround:** always copy all segments
- CANape ignores segment numbers in A2L, if segment numbering starts with 1, SET_CAL_PAGE is executed on segment 0 and 1
- **GET_ID 5 (EPK)** mode = 0x01 is ignored by CANape. **Workaround:** always provide EPK via download by 
- CANape executes GET_SEGMENT_MODE multiple times on the last memory segment before freeze request
- Address extension of memory segment is ignored by CANape. **Workaround:** using 0 for segment relative addressing
- Request for unique address extension per DAQ list is ignored by CANape (DAQ_KEY_BYTE == DAQ_EXT_DAQ). **Workaround:** Store the address extension per ODT entry
- CANape < V24 does not support shared axis in typedefs or THIS. axis references
- Transport Layer counter mutex could be avoided with alternative counter mode, which is not default in CANape
- Indicate when polling access is not possible. CANape assumes polling access is always possible
- Configuration for begin/end atomic calibration user defined XCP command is not default. Must be set once in a new CANape project to 0x01F1 and 0x02F1
- EPK segment is defined with 2 readonly pages, because of CANape irritations with mixed mode calibration segment. CANape would not care for a single page EPK segment, reads active page always from segment 0 and uses only SET_CAL_PAGE ALL mode
- CANape ignores address extension of `loop_histogram` in ccp_demo, when saving calibration values to a parameter file. `loop_histogram` is a CHARACTERISTIC array, but it is in a measurement group
