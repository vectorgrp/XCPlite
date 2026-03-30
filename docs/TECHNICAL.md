# XCPlite Technical Details

This document contains advanced technical information about XCPlite's implementation, addressing modes, and configuration options.

## Instrumentation Cost and Side Effects

Keeping code instrumentation side effects as small as possible was one of the major goals, but of course there are effects caused by the code instrumentation:

### Data Acquisition

The measurement data acquisition trigger and data transfer is a lock-free implementation for the producer. It may be switched to simpler a mutex based implementation, if the platform requirements can not be met. DAQ trigger needs an external call to get the current time.

Some of the DAQ trigger macros do a lazy event lookup by name at the first time (for the convenience not to care about event handles), and cache the result in static or thread local memory.

The instrumentation to create events uses a mutex lock against other simultaneous event creations.

### Measurement of Function Parameters and local Variables

Measurement of function parameters and local variables has the side effect that the compiler will spill the parameters from registers to stack frame and always keeps local variables on the stack frame. This is a side effect of the in scope registration macros, so it will work even with optimization level > -O0. There is no undefined behavior caused by compiler optimizations.

### Calibration

The instrumentation to create calibration parameter segments use a mutex lock against other simultaneous segment creations.

During the creation of a calibration segment, a single heap allocation for 4 copies of the initial page is requested.  
(default page, reference page, working page and a single RCU swap page).  

Calibration segment access is thread safe and lock less.


### A2L Generation

The A2L generation simply uses the file system. There is no need for memory. It opens 4 different files, which will be merged on A2L finalization.

The A2l generation macros are not thread safe and don't have an underlying once pattern. It is up to the user to take care for locking and one time execution. There are helper functions and macros to make this easy.

### One-Time Execution Patterns

The overall concepts often relies on one time execution patterns. If this is not acceptable, the application has to take care for creating events and calibration segments in a controlled way. The A2l address generation for measurement variables on stack needs to be done once in local scope, there is no other option yet. Also the different options to create thread local instances of measurement data.

## A2L File Generation and Address Update Options

### Option 1: Runtime Generation (Volatile)

The A2L file is always created during application runtime. The A2L may be volatile, which means it may change on each restart of the application. This happens when there are race conditions in registering segments and events. The A2L file is just uploaded again by the XCP client.

To avoid A2L changes on each restart, the creation order of events and segments just has to be deterministic.

As a default, the A2L version identifier (EPK) is generated from build time and date. If the A2L file is not stable, it is up to the user to provide an EPK version string which reflects this, otherwise it could create undefined behavior.

### Option 2: Persistent Generation with Freeze Support

The A2l file is created only once during the first run of a new build of the application.

A copy of all calibration parameter segments and events definitions and of the parameter data is stored in a binary .bin file to achieve the same ordering in the next application start. BIN and A2L file get a unique name based on the software version string. The EPK software version string is used to check validity of the A2l and BIN file.

The existing A2L file is provided for upload to the XCP client or may be provided to the tool by copying it.

As a side effect, calibration segment persistence (freeze command) is supported.

### Option 3: External A2L Update Tools

Create the A2L file once and update it with an A2L update tool such as the CANape integrated A2L Updater or Open Source a2ltool.

**Note:** Currently, the usual A2L tools will only update absolute addresses for variables and instances in global memory and offsets of structure fields.

Data acquisition of variables on stack and relative addressing is not possible today. This might change in a future version of the A2L Updater.

### Option 4: No Runtime A2L Generation (Absolute Addressing)

Disable A2L generation or don't use the A2L generation functions at all.

Enable absolute addressing for calibration segments (`#define OPTION_CAL_SEGMENTS_ABS` in `xcplib_cfg.h`).

Use only absolute addressing mode, which is in this mode associated to address extension 0.

The A2l file may then be created and updated with any usual method of your choice, using CANape, A2L-Studio, A2L-Creator, a2ltool, ...

