#!/bin/bash
# Run script for silkit_demo
# Opens 4 Terminal.app windows on macOS:
#   1. sil-kit-registry
#   2. SilKitDemoPublisher   (XCP on TCP 5555)
#   3. SilKitDemoSubscriber  (XCP on TCP 5556)
#   4. sil-kit-system-controller  (starts the simulation)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_BIN="${SCRIPT_DIR}/build"

# SilKit utility binaries – adjust if your build/install location differs
SILKIT_BIN="${SILKIT_BIN:-/Users/Rainer.Zaiser/git/sil-kit/_build/debug/Debug}"

REGISTRY="${SILKIT_BIN}/sil-kit-registry"
SYSCTRL="${SILKIT_BIN}/sil-kit-system-controller"
PUBLISHER="${DEMO_BIN}/SilKitDemoPublisher"
SUBSCRIBER="${DEMO_BIN}/SilKitDemoSubscriber"

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
for bin in "${REGISTRY}" "${SYSCTRL}" "${PUBLISHER}" "${SUBSCRIBER}"; do
    if [ ! -x "${bin}" ]; then
        echo "ERROR: binary not found or not executable: ${bin}"
        echo "Run ./build.sh first."
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# Helper: open a new Terminal.app window and run a command.
# Uses a temp script file to avoid quoting/escaping issues with osascript.
# ---------------------------------------------------------------------------
open_terminal() {
    local title="$1"
    local cmd="$2"
    local tmpscript
    tmpscript="$(mktemp /tmp/silkit_demo_XXXXXX)"
    printf '#!/bin/bash\necho -ne "\\033]0;%s\\007"\n%s\nexec zsh\n' "${title}" "${cmd}" > "${tmpscript}"
    chmod +x "${tmpscript}"
    osascript \
        -e "tell application \"Terminal\"" \
        -e "  activate" \
        -e "  do script \"${tmpscript}\"" \
        -e "end tell"
}

echo "Starting silkit_demo ..."
echo "  Registry        : ${REGISTRY}"
echo "  Publisher       : ${PUBLISHER}  (XCP TCP 5555)"
echo "  Subscriber      : ${SUBSCRIBER}  (XCP TCP 5556)"
echo "  SystemController: ${SYSCTRL}"
echo ""

# ---------------------------------------------------------------------------
# 1. Registry – start first and give it a moment to bind its port
# ---------------------------------------------------------------------------
open_terminal "sil-kit-registry" "\"${REGISTRY}\"; exec zsh"
sleep 1

# ---------------------------------------------------------------------------
# 2. Publisher
# ---------------------------------------------------------------------------
open_terminal "SilKitDemoPublisher (XCP:5555)" "\"${PUBLISHER}\"; exec zsh"

# ---------------------------------------------------------------------------
# 3. Subscriber
# ---------------------------------------------------------------------------
open_terminal "SilKitDemoSubscriber (XCP:5556)" "\"${SUBSCRIBER}\"; exec zsh"

# ---------------------------------------------------------------------------
# 4. System Controller – start last so both participants are already connecting
# ---------------------------------------------------------------------------
sleep 1
open_terminal "sil-kit-system-controller" "\"${SYSCTRL}\" Publisher Subscriber; exec zsh"

echo "All terminals launched."
echo ""
echo "Connect CANape (or another XCP client) to:"
echo "  Publisher  -> XCP on TCP localhost:5555"
echo "  Subscriber -> XCP on TCP localhost:5556"
