# ESP32 FreeRTOS Demo

This example runs XCPlite on an ESP32 board using the Arduino framework and the ESP32 FreeRTOS/lwIP runtime.

## What is shows

The included CANape project and the XCP instrumentaion in main.cpp show:

- Creating a high priority FreeRTOS task with precise cyclic execution timing
- Creating a lower priority FreeRTOS task
- Trigger XCP events and acquire measurment variable in both tasks
- Display cycle time jitter of the tasks in CANape
- Measure local variables
- Create thread-safe calibration variables



# Preconditions

You need:

- VS Code with PlatformIO installed, or PlatformIO Core available as `pio`.
- An ESP32-S3 board compatible with `lilygo-t-display-s3`, or an adapted `platformio.ini`.
- A 2.4 GHz WLAN. ESP32 does not connect to 5 GHz-only networks.
- The ESP32 and the PC running the XCP client or CANape must be on the same reachable network.
- An installed Rust xcpclient application for testing and A2L generation




## What Works

The demo currently:

- Connects the ESP32 to a 2.4 GHz WLAN.
- Scans for the configured SSID and selects the strongest matching BSSID.
- Prints Wi-Fi RSSI, channel, encryption mode, BSSID, disconnect reason, and assigned IP address.
- Starts the XCPlite server after Wi-Fi is connected.
- Binds the XCP UDP server to `0.0.0.0:5555`.
- Uses the FreeRTOS/lwIP socket implementation in `src/platform.c`.
- Uses the 32-bit queue implementation `src/queue32.c`.
- Displays basic status on the T-Display-S3 LCD using LovyanGFX.

During bring-up, we fixed these embedded/ESP32 issues:

- Added the ESP32 PlatformIO build wrapper in `extra_script.py`.
- Added the repository `inc/` and `src/` folders to the include path.
- Enabled `_FREE_RTOS` and `XCPLIB_CFG_OVERRIDE="xcplib_rtos_cfg.h"`.
- Adjusted `tXcpCalSegHeader` padding for 32-bit FreeRTOS targets.
- Made `static_assert` usable from C sources that are compiled by the ESP32 toolchain.
- Fixed an uninitialized `packets_lost` field in `queue32.c`.
- Verified that the XCP receive/transmit FreeRTOS tasks are created and run.


## Configuration

XCP connection options are set in main.cpp
- XCP on Ethernet over UDP
- XCP server port: `5555`

Uses the generic XCPlite FreeRTOS configuration override: `xcplib_rtos_cfg.h` in `src/`.   

Stack size settings, DAQ list size, queue size, maximum number of event and calibration segments have influence on memory consumption and may be tuned to your needs.  


### Wi-Fi Credentials

Do not commit WLAN credentials.

The sketch includes `wlan.h` when `WIFI_SSID` or `WIFI_PASSWORD` are not provided by build flags:

```cpp
#if !defined(WIFI_SSID) || !defined(WIFI_PASSWORD)
#include "wlan.h"
#endif
```

Create this local file:

```text
examples/esp32_freertos_demo/src/wlan.h
```

Example:

```cpp
#pragma once

#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"
```

`wlan.h` is ignored by this example's `.gitignore`.

Alternatively, pass credentials with PlatformIO build flags:

```ini
build_flags =
    -DWIFI_SSID=\"your-ssid\"
    -DWIFI_PASSWORD=\"your-password\"
```

Prefer `wlan.h` for local development so secrets do not enter Git history.

## Build

XCPlite source files are built directly from the repository-level `src/` folder.  


From the repo folder:

```bash
cd examples/esp32_freertos_demo
~/.platformio/penv/bin/pio run
```

If `pio` is in your shell path:

```bash
pio run
```

## Upload

The current serial port is configured in `platformio.ini`:

```ini
upload_port = /dev/cu.usbmodem101
monitor_port = /dev/cu.usbmodem101
```

Adjust it if your board enumerates differently:

```bash
pio device list
```

Upload:

```bash
pio run --target upload
```

If upload has trouble entering the bootloader, hold BOOT while upload starts and release it when PlatformIO prints `Connecting...`.

## Serial Monitor

```bash
pio device monitor
```

