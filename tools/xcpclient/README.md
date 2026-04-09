# xcpclient

XCP test client implementation in Rust

Used for integration testing and for uploading or generating A2L files.  
Partial XCP implementation with hard-coded protocol settings for XCPlite.  


This tool can:

- Connect to XCP on Ethernet servers via TCP or UDP
- Upload A2L files from XCP servers (GET_ID command)
- Create complete A2L files from ELF debug information and the XCP servers event and memory segment information
- Create A2L templates from the XCP server event and memory segment information
- Read and write calibration variables (CAL)
- Configure and acquire measurement data (DAQ)
- List available variables and parameters with regex patterns
- Execute test sequences


Usage: xcpclient [OPTIONS]

Options:
      --log-level <LOG_LEVEL>
          Log level (Off=0, Error=1, Warn=2, Info=3, Debug=4, Trace=5)
          [default: 3]

      --verbose <VERBOSE>
          Verbose output Enables additional output when reading ELF files and creating A2L files
          [default: 0]

      --dest-addr <DEST_ADDR>
          XCP server address (IP address or IP:port). If port is omitted, uses --port parameter
          [default: 127.0.0.1:5555]

      --port <PORT>
          XCP server port number (used when --dest-addr doesn't include port)
          [default: 5555]

      --bind-addr <BIND_ADDR>
          Bind address (IP address or IP:port). If port is omitted, system assigns an available port          
          [default: 0.0.0.0]

      --tcp
          Use TCP for XCP communication..

      --udp
          Use UDP for XCP communication

      --offline
          Force offline mode (no network communication), communication parameters are used to create A2L file

      --a2l <A2L file name>
          Specify and overide the name of the A2L file name. If not specified, The A2L file name is read from the XCP server

      --upload-a2l
          Upload A2L file from XCP server. Requires that the XCP server supports GET_ID A2L upload

      --create-a2l
          Build an A2L file template from XCP server information about events and memory segments. Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands. Insert all visible measurement and calibration variables from ELF file if specified with --elf

      --fix-a2l
          Update the given A2L file with XCP server information about events and memory segments. Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands

      --elf <ELF file name>
          Specify the name of an ELF file, create an A2L file from ELF debug information. If connected to a XCP server, events and memory segments will be extracted from the XCP server
   
      --upload-elf
          Upload ELF file from XCP server. Requires that the XCP server supports GET_ID ELF upload

      --elf-unit-limit <ELF_UNIT_LIMIT>
          Parse only compilations units <= n

      --bin <BIN file name>
          Specify the pathname of a binary file (Intel-HEX or XCPlite-BIN) for calibration parameter segment data

      --upload-bin
          Upload all calibration segments working page data and store into a given binary file. Requires that the XCP server supports GET_ID A2L upload

      --download-bin
          Download all calibration segments working page data in a given binary file

      --list-mea <LIST_MEA>
          Lists all specified measurement variables (regex) found in the A2L file

      --mea <MEA>...
          Specify variable names for DAQ measurement (list), may be list of names separated by space or single regular expressions (e.g. ".*")

      --time-ms <TIME_MS>
          Limit measurement duration to n ms

      --time <TIME>
          Limit measurement duration to n s

      --csv <CSV file name>
          Save measurement data to a CSV file. If not specified, data is printed to the console.
          CSV format: time_ns,daq,name,value  (one row per measurement sample).

      --list-cal <LIST_CAL>
          Lists all specified calibration variables (regex) found in the A2L file

      --cal <NAME> <VALUE>
          Set calibration variable to a value (format: "variable_name value")

      --test
          Execute a test sequence on the XCP server

  -h, --help
          Print help (see a summary with '-h')

  -V, --version
          Print version


## Build and Install

```bash
cd tools/xcpclient
cargo install --path .
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

With ELF file given, creates an A2L file from the ELF file and XCP server information about events and memory segments, then measures the specified variable:  

```bash
xcpclient --udp --elf build/hello_xcp --mea "counter" --verbose 2
```


### Upload the A2L file to be used from the target

```bash
xcpclient --dest-addr=192.168.0.206:5555 --tcp --upload-a2l   
```

### Create an A2L file for a target from ELF without on target A2L generation support

Provide the ELF file and create an A2L file from the ELF file and XCP server information about events and memory segments, then save the A2L file as hello_xcp.a2l:
```bash
xcpclient --dest-addr=192.168.0.206:5555 --udp  --create-a2l --elf hello_xcp --a2l hello_xcp.a2l 
```

Upload the ELF file into hello_xcp.elf and create an A2L file from the ELF file and XCP server information about events and memory segments, then save the A2L file as hello_xcp.a2l:
```bash
xcpclient --dest-addr=192.168.0.206:5555 --udp --create-a2l --upload-elf --elf hello_xcp.elf --a2l hello_xcp.a2l 
```

### Detailed A2L Generation Options

See XCPlite no_a2l_demo README.md.  

Create an A2L for an application ELF file with DWARF debug information.

```bash
xcpclient --dest-addr=192.168.0.206 --tcp --elf no_a2l_demo.out  --create-a2l --create-epk-segment --a2l no_a2l_demo.a2l --offline >no_a2l_demo.log
```

### Upload an Intel-HEX file with the current calibration data

```bash
xcpclient --upload-bin test.hex
```


#### Demo

```bash

# A2l from no_a2l_demo.out
cargo r --  --elf no_a2l_demo.out --elf-unit-limit 1000 --log-level 3  --create-a2l --a2l no_a2l_demo.a2l   --offline  


cargo r --  --dest-addr 192.168.0.206 --udp --elf no_a2l_demo.out --elf-unit-limit 1000 --log-level 3  --create-a2l --a2l no_a2l_demo.a2l 

cargo r --  --dest-addr 192.168.0.206  --elf no_a2l_demo.out --elf-unit-limit 1000 --log-level 3  --create-a2l --a2l no_a2l_demo.a2l --list-mea 'counter'

cargo r --  --dest-addr 192.168.0.206  --elf no_a2l_demo.out --elf-unit-limit 1000 --log-level 3  --create-a2l --a2l no_a2l_demo.a2l --mea 'counter'  --time 5 --verbose 2

```