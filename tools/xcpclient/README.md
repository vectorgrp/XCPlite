# xcpclient

XCP test client and A2L generator implementation in Rust

Used for integration testing and for uploading or generating A2L files.  
Partial XCP implementation with hard-coded protocol settings for XCPlite.  

XCP client v3.x for testing XCP servers and managing A2L and HEX files.

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
          Update the given A2L file with XCP server information about events and memory segments. Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands

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
          Upload all calibration segments working page data from target and store into a binary file. Requires that the XCP server supports GET_ID A2L upload

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
          Save measurement data to a CSV file. If not specified, data is printed to the console. CSV format: time_ns,daq,name,value  (one row per measurement sample)
          
          [default: ""]

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

### Create an A2L file for a target from ELF without on target A2L generation support

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
