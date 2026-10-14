# Offline A2L Generation with xcpclient

XCPlite can generate the A2L file on the target at runtime ([on-target A2L generation](TECHNICAL.md#on-target-a2l-file-generation)),
or the A2L file is generated offline from the ELF file of the application by the ELF/DWARF to A2L generator built into the `xcpclient` tool (`tools/xcpclient/`). Offline generation is used by the `no_a2l` (Linux,MacOS) and `rtos` (FreeRTOS) build configurations. In this case, the library is built without A2L generator and without file system dependency.

The generator is specific to XCPlite. It knows the markers the XCPlite instrumentation macros leave in the ELF file, the relative addressing modes of XCPlite and other implementation details, so it cis able to creates a complete A2L file for measurement variables, calibration parameters in segments and complex types, which a general purpose A2L creator can not reconstruct from the debug information alone.

See `examples/no_a2l_demo`, `examples/no_a2l_demo_cpp` and `examples/freertos_demo` for complete examples with build scripts.

## Concept

The instrumentation macros place information in the ELF file at compile and link time. The library and the generator use it.  
The sections (`xcp_evts`,`xcp_cals`,...) are only emitted with the configuration option `OPTION_SECTION_REGISTRATION` (defined in the `no_a2l` and `rtos` configurations). Without it, a pure offline A2L file is not possible, see
[Without section registration](#without-section-registration):

| Source in the ELF file | Written by | Used for |
|---|---|---|
| `xcp_evts` section | `DaqCreateEvent`, `DaqCreateEventExt`, `DaqCreateAndTriggerEvent` | All events with name, cycle time and priority. The position of the descriptor in the section is the event id, on the target and in the A2L file |
| `xcp_cals` section | `CalSegDecl`, `CalSegDeclRef`, `CalSegCreate` | All calibration segments with the address and the size of their default page. The order of the descriptors is the segment number |
| `xcp_epk` section, or the `xcp_epk__` variable without `OPTION_SECTION_REGISTRATION` | `XcpCreateEpk` | The EPK software version string. `ADDR_EPK` is not the address of this string: the target keeps its own copy of the EPK given to `XcpInit`, which the `epk` segment and the EPK upload use, the A2L file gets the address the target maps to it (`XCP_ADDR_EPK`: `0xFFFFFF00` with absolute addressing of calibration segments, `0x80000000` with segment relative addressing) |
| `xcp_meta` section | `XCP_UNIT`, `XCP_LIMITS`, `XCP_COMMENT`, `XCP_READ_WRITE` | Metadata of measurement and calibration objects |
| DWARF scope of the `trg__<modes>__<event>` anchor variables | `DaqTriggerEvent`, `DaqCreateAndTriggerEvent`, `DaqTriggerEventExt`, `DaqEventVar` | The function in which an event is triggered, its stack frame and the addressing modes available at the trigger point |
| `XCPLITE__<signature>` variable | libxcplite | The addressing scheme of the target (`CASDD`, `ACSDD`, `AXSDD`, `CXSDD`), see [addressing modes](TECHNICAL.md#addressing-modes) |
| DWARF type of the `cap__<event>` capture structs | `DaqTriggerEventCapture`, `DaqCreateAndTriggerEventCapture` | The captured local variables of an event trigger, with their names, types and offsets in the capture struct |
| DWARF variables and types, ELF symbol table | Compiler and linker | Names, addresses, stack frame offsets and types of all global, static and local variables |

The macro expansions and the naming conventions of the markers are the contract between the library and the generator, they are
described in [TECHNICAL.md — Instrumentation Markers for Offline A2L Tools](TECHNICAL.md#instrumentation-markers-for-offline-a2l-tools).

The same link-time information is used by the library itself: `XcpInit` registers the events and calibration segments from the sections in a deterministic order, so the event ids and segment numbers in the A2L file stay valid independent of the code execution order, without a persistence file. What this requires from the build is described in
[With section registration: requirements and checks](#with-section-registration-requirements-and-checks).

### With section registration: requirements and checks

Section registration relies on the compiler and the linker. The instrumentation macros put constant descriptors into the sections
`xcp_evts` (events, 16 bytes each) and `xcp_cals` (calibration segments, 32 bytes each). `XcpInit()` and xcpclient walk through the
sections as arrays, between the linker symbols `__start_xcp_evts`/`__stop_xcp_evts` and `__start_xcp_cals`/`__stop_xcp_cals` (on macOS
`section$start$__DATA$xcp_evts` etc., provided by the Mach-O linker). The event id is the position of the descriptor in `xcp_evts`.
What has to be considered:

- **The linker must keep the sections and provide the boundary symbols.** GNU ld creates the `__start_`/`__stop_` symbols for a
  section with a C identifier name which the linker script does not place (orphan section), and keeps the section with `--gc-sections`
  because the library references these symbols. lld creates the symbols as well, but whether the references keep the section depends
  on the option `-z start-stop-gc`, whose default differs between lld versions. The `used` attribute of the descriptors only prevents
  the compiler from removing them, not the linker. A custom linker script, typical for microcontrollers, which places the sections
  explicitly, must `KEEP` them, define the boundary symbols around them and place them in memory the program can read:

  ```
  __start_xcp_evts = ABSOLUTE(.);
  KEEP(*(xcp_evts))
  __stop_xcp_evts = ABSOLUTE(.);
  __start_xcp_cals = ABSOLUTE(.);
  KEEP(*(xcp_cals))
  __stop_xcp_cals = ABSOLUTE(.);
  ```

  This is the safe way with any linker and any option.
  The `xcp_meta` section (metadata) is never referenced by the program, it needs `KEEP` with `--gc-sections`, otherwise the metadata is
  silently missing in the A2L file (xcpclient warning `No xcp_meta section found`). `examples/freertos_demo/freertos_esp32_demo/extra_linker_script.py` shows all of this for ESP-IDF, including an ESP32 specific pitfall: the flash data must stay one contiguous DROM segment, otherwise the bootloader maps only a part of it and the firmware crashes at startup.
- **No gaps between the descriptors.** Nothing else may be placed between the boundary symbols, and no alignment padding. The section size
  must be a multiple of the descriptor size, 16 bytes for `xcp_evts`, 32 bytes for `xcp_cals`.
- **Create each event in one place.** Every call site of `DaqCreateEvent`, `DaqCreateEventExt`, `DaqCreateAndTriggerEvent` or
  `DaqCreateAndTriggerEventCapture` emits its own descriptor. With `OPTION_DAQ_EVENT_LIST` (the `rtos` configuration) an event name
  which is created at two places shifts the ids of all events after it in the section: `XcpInit()` creates the event only once and prints an error message, the trigger macros use the position of their descriptor. The triggers of the following events then silently use a wrong or undefined event id, and the A2L file has the positions as well. Without `OPTION_DAQ_EVENT_LIST` (the `no_a2l` configuration) there are two events with the same name. xcpclient warns with `Event 'x' is defined N times`, and with `OPTION_DAQ_EVENT_LIST` `XcpInit()` reports
  `Event 'x' is created at more than one place`. To trigger an event in several functions, declare it
  once with `DaqDeclareEvent` at file scope and use `DaqTriggerEvent` in the functions.
- **Declare each calibration segment once**, the generator expects exactly one `calseg__<name>` descriptor per segment.
- **The numbers belong to one build.** The order of the descriptors, and with it the event ids and segment numbers, depends on the code
  and the link order. A new build may change them, the A2L file has to be generated again for each build. The EPK check detects an A2L
  file of another build only if the EPK changes with the build (for example with a build number or a hash in the EPK string).
- **Platforms:** ELF (Linux, QNX, embedded) and Mach-O (macOS, runtime only, the A2L generator reads ELF files). Not supported with MSVC
  (Windows) and in SHM mode.

How to make sure it works:

1. Check the ELF file: `readelf -S <elf-file> | grep xcp_` lists the sections with their sizes (multiples of 16 and 32). If the linker
   script merged them into another output section, check the boundary symbols: `nm <elf-file> | grep -E "__(start|stop)_xcp_"`.
2. Check the log of the target at `XcpInit()` (log level 3): `Preregistered N events from event descriptor section` and
   `Preregistered N new calibration segments or blocks from descriptor section`. `No xcp_evts section found` or
   `No xcp_cals section found for pre-registration` mean that the registration did not work, calibration segments declared with
   `CalSegDecl` are then never created and calibration silently has no effect. The error `Event 'x' is created at more than one place`
   means that event ids are wrong, see above.
3. Check the xcpclient log of the A2L generation: `Found XCP event descriptor memory section at address ...`, no warnings
   `New event '...' found, created with undefined event id` and `Event 'x' is defined N times`.
4. Final check with the running target: load the A2L file with xcpclient, `xcpclient --udp --dest-addr <ip> --a2l <a2l-file>`. It compares
   the events and calibration segments of the A2L file with the target, there must be no warnings `Event id of 'x' differs` or
   `Calibration segment index of 'x' differs`. Measure a variable of each event, for example with `--mea`.

### Without section registration

Without `OPTION_SECTION_REGISTRATION` the target creates the events and calibration segments at runtime, the event ids and the segment
numbers are the order of creation. This order is not in the ELF file, so the A2L file can not be generated offline alone:

- The generator still finds all events and calibration segments by their marker variables in the debug information, but not their numbers.
  The events get placeholder ids (0xFFFF, 0xFFFE, ...), the calibration segments are numbered in declaration order, the `epk` segment is
  always 0. Both are reported as warnings.
- **Connect to the target once.** Either generate the A2L file with the running target, without `--offline`, the event ids and segment
  numbers are then read from the target (GET_EVENT_INFO, GET_SEGMENT_INFO):
  `xcpclient --udp --dest-addr 192.168.0.206 --elf <elf-file> --create-a2l --a2l <a2l-file>`
  or generate it offline and correct it later with the running target, the file is rewritten and the original kept as `<a2l-file>.bak`:
  `xcpclient --udp --dest-addr 192.168.0.206 --a2l <a2l-file> --fix-a2l`
  `--fix-a2l` corrects the event and segment definitions and the addresses which contain an event id (stack and other dynamic addressing)
  or a segment number (calibration segment relative addressing). It accepts only A2L files created by xcpclient.
- The A2L file stays valid only as long as the creation order on the target is the same in every run. Keeping it deterministic is the
  responsibility of the application. Events created by `DaqCreateEvent` in concurrently running tasks get their ids in the order in
  which the tasks happen to execute the macro. Create all events and calibration segments in one place after `XcpInit()`, before any
  task is started: the events with `XcpCreateEvent(name, cycle_time_ns, priority)` in a fixed order, the segments with `CalSegCreate`.
  The event creation macros in the tasks then find the existing events by name (`XcpCreateEvent` returns the id of an existing event),
  the name must be identical. `createEventsAndCalSegs()` in `examples/freertos_demo/xcp_demo.c` shows this. A different build, or a code
  change in the creation order, may change the numbers, correct or generate the A2L file again with the target in that case.
- **Most XCP tools use the event ids and segment numbers of the A2L file as they are**, they do not detect or correct a difference to the
  target, the measurement or calibration then silently uses the wrong event or segment. xcpclient itself checks the numbers of a loaded
  A2L file against the target (warnings `Event id of 'x' differs` and `Calibration segment index of 'x' differs`), a DAQ measurement
  with wrong event ids is rejected by the target (`SetDaqListMode: Parameter out of range`).
- `CalSegDecl`, `CalBlkDecl` and `CalSegDeclRef` only work with section registration, without it they are a compile-time error (the
  segment would never be created). Use `CalSegCreate` after `XcpInit()`. `examples/freertos_demo/xcp_demo.c` shows both variants.

## Workflow

1. Build the application with debug information: `-g`, `CMAKE_BUILD_TYPE=Debug` or `RelWithDebInfo`. Optimized builds work, see the
   rules for the application code below. The generator reads ELF files: build on Linux or QNX, or for an embedded ELF target.
   macOS is not supported: the macOS linker does not put the DWARF debug information into the executable (Mach-O), it stays in the
   object files and in the `.dSYM` bundle. xcpclient rejects Mach-O files with an error message. The `no_a2l` and `rtos`
   configurations build and run on macOS, but the A2L file has to be generated from a Linux build of the same sources, as the
   `create_a2l.sh` scripts of the examples do with a remote build on a Linux target.
2. Generate the A2L file from the ELF file, offline or with the running target:

```bash
# Offline: events and segments from the ELF file only, transport layer parameters for the A2L IF_DATA from the command line
xcpclient --offline --udp --dest-addr 192.168.0.206 --elf build-no_a2l/no_a2l_demo --create-a2l --a2l no_a2l_demo.a2l

# Online: events and segments are checked against the running target (GET_EVENT_INFO, GET_SEGMENT_INFO), the EPK is checked
xcpclient --udp --dest-addr 192.168.0.206 --elf build-no_a2l/no_a2l_demo --create-a2l --a2l no_a2l_demo.a2l

# The ELF file may also be uploaded from the target (OPTION_ENABLE_ELF_UPLOAD)
xcpclient --udp --dest-addr 192.168.0.206 --upload-elf --elf no_a2l_demo.elf --create-a2l --a2l no_a2l_demo.a2l

# Restrict the variables to compilation units and names (regular expressions)
xcpclient --offline --elf build-no_a2l/no_a2l_demo --create-a2l --a2l no_a2l_demo.a2l --elf-unit-filter main --elf-var-filter "^(counter|params)"

# Only annotated variables (XCP_UNIT, XCP_LIMITS, XCP_COMMENT)
xcpclient --offline --elf build-no_a2l/no_a2l_demo --create-a2l --a2l no_a2l_demo.a2l --elf-skip-no-metadata

# A2L skeleton with events, segments and IF_DATA only, to be completed with other tools
xcpclient --offline --elf build-no_a2l/no_a2l_demo --create-a2l-template --a2l no_a2l_demo_template.a2l
```

3. Use the A2L file in the XCP tool. The xcpclient test client itself can work with the ELF file directly (`--elf` with `--mea`,
   `--cal`, `--list-mea`), no A2L file is needed for it. Variables without a fixed event get the event given by `--default-event`,
   in the generated A2L file and for the measurement with xcpclient.

`examples/no_a2l_demo_cpp/create_a2l.sh` shows a complete round trip: sync the sources to the target, build there, download the ELF
file and generate the A2L file. The command line reference is in [tools/xcpclient/README.md](../tools/xcpclient/README.md).

### Calibration segment addressing

The addressing scheme of the target is read from the `XCPLITE__<signature>` variable of the XCPlite library and written as `PROJECT_NO`
into the A2L header. The variable is an exported global, so it is read from the symbol table and found even when the debug information of
the library is not parsed (`--elf-unit-limit`) or the library was built without `-g`:

- `XCPLITE__ACSDD` (`OPTION_CAL_SEGMENTS_ABS` defined): calibration parameters are addressed by the absolute address of their default
  page with address extension 0. This is the usual choice for microcontrollers. The default pages of all segments must be in the 32 bit
  address range of the target and have static lifetime.
- `XCPLITE__CASDD` (default): calibration parameters are addressed by segment number and offset with address extension 0, the default
  pages may be anywhere in a 64 bit address space. The segment numbers are read from the target when connected, or derived from the order
  of the descriptors in the `xcp_cals` section.

## Rules for the application code

- Use the instrumentation macros (`DaqCreateEvent`, `DaqTriggerEvent`, `CalSegDecl`, `XcpCreateEpk`, ...), not the C API functions
  (`XcpCreateEvent`, `XcpCreateCalSeg`). Only the macros emit the sections and the anchor variables.
- Mark local measurement variables `volatile` (or use the `XCP_MEA` attribute). Otherwise an optimizing compiler might keep them in registers, the DWARF entry has no location and the variable is skipped.
- Declare calibration segments with `CalSegDecl` or `CalSegDeclRef` and give the default page static lifetime. File scope is
  recommended. Without section registration, create them with `CalSegCreate` after `XcpInit()` instead, see
  [Without section registration](#without-section-registration). The segment name and the name of the default page variable are
  identical by convention, the generator relies on it.
- Metadata macros name the object with `__` as path separator: `XCP_UNIT(params__delay_us, "us")` annotates the field `delay_us` of the
  instance `params`. A macro placed in the same namespace as the variable, or in the same function as a local variable, does not need a
  scope prefix: `XCP_COMMENT(input, ...)` in namespace `motor_control` annotates `motor_control.input`, `XCP_COMMENT(counter, ...)` in
  function `foo` annotates `foo.counter`. Explicit prefixes (`foo__counter`) are possible anyway.

## What the generator derives from the ELF file

This section describes how `xcpclient --create-a2l` discovers events, calibration segments, variables and types, for contributors
and for developers who need to understand why a variable does or does not appear in the generated A2L file.

### Events

Every descriptor in the `xcp_evts` section is an event. The event id is the position of the descriptor in the section, which is also how
`XcpInit` assigns the ids on the target. If the same event is created at several places (a `DaqCreateEvent` in several functions or
compilation units), the descriptor with the lowest address wins, on the target and in the generator. Without an `xcp_evts` section (a
linker script may merge it into another output section) the linker symbols `__start_xcp_evts` and `__stop_xcp_evts` are used. Without
both, the events get placeholder ids, which are corrected from the event information of the target when connected.

### Calibration segments

Every descriptor in the `xcp_cals` section is a calibration segment or block. The default page variable of a segment has the same name
as the segment, its DWARF type gives the size and the layout: the parameters become a `TYPEDEF_STRUCTURE` with an `INSTANCE`, or
`CHARACTERISTIC` objects for basic types. Variables whose address lies within a segment are calibration parameters, all other variables
are measurements.

### Trigger points and local variables

The trigger macros emit a static variable `trg__<modes>__<event>` in the function in which the event is triggered. Its DWARF scope gives
the function and the addressing modes available there (the mode letters, see the marker contract). Local variables of that function are
registered with stack frame relative addresses (address extension 2) and the event as fixed event, static variables in the function get
the event as well. The DWARF locations of local variables are relative to the frame base of the function (`DW_AT_frame_base`), and the
trigger macros pass exactly this frame base to the target (`xcp_get_frame_addr()` in `inc/xcplib.h`): the canonical frame address
(`__builtin_dwarf_cfa()`) for GCC, which describes the locals relative to the CFA, and the frame pointer (`__builtin_frame_address(0)`)
for clang, which describes them relative to the frame pointer register. The generator checks the frame base of the function of every
trigger and uses the offsets from the DWARF as they are. A function whose frame base is something else, for example the stack pointer of
a function without frame pointer under clang, gets a warning and its stack frame relative variables are not registered. clang describes
a local variable relative to the stack pointer when that is closer to the variable than the frame pointer, such variables are not
measurable, they are reported at debug level.

### Captured local variables

A local variable which the compiler keeps in a register has no address and can not be measured. Marking it `volatile` gives it a memory
location for its whole lifetime. The alternative is to capture it: `DaqTriggerEventCapture(event, counter, ratio)` declares a struct
`cap__<event>` in the function, copies the given variables into it and passes the address of the struct to the target as the base address
of address extension 3. The originals stay in their registers, the copy of a scalar is a single store instruction.

The generator takes the DWARF type of `cap__<event>`, which is a struct with one member per captured variable, and registers every member
as a measurement named `<function>.<member>`, with the event of the trigger as fixed event, address extension 3 and the offset of the
member in the struct as address. The member is named like the variable with one trailing underscore, which the generator removes again:
a member may not be named like the variable used in its own type expression, C++ forbids it. The measurements look exactly like the stack frame relative ones, the metadata markers of the captured
variables work unchanged. A variable which is captured is not registered a second time as a stack frame relative variable.

The macros work in C and in C++. In C++ a reference variable is captured as the object it refers to, and the captured objects must be
trivially copyable, since they are copied byte wise. A `const` variable can only be captured in C, in C++ a const member would leave the
capture struct without a default constructor. A bitfield member can not be captured in either language.

Captured variables do not depend on the stack frame of their function, so a function which only captures may be inlined. Asynchronous
access (polling) works like for any other event based relative address, the pending command is executed in the next trigger of the event,
while the capture struct is alive. One capture per event: if the same event is triggered with a capture in several functions, the first
one is used and the others are reported.

A function with an event trigger must not be inlined. An inlined function has a copy at each call site and possibly an out of line copy,
each with its own stack frame layout, and the event may be triggered from any of them, so there is no stack frame relative address which
is valid for all copies. xcpclient warns when the trigger of an event is found in an inlined function (an abstract instance with
`DW_AT_inline`, an inlined copy `DW_TAG_inlined_subroutine` or an out of line copy referring to the abstract instance) and does not
register the stack frame relative variables of the function, its static variables keep the function scope and the event. Mark such
functions `XCP_NOINLINE` (`inc/xcplib.h`). GCC does not inline external functions at `-O1`, clang inlines a function which is called once
already at `-O1`. Global variables and static variables
in functions without an event trigger are registered without a fixed event, in this case it is in the responsibility of the XCP tool user to assign an event which allows correct visibilty and consistent capture of the associated variables. CANape usually defaults to polling in this case, and each available event may be selected for synchronous data acquisition. 
With `--default-event <id|name>`, xcpclient assigns this event to such variables when it creates the A2L file (`DAQ_EVENT VARIABLE` with a
`DEFAULT_EVENT_LIST`), and measures them with it. The event is given by its id or by its name (a C identifier, e.g. `--default-event mainloop`),
a name is looked up in the event list of the ELF file (and of the XCP server when connected), xcpclient aborts when it is not found.

### Variables and symbols

- The address of a variable comes from its `DW_AT_location`. Variables without a location (declarations, `static const` data in a
  namespace, the metadata markers in optimized builds) are resolved from the ELF symbol table, by exact symbol names only: by
  `DW_AT_linkage_name`, by name, by the Itanium mangled name of a namespace scope variable (`_ZN13motor_controlL5inputE`) or of a static
  local in a C++ function (`_ZZ4mainE7counter`), by the clang name of a static local in a C function (`main.counter`) or by the GCC name
  of a static local in a C function (`counter.1`, told apart by size). A symbol whose name only ends with the variable name is never
  used, it belongs to a different object (the function `spiDetachMOSI` is not the variable `MOSI`). For variables inside a function
  only symbols with local binding are considered, a global symbol with the same name belongs to a different variable.
- A variable with `DW_AT_const_value` and without location is a compile-time constant without memory, for example a pin number
  `static const uint8_t MOSI = 11` from a board header. It is skipped (log level 4), unless one of the mangled or local static symbol
  names above exists: GCC describes the metadata markers in a namespace or a function the same way, although they are in memory.
  The plain name is not looked up for a constant, a symbol with this name is a different variable of another compilation unit.
- GCC describes a namespace scope variable with a declaration entry inside the namespace and a definition entry at compilation unit level
  (`DW_AT_specification`), both are merged into one variable.
- The segment index variables `calseg_id_<segment>` and `calblk_id_<block>` of `CalSegDecl`, `CalBlkDecl` and `CalSegDeclRef` are not
  registered, they are XCPlite internals.
- Static variables in functions are always prefixed with the function (`foo.counter`), also when the name is unique. The default page
  of a calibration segment declared in a function keeps the name of its segment. Global variables with the same name defined in several
  namespaces are prefixed with their namespace (`motor_control.input`).

### Type names

The `DW_AT_name` of a `DW_TAG_structure_type` or `DW_TAG_class_type` entry is the unqualified type name (`Input` for
`motor_control::Input`). The enclosing namespace, class or function is only visible from the position of the entry in the DWARF tree:
it is a child of the `DW_TAG_namespace`, `DW_TAG_class_type` or `DW_TAG_subprogram` entry. Type entries have no `DW_AT_linkage_name`,
only variables and functions carry a mangled name. Every compilation unit which uses a type has its own copy of the type entry.

A2L has one flat name space for `TYPEDEF_STRUCTURE`, so the generator records the enclosing scopes of the type entries while traversing
the tree and names the typedefs as follows:

- The typedef is named after the type. If struct/class types with the same name exist in different scopes, all of them are qualified
  with their scope (`motor_control.Input`, `valve_control.Input`, `MotorController.Params`), types with a unique name keep their plain name.
  Type names which are not valid A2L identifiers (template instantiations such as `TplStruct<float>`) are sanitized (`TplStruct_float_`).
- Typedefs with identical content are merged: the same type used by several variables, or the copies of a type from several compilation units.
- A name which is still used by a typedef with different content (types without a scope in different C files, or a type used for
  measurement and for calibration variables) simply get a numeric suffix (`state_1`), which is reported as a warning.
- The `TYPEDEF_MEASUREMENT` or `TYPEDEF_CHARACTERISTIC` of a struct field is named after the field. If another structure has a field
  with the same name but a different type or metadata, the name is qualified with the sanitized structure name (`TplStruct_float_.value`).

### Metadata

Each metadata macro emits a constant named `xcp_meta__<kind>__<name>` into the `xcp_meta` section, with `<kind>` one of `unit`, `min`,
`max`, `comment` or `read_write`. After the variables are registered, the constants are matched to their A2L objects: the name is looked
up qualified with the scope of the marker first (its namespace, or its function for a local variable), then unqualified. `__` in the name
is the path separator for the fields of typedef instances (`params__delay_us`, `motor_control__input__speed`).

A marker in a function annotates a variable of this function, written plain (`XCP_COMMENT(counter, ...)` in `foo` annotates `foo.counter`)
or with the scope prefix (`XCP_COMMENT(foo__counter, ...)`). If the function has no variable of that name, the marker annotates the object
of that name outside the function, for example a global variable used there. A marker at file scope annotates the global variable and
never a local variable of the same name in a function, it reaches a typedef field only when no object has its plain name.

GCC gives the `static const` marker constants inside a function no `DW_AT_location` and names their symbols `<name>.<number>`, so their
addresses come from the symbol table. Markers with the same name in several functions are told apart by their size, which differs as soon
as the annotation strings differ. Markers of the same name, function and size cannot be told apart, xcpclient warns and asks for the scope
prefixed form. Metadata never adds
objects, it only annotates variables which were registered from the sources above. With `--elf-skip-no-metadata` every variable without
any annotation is removed from the A2L file, a convenient way to publish only explicitly curated signals.

## Supported types and limitations

The DWARF type information is mapped to A2L objects as follows:

| C/C++ type | A2L representation |
|---|---|
| `bool`, integer and floating point types | `MEASUREMENT` or `CHARACTERISTIC` of the matching A2L data type |
| `enum` | integer of the enum's size; for variables the enumerators become a verbal conversion table, enum struct members are plain integers |
| one- and two-dimensional arrays | `MEASUREMENT` / `CHARACTERISTIC` with `MATRIX_DIM` (`VAL_BLK`, `CURVE`, `MAP`); arrays of structs become arrays of typedef instances |
| `struct`, `class`, template instantiations | `TYPEDEF_STRUCTURE` + `INSTANCE`; nested structs and classes become nested typedefs; private members are included; base class members are flattened into the derived type for all combinations of `struct`/`class` bases; `static`/`constexpr` members are skipped |
| pointers as struct or class members | the address value as unsigned integer of the target's pointer size, the pointee is not followed |

Not supported, future extensions, cases skipped and reported as warnings (log level 2 and above):

- Variables of pointer type (measure the pointed-to variable instead).
- Unions, bitfields and function pointers. A struct member of such a type is written as a one byte `UBYTE` placeholder so that the
  remaining members of the structure keep their offsets.
- Arrays with more than two dimensions (written as a byte array placeholder) (@@@@ TODO verify this claim).
- C++ pointer-to-member types (`DW_TAG_ptr_to_member_type`): a struct or class containing one cannot be read at all, so it and every
  class deriving from it end up without members. This is a limitation of the a2ltool DWARF reader this code is based on.
- C++ library containers (`std::vector`, `std::string`, smart pointers, ...) are read as the structs they are; the heap data behind them is not reachable (@@@@ TODO: Maybe add a blacklist feature to remove these).
- Variables addressed relative to a base pointer (`DaqTriggerEventExt`, the dynamic slots of `DaqEventVar`, address extension 3 and
  above) are not generated yet, only absolute and stack frame relative addressing is.
  (@@@@ TODO: Create a concept how to handle this)
- Thread local variables are not evaluated yet.
  (@@@@ TODO: future feature ?)
- Local variable in functions without events are skipped
  (@@@@ TODO: future feature ?, trigger if called by ?, with stack unwinding check)

## Diagnostics

The compilers which built the ELF file are logged as `Compiler: ...` (the `DW_AT_producer` of the compilation units), with their
version and the command line options which matter here, in particular the optimization level and the frame pointer.

Messages worth knowing when a variable is missing or looks wrong in the A2L file:

| Message | Meaning |
|---|---|
| `Struct/class type 'x' in unit has a different definition or object type than the existing typedef 'x', registered as typedef 'x_1'` | Two different types with the same name and no scope to qualify them with, or one type used for measurement and for calibration. Both get their own typedef. |
| `Local variable 'x' in function 'f' skipped, could not find event for dyn addressing mode` | The function contains no event trigger, so there is no stack frame anchor for its local variables. |
| `Variable 'x' not registered, no address` (log level 4) | The variable has no DWARF location, typically a local variable held in a register. Make it `volatile`. |
| `Local variable 'x' in function 'f' not registered, it has no location in the debug information, not implemented yet` | The compiler emitted no `DW_AT_location` for the variable and there is no symbol for it either, so there is nothing to derive an address from. Seen with clang `-O1` for the aggregate locals (struct, array) of a function whose scalar locals all got a location, `volatile` or not. Not handled yet. |
| `'x': the variable is split into N slices (DW_OP_piece)`, `only a slice of the variable has a location` | The compiler spread the variable over several places (registers, constants, a stack slot). There is no single address for it, so it is not measurable. Declare it `volatile`. |
| `'x': location is a register ...`, `'x': variable location is stack-pointer-relative ...` | The location expression of the variable can not be evaluated without a running program, the variable is not measurable. Usual for local variables in optimized code, make the variable `volatile` to keep it in memory. |
| `Global variable 'x' not registered, address ... out of the 32 bit XCP address range` | The variable is outside the addressable range, see the addressing modes. |
| `Metadata 'xcp_meta__...': no matching registry entry for '...'` | The annotated variable was not registered, or the name does not match. Check the scope prefix and the `__` path. |
| `Metadata variable '...' address is 0` | The marker has no DWARF location and no resolveable symbol. |
| `Event '...' is triggered with a capture in N functions, only the one in function ... is used` | The same event is triggered with `DaqTriggerEventCapture` in several functions, whose capture structs have different layouts. Use one event per capture. |
| `No target signature found in ELF file` | The `XCPLITE__<signature>` variable of the XCPlite library is missing, absolute addressing of calibration segments is assumed. A build with segment relative addressing (`CASDD`, `CXSDD`) then gets wrong calibration addresses. |
| `New event '...' found, created with undefined event id ...` | No `xcp_evts` section and no linker symbols, the application is built without section registration. Generate the A2L file with the running target, see [Without section registration](#without-section-registration). |
| `Calibration segment 'x' is declared at file scope with CalSegDecl, CalBlkDecl or CalSegDeclRef, but the ELF file has no xcp_cals section` | These macros only emit a descriptor, the segment is created by the `xcp_cals` section scan in `XcpInit`. Without `OPTION_SECTION_REGISTRATION` the target never creates it and calibration has no effect. Use `CalSegCreate`/`CalBlkCreate`. The current `xcplib.h` rejects this at compile time, the message is for applications built with an older one. |
| `No xcp_cals section: the calibration segment numbers (...) are assumed in the order of the debug information` | Without section registration the target numbers the segments in the order in which `CalSegCreate` is executed. The generator can not know this order, it uses the declaration order (the `epk` segment is always 0). Generate the A2L file with the running target, see [Without section registration](#without-section-registration). |
| `A2L file ... differs from target, use automatic correction (--fix-a2l)` | The event ids or segment numbers of the loaded A2L file do not match the target. `--fix-a2l` rewrites the A2L file with the numbers of the target. |
| `--fix-a2l: A2L file '...' was not created by xcpclient` | `--fix-a2l` only corrects A2L files created by xcpclient, a file of another A2L creator could lose information when it is written again. |
| `Calibration segment reference page variable 'x' has N usable definitions, expected 1` | The name of the default page variable is ambiguous, restrict the compilation units with `--elf-unit-filter`. |
| `No EPK string found, neither the section xcp_epk nor the variable xcp_epk__ exist` | The application does not call `XcpCreateEpk(epk)`. The A2L file gets no `EPK`/`ADDR_EPK`, so neither the tool nor CANape can check whether the A2L file matches the target software. Call `XcpCreateEpk` with the same string as `XcpInit`. |
| `No EPK string found: the variable xcp_epk__ (XcpCreateEpk) exists in the debug information, but it has no address` | The compiler or the linker (`--gc-sections`) removed the EPK string. The application was built with an older `xcplib.h`, whose `XcpCreateEpk` did not reference the array. If it happens with the current one, add `KEEP(*(.rodata.*xcp_epk__*))` to the linker script. |
| `The EPK address ... is from the debug information only, there is no xcp_epk__ symbol at this address` (info) | The EPK string is correct, but the array itself was removed and the compiler described its location with an identical string literal. Same cause and fix as above. |
| `The target changes the EPK '...' to '...'` | `XcpInit` truncates the EPK to 31 characters and replaces space, tab and colon with an underscore. The A2L file gets the changed EPK, which the target reports. Use an EPK string without these characters, so `XcpCreateEpk` and the target agree. |
| `EPK mismatch: A2L file '...' has EPK '...', target reports EPK '...'` | The A2L file does not belong to the running build. `--yes` overrides the check. |
| `'...' is a Mach-O (macOS) binary, macOS is not supported` | The application was built on macOS. Executables built on macOS contain no DWARF debug information, build on Linux or for an embedded ELF target. |
| `... does not contain DWARF2+ debug info. The section .debug_info is missing.` | The application was built without `-g`, or the debug information was stripped. |

The messages about a single variable are only warnings if the variable could become an A2L object at all. The variables of the
compilation units which `--elf-unit-filter` excludes, and the marker variables which the XCPlite macros generate in the code
(`evt__`, `trg__`, `cap__`, `xcp_meta__`, `evt_id_<event>`, `calseg_id_<segment>`), are reported at log level 4 (`--log-level=4`) instead: they would
otherwise bury the relevant warnings under hundreds of messages about the internals of the XCPlite library itself.

xcpclient exits with status 1 when the A2L file could not be created or any other error occurred, scripts can rely on the exit
status. The `create_a2l.sh` scripts of the examples check the exit status and the existence of the A2L file, and print the error
lines of the xcpclient log when the generation failed.

## Other tools

The A2L template generated with `--create-a2l-template` contains the IF_DATA, the EPK, the memory segments and the events, and can be
completed with other tools:

- **Vector CANape A2L editor with ELF support**: create a new XCP on Ethernet device from the template, enable the ELF file in the
  device configuration and add measurement and calibration objects in the A2L editor. For calibration segments, add the default page
  structure as an `INSTANCE` of a `TYPEDEF_STRUCTURE` or the parameters as `CHARACTERISTIC` objects.
- **Vector A2L-Toolset A2L-Creator**: the examples contain metadata annotations as comments (`@@ SYMBOL`, `@@ STRUCTURE`, ...) for
  the commercial A2L Creator.
- **a2ltool** (open source), for example to add the calibration segment `params` and the measurement `counter` to the template:

```bash
a2ltool --update --measurement-regex "counter" --characteristic-regex "params" --elffile no_a2l_demo.elf --enable-structures --output no_a2l_demo.a2l
```

These tools reconstruct absolute addresses only. Stack frame relative and segment relative addressing needs the XCPlite specific
generator.
