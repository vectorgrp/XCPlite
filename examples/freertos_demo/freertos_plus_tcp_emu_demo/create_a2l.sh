#!/usr/bin/env bash
set -euo pipefail

# Generate an A2L description from the compiled demo's ELF/DWARF information.
# Usage: ./create_a2l.sh <demo-executable> <XCP-server-IP>
# The XCP server does not need to be running.

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 DEMO_EXECUTABLE DEMO_IP_ADDRESS" >&2
    exit 1
fi

# Locate this script's directory and resolve the executable to an absolute
# path, so it remains accessible after changing directories.
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -f "$1" ]]; then
    echo "Demo executable not found: $1" >&2
    exit 1
fi

demo_elf="$(realpath -- "$1")"
demo_address="$2"

# Find XCPlite's xcpclient tool. XCPCLIENT can override its location.
xcpclient_bin="${XCPCLIENT:-xcpclient}"

if ! command -v "$xcpclient_bin" >/dev/null 2>&1; then
    echo "xcpclient is required; see tools/xcpclient/README.md. Set XCPCLIENT to its executable path." >&2
    exit 1
fi

xcpclient_bin="$(command -v "$xcpclient_bin")"
if [[ "$xcpclient_bin" != /* ]]; then
    xcpclient_bin="$(realpath -- "$xcpclient_bin")"
fi

# Generate the A2L in CANape/, which also contains the XCP_104.aml definition.
# --offline reads the executable rather than connecting to the XCP server.
# --elf-unit-filter selects variables from xcp_demo.c.
# fastTask is the default for measurements without a fixed event.
cd -- "$script_dir/CANape"

"$xcpclient_bin" --offline --udp --dest-addr "$demo_address" \
    --elf "$demo_elf" --elf-unit-filter xcp_demo --default-event=fastTask \
    --create-a2l --a2l freertos_demo.a2l

echo "Generated $script_dir/CANape/freertos_demo.a2l"
