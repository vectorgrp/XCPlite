# CANape Project

This directory contains an optional CANape project template for the XCPlite
FreeRTOS-Plus-TCP demo.

**CANape** is a measurement and calibration tool that communicates with an XCP
server. It is not required to run the demo; XCPlite's `xcpclient` can also be used.

The template reuses the measurement and calibration setup from the existing
`freertos_emu_demo`. The device name remains `freertos_demo` to match the shared
application.

## Project Files

| File | Description |
| --- | --- |
| `CANape.ini` | CANape project and device settings |
| `CanapeCmd.ini` | CANape command configuration |
| `freertos_demo.cna` | CANape measurement configuration |
| `XCP_104.aml` | Standard XCP protocol description used by the A2L |

The measurement description (`freertos_demo.a2l`) is not included and must be
generated for your build.

## Generate the A2L

An **A2L file** describes the measurements, calibration parameters, memory
addresses, and DAQ events available through XCP.

Generate it from the same executable you intend to run, because addresses and
event IDs may change between builds. The executable must contain DWARF debug
information.

The generator requires `xcpclient` on PATH, or `XCPCLIENT` set to its executable
path. See the [demo README](../README.md#2-generate-the-a2l-file) for setup instructions.

From this directory:

```sh
../create_a2l.sh ../build/freertos_plus_tcp_emu_demo 192.168.0.208
```

Replace the executable path and IP address with your own.

The script uses `xcpclient` to generate `freertos_demo.a2l` in this directory.
The server does not need to be running during generation.

## Connect with CANape

Copy this directory, including the generated A2L and `XCP_104.aml`, to the CANape
computer.

1. Open `CANape.ini` and select `freertos_demo.cna`.
2. Load the generated `freertos_demo.a2l` in the `freertos_demo` device settings.
3. Configure the XCP-over-UDP destination using your demo's IP address and port
   **5555**.
4. Connect to the running demo and verify the DAQ event assignments (`fastTask`,
   `slowTask`, `foo`).

The template uses `192.168.0.208` as an example address. Your CANape computer must
be able to reach the actual demo IP.

**Note:** Internal FreeRTOS-Plus-TCP loopback mode cannot be accessed by external XCP
clients.
