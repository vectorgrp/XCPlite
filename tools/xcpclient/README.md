# xcpclient

XCP test client and A2L generator implementation in Rust

Used for integration testing and for uploading or generating A2L files.  
Partial XCP implementation with hard-coded protocol settings for XCPlite.  

XCP client v4.x for testing XCP servers and managing A2L and HEX files.

This tool can:
- Connect to XCP on Ethernet servers via TCP or UDP and show information about the XCP protocol and the target ECU
- Upload A2L or ELF files from XCP servers (GET_ID command)
- Create A2L files from ELF/DWARF debug information including event and memory segment information obtained from the XCP server or from an ELF file
- Create A2L file templates for from a XCPlite ELF/DWARF
- Fix A2L files with event and memory segment information from the XCP server
- Read and write calibration variables (CAL)
- Upload (from target) and download (to target) binary files (Intel-HEX) with calibration segment data
- List available measurement variables and parameters with regex patterns
- Test data acquisition (DAQ)
- Execute test sequences
- Check the EPK of the A2L file against the EPK reported by the target, abort on mismatch unless `--yes` is given
- Load options from a TOML configuration file (`--config`, see `xcpclient.toml`)

## Offline A2L generation

xcpclient contains the XCPlite specific ELF/DWARF to A2L generator: `xcpclient --create-a2l --elf <binary>` reads the markers the
XCPlite instrumentation macros leave in the ELF file (events, calibration segments, trigger points, metadata) and the DWARF debug
information, and writes a complete A2L file without any runtime A2L code in the application. The workflow, the rules for the
application code, the naming of types and variables, the supported types and the diagnostics are described in
[docs/OFFLINE_A2L.md](../../docs/OFFLINE_A2L.md), the markers in [docs/TECHNICAL.md](../../docs/TECHNICAL.md#instrumentation-markers-for-offline-a2l-tools).

The generator reads ELF files with DWARF debug information (Linux, QNX, embedded targets). macOS is not supported: executables built
on macOS (Mach-O) contain no DWARF debug information, the macOS linker leaves it in the object files and in the `.dSYM` bundle.
xcpclient rejects Mach-O files with an error message, generate the A2L file from a Linux build of the application instead.

xcpclient exits with status 1 on any error (connection failed, ELF or A2L file not found or not usable, test failed), so scripts can
detect failures by the exit status.

## Usage

```text
Usage: xcpclient [OPTIONS]

Options:
      --config <CONFIG>
          Load arguments from a TOML config file. Command-line arguments take precedence
          
          [default: ""]

      --log-level <LOG_LEVEL>
          Program flow log level (Off=0, Error=1, Warn=2, Info=3, Debug=4, Trace=5)
          
          [default: 3]

      --verbose <VERBOSE>
          Content information detail verbosity level
          
          [default: 0]

      --dest-addr <DEST_ADDR>
          XCP server address (IP address or IP:port). If port is omitted, uses --port parameter
          
          [default: 127.0.0.1]

      --port <PORT>
          XCP server port number (used when --dest-addr doesn't include port)
          
          [default: 5555]

      --bind-addr <BIND_ADDR>
          Bind address (IP address or IP:port). If port is omitted, system assigns an available port
          
          [default: 0.0.0.0]

      --baud-rate <BAUD_RATE>
          Baud rate for XCP communication (only applicable for certain protocols)
          
          [default: 115200]

      --tcp
          Use TCP for XCP communication..

      --udp
          Use UDP for XCP communication

      --sxi
          Use SxI for XCP communication

      --connect-mode <CONNECT_MODE>
          XCP connect mode
          
          [default: 0]

      --offline
          Force offline mode (no network communication), communication parameters are used to create A2L file

      --a2l <A2L>
          Specify and overide the name of the A2L file name. If not specified, The A2L file name is read from the XCP server
          
          [default: ""]

      --upload-a2l
          Upload A2L file from XCP server. Requires that the XCP server supports GET_ID A2L upload

      --create-a2l
          Build an A2L file template from XCP server information about events and memory segments. Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands. Insert all visible measurement and calibration variables from ELF file if specified with --elf or --upload-elf

      --create-a2l-template
          Build a minimal A2L template from XCP server event and memory segment information only. No variables or types are registered; the result is a skeleton A2L file. Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands

      --fix-a2l
          Correct the event ids and calibration segment numbers of the given A2L file with XCP server information and rewrite it, the original is kept as <file>.a2l.bak. Only for A2L files created by xcpclient (--elf and --create-a2l), typically generated offline without section registration. Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands

      --embed-aml
          Embed the XCP A2ML description into an A2L file created by xcpclient (--elf, --create-a2l, --create-a2l-template), instead of including XCP_104.aml. The A2L file is self-contained, XCP_104.aml is not needed next to it. --fix-a2l keeps an embedded A2ML description

      --upload-elf
          Upload ELF file from XCP server. Requires that the XCP server supports proprietary GET_ID ELF upload command

      --elf <ELF>
          Specify the name of an ELF file, create an A2L file from ELF debug information. If connected to a XCP server, events and memory segments will be extracted from the XCP server. ELF files with DWARF debug information only (Linux, QNX, embedded targets), macOS Mach-O executables are not supported
          
          [default: ""]

      --elf-unit-limit <ELF_UNIT_LIMIT>
          Parse only compilations units <= n
          
          [default: 18446744073709551615]

      --elf-var-filter <ELF_VAR_FILTER>
          Regex pattern to filter variable names when registering from an ELF file. Only variables whose names match the pattern are included in the A2L output. If not specified (or empty), all variables are registered
          
          [default: ""]

      --elf-skip-no-metadata
          Skip variables without any metadata (XCP_UNIT / XCP_LIMITS / XCP_COMMENT) when creating an A2L file from an ELF file. Only variables that have at least one metadata annotation are included in the A2L output

      --elf-unit-filter <ELF_UNIT_FILTER>
          Regex pattern to filter variables by their compilation unit (source file) name. Only variables defined in compilation units whose name matches are included in the A2L output. If not specified (or empty), variables from all compilation units are registered
          
          [default: ""]

      --bin <BIN>
          Specify the pathname of a binary file (Intel-HEX) for calibration parameter segment data
          
          [default: ""]

      --upload-bin
          Upload all calibration segments working page data from target and store into a binary file. Requires that the XCP server supports GET_ID A2L upload. A .bin file is an XCPlite persistence file with the event list and the context list (conv.contexts in the A2L file), a .hex file contains the segment data only

      --download-bin
          Download all calibration segments working page data in a binary file to the target

      --list-mea <LIST_MEA>
          Lists all specified measurement variables (regex) found in the A2L file
          
          [default: ""]

      --mea <MEA>...
          Specify variable names for DAQ measurement (list), may be list of names separated by space or single regular expressions (e.g. ".*")

      --default-event <ID|NAME>
          Event for variables without a fixed event (global variables and static variables in functions without an event trigger), given by event id or event name. Used for their DAQ measurement and assigned to them as default event when an A2L file is created from an ELF file. An event name is looked up in the event list (from the XCP server, the ELF file or the A2L file), xcpclient aborts when it is not found. If not specified, such variables get no event and can not be measured with xcpclient

      --time <TIME>
          Time limit measurement duration to n s. 0 means infinite
          
          [default: 0]

      --csv <CSV>
          Save measurement data to a CSV file. If not specified, data is printed to the console. CSV format: time_ns,daq,name,value,unit  (one row per measurement sample). Values are physical values (factor and offset applied), the value of an enum is its label
          
          [default: ""]

      --mdf <FILE>
          Save measurement data to an ASAM MDF 4.1 file. One channel group per DAQ list, the payload of the DAQ packets is stored as received. Values are raw values, the MDF file contains the conversions (factor and offset, enum labels) and units. May be combined with --csv and --perfetto
          
          [default: ""]

      --mcap <FILE>
          Save measurement data to an MCAP file (https://mcap.dev). One channel per DAQ list, the topic is the event name, the payload of the DAQ packets is stored as received, as ros1 messages. Values are raw values, MCAP has no conversions or units, they are in the comments of the message definitions and in the channel metadata. May be combined with --csv, --mdf and --perfetto
          
          [default: ""]

      --perfetto <FILE>
          Save measurement data to a trace file for Perfetto (Chrome Trace Event JSON format, open it with https://ui.perfetto.dev). Spans (DaqSpan) are slices in a track per context and thread, numeric signals are counter tracks, enum signals are state tracks, span links (xcp::LinkSpan) are flows (arrows) from the producer span to the consumer span. The span and link signals of the contexts needed for the trace are added to the measurement, see --trace-auto. May be combined with --csv, the added signals are not written to the CSV file
          
          [default: ""]

      --trace-auto <LEVEL>
          Trace signals added to the measurement with --perfetto: off (none, only hints which signals are missing), context (the spans of the contexts of the measured signals), links (also the span links of these contexts and, if there are any, the spans of all contexts as possible producers), all (the spans and span links of all contexts). The context of a signal is the prefix of its name or of the name of its event (<context>.<name>)
          
          [default: links]
          [possible values: off, context, links, all]

      --list-cal <LIST_CAL>
          Lists all specified calibration variables (regex) found in the A2L file
          
          [default: ""]

      --cal <NAME> <VALUE>
          Set calibration variable to a value (format: "variable_name value")

      --test
          --test Execute a test sequence on the XCP server

  -y, --yes
          Automatically confirm EPK mismatch and other safety warnings. Use in scripts to suppress interactive prompts

  -h, --help
          Print help (see a summary with '-h')

  -V, --version
          Print version
```


## Build and Install

```bash
cd tools/xcpclient
cargo install --path . --locked
```

## Test

```bash
cd tools/xcpclient
cargo test
```


## Examples

### List all calibration or measurement variables

```bash
xcpclient --dest-addr 192.168.0.206 --udp --list-cal .
xcpclient --dest-addr 192.168.0.206 --udp --list-mea .
```

### Set a calibration variable

Set variable counter_max to 1000
```bash
xcpclient --dest-addr 192.168.0.206 --port 5555 --tcp --cal counter_max 1000
```

### Measure variables

Measure everything from uploaded A2L file for 5 seconds with detailed log to terminal:  

```bash
xcpclient --dest-addr=127.0.0.1  --udp --upload-a2l --mea ".*" --time 5 --verbose 2
```

With A2L file given:  

```bash
xcpclient --dest-addr=192.168.0.206  --tcp --a2l hello_xcp.a2l  --mea ".*" 
```

Variables without a fixed event (global variables in an A2L file generated from an ELF file) are measured with the event given by `--default-event`. The event is given by its id or by its name (a C identifier, looked up in the event list of the XCP server, the ELF file or the A2L file, xcpclient aborts if the name is not found). When the A2L file is created from an ELF file, `--default-event` assigns the event to such variables in the A2L file:

```bash
xcpclient --dest-addr=192.168.0.146 --udp --a2l freertos_demo.a2l --default-event 0 --mea global_counter --time 5
xcpclient --dest-addr=192.168.0.146 --udp --a2l freertos_demo.a2l --default-event mainloop --mea global_counter --time 5
```

With ELF file given, creates an A2L file from the ELF file and XCP server information about events and memory segments, then measures the specified variable:  

```bash
xcpclient --udp --elf build/hello_xcp --mea "counter" --verbose 2
```

### Upload the A2L file to be used from the target

```bash
xcpclient --dest-addr=192.168.0.206:5555 --tcp --upload-a2l   
```

### Create an A2L file for a target from ELF without on-target A2L generation support

Create an A2L file from the ELF file (and optional XCP server information about events and memory segments), then save the A2L file as hello_xcp.a2l:
```bash

# offline
xcpclient --offline --elf examples/no_a2l_demo/CANape/no_a2l_demo.elf --a2l examples/no_a2l_demo/CANape/no_a2l_demo_template.a2l --create-a2l-template 
xcpclient --offline --elf examples/no_a2l_demo/CANape/no_a2l_demo.elf --a2l examples/no_a2l_demo/CANape/no_a2l_demo.a2l --create-a2l

# online with XCP server information about events and memory segments
xcpclient --dest-addr=192.168.0.206:5555 --udp  --create-a2l --elf no_a2l_demo.elf --a2l no_a2l_demo.a2l 
```

Upload the ELF file into hello_xcp.elf and create an A2L file from the ELF file and XCP server information about events and memory segments, then save the A2L file as hello_xcp.a2l:
```bash
xcpclient --dest-addr=192.168.0.206:5555 --udp --create-a2l --upload-elf --elf hello_xcp.elf --a2l hello_xcp.a2l 
```

### Upload the current calibration data from the target into an Intel-HEX file

```bash
xcpclient --dest-addr=192.168.0.206:5555 --udp --a2l hello_xcp.a2l --upload-bin --bin hello_xcp.hex
```


#### Demo

```bash
# A2L from the ELF file of no_a2l_demo, offline
cargo r -- --elf no_a2l_demo.elf --log-level 3 --create-a2l --a2l no_a2l_demo.a2l --offline

# A2L with the event and segment information of the running target
cargo r -- --dest-addr 192.168.0.206 --udp --elf no_a2l_demo.elf --log-level 3 --create-a2l --a2l no_a2l_demo.a2l

cargo r -- --dest-addr 192.168.0.206 --udp --elf no_a2l_demo.elf --log-level 3 --create-a2l --a2l no_a2l_demo.a2l --list-mea 'counter'

cargo r -- --dest-addr 192.168.0.206 --udp --elf no_a2l_demo.elf --log-level 3 --create-a2l --a2l no_a2l_demo.a2l --mea 'counter' --time 5 --verbose 2
```




## Measurement limits

The console output shows each sample with the name of its event:

```
EVENT: SigGen1.clip (id=2, daq=2), lost=0, t=3855188843115083ns (+3473us)
 SigGen1.input = -17.514199949800968 Volt
 SigGen1.clipped = -17.514199949800968 Volt
```

Signals of an event which are adjacent in memory (same address extension, no gap between them) are merged and measured with one ODT entry, for example the members of a struct or the elements of an array. This saves DAQ memory and copy operations in the XCP server. Gaps are not bridged, a struct with members which are not measured needs an ODT entry for each run of measured members.

xcpclient uses one DAQ list with one ODT (one DAQ packet) for up to 123 ODT entries of an event. An event with more ODT entries, or with more signal bytes than fit into one DAQ packet, gets as many DAQ lists as needed. The XCP server samples all DAQ lists of an event with the same timestamp. In the console output and in the CSV file such an event appears once per DAQ list, with the same event name and timestamp and a different `daq` number.

A signal can not be split over several ODT entries, ODTs or DAQ lists. xcpclient reports a signal which is too large as an error and does not start the measurement. There are two limits, both are properties of the XCP server:

- The maximum size of an ODT entry (`MAX_ODT_ENTRY_SIZE_DAQ` from `GET_DAQ_RESOLUTION_INFO`). In XCPlite this is `XCP_MAX_ODT_ENTRY_SIZE` (248 bytes). It is the limit for merged signals as well. The protocol limit is 255 bytes, a larger signal can not be measured as one signal.
- The payload of a DAQ packet, `MAX_DTO` without the 4 byte header and the 4 byte timestamp. In XCPlite this is `XCPTL_MAX_DTO_SIZE - 8`, 1016 bytes by default and 240 bytes with `OPTION_QUEUE_64_FIX_SIZE`. If a signal does not fit, `XCPTL_MAX_DTO_SIZE` has to be increased.

Structs and arrays are not affected, they are measured as their basic type elements (`points_._0.x`, `parameters.counter_max`).

### Lost packets and UDP receive buffer

The XCP server numbers its messages with a 16 bit transport layer counter. xcpclient detects lost DAQ packets from the gaps of this counter and reports them every second and at the end of the measurement. A packet is lost when the transmit queue of the XCP server overflows (XCPlite counts these packets in the counter, it prints `Transmit queue overflow`) or when the UDP receive buffer of xcpclient is full and the system drops a whole datagram (up to about 75 packets with MTU 1500, about 400 with MTU 8000). A gap of 65536 or more packets can not be detected.

xcpclient requests an 8 MB UDP receive buffer and logs the size it got. The system default is too small for high data rates (Linux 208 KB, macOS 768 KB). On Linux the size is limited by `net.core.rmem_max` (default 208 KB, xcpclient then gets 416 KB, Linux reports twice the set size) and xcpclient warns. To allow 8 MB:

```bash
sudo sysctl -w net.core.rmem_max=8388608
```

Add `net.core.rmem_max=8388608` to `/etc/sysctl.conf` to keep it after a reboot.



### DAQ memory

The number of signals which can be measured at the same time is limited by the DAQ memory of the XCP server. XCPlite needs 6 bytes per ODT entry and 20 bytes per DAQ list of `OPTION_DAQ_MEM_SIZE` (default 12288 bytes, about 2000 signals when no signals can be merged). The server rejects a larger measurement with a memory overflow error. xcpclient uses at most 1024 DAQ lists.



## Save measurement data to MDF4

`--mdf <file>` writes the measurement to an ASAM MDF 4.1 file, which can be opened with CANape, asammdf and other MDF tools.

```bash
xcpclient --udp --mea '.*' --time 5 --mdf measurement.mf4
```

The payload of the DAQ packets is stored as received, without decoding, the MDF metadata describes its binary layout. Each DAQ list is a channel group with the event name as acquisition name and the ECU name as source. A record is the DAQ list number (2 byte record id), the timestamp (64 bit, ticks since the measurement start, the master channel `time` in s) and the payload of the DAQ packet. The signals are raw values with their conversion: factor and offset as linear conversion, enums as value to text conversion, and the unit.

The file is unsorted (all channel groups in one data group), MDF tools sort it when they open it. It is finalized when the measurement stops. If xcpclient is terminated without stopping the measurement, the file stays unfinalized (`UnFinMF`), MDF tools can repair it.

## Save measurement data to MCAP

`--mcap <file>` writes the measurement to an [MCAP](https://mcap.dev) file, the container format of Foxglove, Lichtblick and ROS 2 tools.

```bash
xcpclient --udp --mea '.*' --time 5 --mcap measurement.mcap
```

MCAP stores timestamped messages, a schema describes their format. Each DAQ list is a channel, its topic is the event name (`/Mainloop`). A message is the payload of a DAQ packet as received, `log_time` is the DAQ timestamp in ns. The schema is a ROS 1 message definition (`ros1msg`): ros1 serializes packed and little endian, so the definition describes the binary layout of the payload exactly, gaps are `uint8[n]` padding fields. The field names are the signal names with `.` and other characters which are not allowed replaced by `_` (`Mainloop.value1` is the field `Mainloop_value1`).

MCAP has no conversions and units, the values are raw values. Factor and offset, enum labels and the unit of each signal are in the comments of the message definition and in the channel metadata (`<field>.factor`, `<field>.offset`, `<field>.labels`, `<field>.unit`, `<field>.name` is the signal name). A metadata record `xcp` contains the ECU name and the timestamp resolution.

The messages are written in zstd compressed chunks of 1 MB, the index and statistics are written when the measurement stops. If xcpclient is terminated without stopping the measurement, the last chunk is lost and the file has no index, `mcap recover` from the [MCAP CLI](https://mcap.dev/guides/cli) repairs it.

## Export spans and signals to Perfetto

`--perfetto <file>` writes the measurement as a trace file in the Chrome Trace Event JSON format. Open it with <https://ui.perfetto.dev> ("Open trace file"), the file is processed locally in the browser.

```bash
xcpclient --udp --mea '.*' --time 5 --perfetto trace.json
xcpclient --udp --mea 'Mainloop.channel1' --time 5 --perfetto trace.json
```

The second command measures one signal, xcpclient adds the span and link signals needed for the trace, see "Trace signals" below.

| In Perfetto | From XCP |
|---|---|
| Process | A context (`xcp::Context`, for example `SigGen1`). Signals which do not belong to a context are in a process named after the ECU |
| Thread track | A thread which executed spans in the context (`thread 1`, `thread 2`, ...), from the signal `<context>.thread` |
| Slice | A span (`DaqSpan`), nested spans are stacked |
| Counter track | A numeric signal as physical value, with its unit |
| State track | An enum signal, one slice per state, named with the enum label |
| Flow (arrow) | A span link (`xcp::LinkSpan`), from the producer span to the consumer span |

- A context is found in the A2L file by its signals `<context>.span`, `.level` and `.span_dt`. A signal belongs to the context which is the prefix of its name or, if none, of the name of its event (`<context>.<name>`). Each context is a process, also when its spans are not measured.
- `<context>.thread` needs `OPTION_DAQ_SPAN_THREAD` on the target (not defined by default). Without it, there is one track `spans` per context. This is correct as long as a context is executed by one thread at a time, otherwise spans are cut and xcpclient prints a warning with the number of samples which did not fit.
- Lost samples and a measurement which starts inside a span are handled: the open spans are resynchronised to the nesting level of the next sample, a span the begin of which was not seen is named `(unknown)`. Spans which are still open at the end of the measurement end at the last sample of their track.
- A span link is a sample of the event `<context>.links` of the consumer context, with the signals `<context>.link_context` (enum `conv.contexts`), `.link_time` and, if available, `.link_span`, `.link_thread` and `.link_consumer_thread`. It is written as a flow in format v1: `"ph":"s"` on the track of the producer context at the producer timestamp, and `"ph":"f"` with `"bp":"e"` on the track of the consumer at the time of the sample. Perfetto binds each end to the innermost slice which encloses its timestamp. The span signals of the producer context must be measured as well (added with `--trace-auto links`), links which can not be resolved (producer context not measured, captured before the measurement start) are counted and reported. In the Perfetto UI, click a slice to see its arrows.
- Timestamps are relative to the start of the measurement, with ns resolution.
- A span is written when it ends, so the file is not sorted by time. Perfetto sorts on import.

See `examples/thread_context_demo` for contexts and spans.

### Trace signals

With `--perfetto`, it is enough to select the signals of interest with `--mea`. The span and link signals of the contexts are added to the measurement, depending on `--trace-auto`:

| `--trace-auto` | Added |
|---|---|
| `off` | Nothing. Warns which signals are missing for spans and arrows |
| `context` | The spans of the contexts of the selected signals |
| `links` (default) | Also the span links of these contexts and, if there are any, the spans of all contexts. The producer of a span link is only known at runtime, so every context is a possible producer |
| `all` | The spans and span links of all contexts |

The added signals are listed in the log and are not written to the CSV file. The spans of a context with a fast task add data rate, in `thread_context_demo` the spans of `SigGen1` (three nested spans every 25 µs) are most of the measured data. Use `--trace-auto context` to keep them out, the arrows from these contexts are then counted as not resolved.



## Notes

- The CSV file has the columns `time_ns,daq,name,value,unit`, the values are physical values or enum labels.
- At least one sample of any event is needed every 2.147 s (with 1 ns ticks), otherwise the 64 bit
  timestamp is ambiguous.
- Maybe add a `--raw` switch in xcpclient for unconverted values in console and CSV.
