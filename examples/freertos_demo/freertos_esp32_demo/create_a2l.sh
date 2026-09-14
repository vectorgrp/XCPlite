#!/bin/bash

# A2L file creator for the freertos_esp32_demo example project
# Creates the A2L file offline from the ELF file built with PlatformIO
# Transport layer UDP, the given IP address and port 5555 are written to the A2L file
# Prerequisites:
# - The firmware has been built with PlatformIO (pio run)
# - The local machine must have xcpclient installed:
#     cd XCPlite
#     ./build.sh rust_tools cargo_install

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" || exit 1
cd "$SCRIPT_DIR" || exit 1

# IP address of the ESP32, written to the A2L file
TARGET_HOST="192.168.0.146"

# ELF file built by PlatformIO
ELFFILE=".pio/build/lilygo-t-display-s3/firmware.elf"

# A2L file
A2LFILE="CANape/freertos_demo.a2l"

# LOG file
LOGFILE="CANape/freertos_demo.log"

# Path to xcpclient tool executable (assuming cargo installed it to ~/.cargo/bin)
XCPCLIENT="xcpclient"

if [ ! -f "$ELFFILE" ]; then
    echo "❌ FAILED: ELF file $ELFFILE not found, build the firmware with PlatformIO first"
    exit 1
fi

# Remove the A2L file of a previous run, so a failed generation can not leave a stale A2L file behind
rm -f "$A2LFILE"
XCPCLIENT_ARGS=(--offline --udp --dest-addr "$TARGET_HOST" --elf "$ELFFILE" --a2l "$A2LFILE" --elf-unit-limit=100 --elf-unit-filter xcp_demo --default-event=fastTask --log-level=3 --verbose=1) 
echo "Command: $XCPCLIENT ${XCPCLIENT_ARGS[*]}"
"$XCPCLIENT" "${XCPCLIENT_ARGS[@]}" >$LOGFILE
if [ $? -ne 0 ] || [ ! -f "$A2LFILE" ]; then
    echo "❌ FAILED: xcpclient could not create the A2L file $A2LFILE"
    exit 1
fi

echo "✅ SUCCESS: Created a new A2L file $A2LFILE"