**Limitations:**
- Measurement of heap and stack is not possible anymore
- You are now limited to 32 bit address range starting at the module load address (`ApplXcpGetBaseAddr()`/`xcp_get_base_addr()`)

**Advantages:**
- Thread safe parameter modification using calibration segments is still assured
- Thread safety of measurement data acquisition is now in your responsibility, by using a safe fixed event for each individual measurement variable

### Option 5: XCPlite-Specific A2L Creator (Experimental)

Experimental. Use a XCPlite specific A2L creator tool, which is aware of the different addressing schemes and static markers created by the code instrumentation macros.

Experimental, work in progress. See `no_a2l_demo`.

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

XCPlite relative addressing: XCPLITE__ACSDD (for use case with external A2L generation)
0x00        - Absolute addressing mode (XCP_ADDR_EXT_ABS)
0x01        - Calibration segment relative addressing mode (XCP_ADDR_EXT_SEG)
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
- **GET_ID 5 (EPK)** mode = 0x01 is ignored by CANape. **Workaround:** always provide EPK via upload
- CANape executes GET_SEGMENT_MODE multiple times on the last memory segment before freeze request
- Address extension of memory segment is ignored by CANape. **Workaround:** using 0 for segment relative addressing
- Request for unique address extension per DAQ list is ignored by CANape (DAQ_KEY_BYTE == DAQ_EXT_DAQ). **Workaround:** Store the address extension per ODT entry
- CANape < V24 does not support shared axis in typedefs or THIS. axis references
- Transport Layer counter mutex could be avoided with alternative counter mode, which is not default in CANape
- Indicate when polling access is not possible. CANape assumes polling access is always possible
- Configuration for begin/end atomic calibration user defined XCP command is not default. Must be set once in a new CANape project to 0x01F1 and 0x02F1
- EPK segment is defined with 2 readonly pages, because of CANape irritations with mixed mode calibration segment. CANape would not care for a single page EPK segment, reads active page always from segment 0 and uses only SET_CAL_PAGE ALL mode


### Other Issues

- CANape ignores address extension of `loop_histogram` in ccp_demo, when saving calibration values to a parameter file. `loop_histogram` is a CHARACTERISTIC array, but it is in a measurement group


### TODO list

- Provide a way to deactivate XCP without code changes when accessing calibration segments 




## 5 · Appendix

### Static Instrumentation Markers for A2L Updater/Creator Tools

The code instrumentations creates static variables, to help an A2L Updater/Creator or an XCP tool to build an A2L file or its database from  linker map and debug information only.  
The markers make it possible to detect calibration segments, events, capture buffers and the scope where an event is triggered in the ELF/DWARF file.
Runtime A2L generation can be turned off. Measurement and calibration metadata may be added with the usual methods.  
  
This is currently in experimental state.  
The xcpclient tool has support to read this information from an ELF/DWARF file.  
CPP is not supported yet.  


```c
//Create calibration segment macro segment index once pattern
static tXcpCalSegIndex cal__##name;

// Create measurement event macro event id once pattern
// From  DaqCreateXxx(name), 
static tXcpEventId evt__##name
static tXcpEventId evt__dynname

// Daq capture macro (DaqCapture(event, var)) capture buffer
static __typeof__(var) daq__##event##__##var

// Daq event trigger macro event id once pattern
// From C macros DaqCreateAndTriggerXxx(name), DaqEventVar(name, ...), DaqEventExtVar(name, ...), ...)
static tXcpEventId trg__AAS__##name // For absloute and stack relative addressing [XCP_ADDR_EXT_ABS and XCP_ADDR_EXT_DYN]
static tXcpEventId trg__AASD__##name // For absolute, stack and relative addressing [XCP_ADDR_EXT_ABS, XCP_ADDR_EXT_DYN, XCP_ADDR_EXT_DYN+1]
static tXcpEventId trg__AASDD__##name // for multiple DYN address extensions [XCP_ADDR_EXT_DYN+1 ..= XCP_ADDR_EXT_DYN_MAX] 
```