Expected output includes:

```text
WiFi connected, IP address: ...
Start XCP on Ethernet server
Listening for XCP commands on UDP 0.0.0.0 port 5555
Start XCP receive thread
Start XCP transmit thread
```

## Network Test

First confirm that the board receives an IP address in the serial log.

Then try:

```bash
ping <esp32-ip-address>
```

If ping does not work:

- Check that PC and ESP32 are on the same WLAN/VLAN.
- Move the board closer to the access point if RSSI is weak.
- Try a 2.4 GHz phone hotspot as a control test.
- Still try the XCP client, because ICMP may be filtered even when UDP works.

The XCP server listens on UDP port `5555`.

## XCP test

Execute a basic XCP connection test:

```bash
xcpclient --udp --dest-addr 192.168.0.146 --help
```

The upload A2L file error message is expected, as the FreeRTOS implementation does not support on-target A2L generation and A2L upload.

Instead, get the ELF file and generate the A2L file:

```bash

Examples:

# Add everything (not recomended):
xcpclient --offline  --elf "<path/to/elf>" --a2l esp32_freertos_demo.a2l  

# Get verbose output with --verbose 1 or 2:
xcpclient --offline  --elf examples/esp32_freertos_demo/.pio/build/lilygo-t-display-s3/firmware.elf --a2l esp32_freertos_demo.a2l  --verbose 2

# Restrict compilation units with --elf-unit-filter: 
xcpclient --offline  --elf ../../examples/esp32_freertos_demo/.pio/build/lilygo-t-display-s3/firmware.elf --a2l esp32_freertos_demo.a2l   --elf-unit-filter main.cpp  --verbose 1  >> esp32_freertos_demo.log
 
```

See the documentation of xcpclient and no_a2l_demo for more infomation on working with offline A2L generation.  

Do a test measurement:

```bash
xcpclient --udp --dest-addr 192.168.0.2146   --a2l esp32_freertos_demo.a2l  --mea counter --verbose 2
 --elf 
```

Try out the CANape project in folder CANape_Project.  




## Adapting To Other ESP32 Hardware

Change the PlatformIO board in `platformio.ini`:

```ini
board = esp32dev
```

or choose the exact board ID from PlatformIO.

For a board without the LilyGo display:

- Remove `lovyan03/LovyanGFX` from `lib_deps`.
- Remove `#include <LovyanGFX.hpp>` and the display initialization/status code from `src/main.cpp`.
- Replace `LED_BUILTIN` if your board uses a different LED pin.

For a different display:

- Keep the Wi-Fi and XCP setup.
- Replace the LovyanGFX pin configuration in `Display`.
- Check the board variant header for pin names such as `LCD_WR`, `LCD_D0`, and `LCD_BL`.

For non-S3 ESP32 boards:

- Check `board` and upload port.
- Confirm enough RAM for the XCP queue and task stacks.
- Keep `_FREE_RTOS` and the `xcplib_rtos_cfg.h` override.

## XCPlite Source Selection

The PlatformIO build uses `extra_script.py` to compile only the source files needed for this embedded FreeRTOS target:

```text
src/xcpappl.c
src/xcplite.c
src/xcpethserver.c
src/xcpethtl.c
src/queue32.c
src/cal.c
src/platform.c
```

The source files remain in the repository-level `src/` folder. They are not copied into this example.

## Current Notes

- TCP is disabled for the FreeRTOS/lwIP path.
- XCP runs over UDP.
- On-target A2L generation and file persistence are disabled by `xcplib_rtos_cfg.h`.
- The internal XCP FreeRTOS task stack size is configured in `src/xcplib_rtos_cfg.h`.
- During debugging, the stack was temporarily set large enough to prove that the transmit task issue was not ordinary stack exhaustion.



## TODO

Prio 1:
- Add a high precision timestamp wall clock
- Remove the bug from the xcpclient ELF reader
- Implement demo measurement events and calibration segment

Other:
- Add a how to tune the different configuration options
- Check if the mutex based queue is acceptable or if we should port one of the lockless queue implementations based on 64Bit atomic head and tail
- Add TCP support
- Do some benchmarking on CPU load, event trigger and calibration RCU latency