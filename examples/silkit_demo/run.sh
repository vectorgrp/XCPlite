#!/bin/bash
# Run script for silkit_demo
# Opens separate terminals for:
#   1. sil-kit-registry
#   2. SilKitXcpServer
#   3. SilKitDemoPublisher
#   4. SilKitDemoSubscriber
#   5. sil-kit-system-controller  (starts the simulation)
#
#
# Usage: ./run.sh [options]
#   -d <us>   Simulation step duration in microseconds 
#   -f        Run as fast as possible (no animation throttle)
#   -r        Run in approximately real time (SIL Kit AnimationFactor=1.0)
#   -h        Show this help

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_BIN="${SCRIPT_DIR}/build"

# SilKit utility binaries – adjust if your build/install location differs
SILKIT_BIN="${SILKIT_BIN:-/Users/Rainer.Zaiser/git/sil-kit/_build/debug/Debug}"

REGISTRY="${SILKIT_BIN}/sil-kit-registry"
SYSCTRL="${SILKIT_BIN}/sil-kit-system-controller"
XCP_SERVER="${DEMO_BIN}/SilKitXcpServer"
PUBLISHER="${DEMO_BIN}/SilKitDemoPublisher"
SUBSCRIBER="${DEMO_BIN}/SilKitDemoSubscriber"

if [[ -n "${WSL_DISTRO_NAME:-}" ]]; then
    PLATFORM="wsl"
    TERMINAL_SHELL="bash"
elif [[ "$(uname -s)" == "Darwin" ]]; then
    PLATFORM="macos"
    TERMINAL_SHELL="zsh"
else
    PLATFORM="linux"
    TERMINAL_SHELL="bash"
fi

# ---------------------------------------------------------------------------
# Parse command line arguments
# ---------------------------------------------------------------------------
STEP_US=""
FAST_FLAG=""
REALTIME=""

usage() {
    echo "Usage: $0 [options]"
    echo "  -d <us>   Simulation step duration in microseconds (default: 10000 = 10ms)"
    echo "  -f        Run as fast as possible (no real-time throttle)"
    echo "  -r        Run in approximately real time (SIL Kit AnimationFactor=1.0)"
    echo "  -h        Show this help"
    exit 0
}

while getopts ":d:frh" opt; do
    case ${opt} in
        d) STEP_US="${OPTARG}" ;;
        f) FAST_FLAG="yes" ;;
        r) REALTIME="yes" ;;
        h) usage ;;
        :) echo "ERROR: Option -${OPTARG} requires an argument."; exit 1 ;;
        \?) echo "ERROR: Unknown option -${OPTARG}"; exit 1 ;;
    esac
done

if [[ -n "${FAST_FLAG}" && -n "${REALTIME}" ]]; then
    echo "ERROR: -f and -r cannot be used together."
    exit 1
fi

# When -r is set, use the static SIL Kit participant config file (AnimationFactor=1.0).
# Note: --config and --log cannot be combined in ApplicationBase, so logging is configured in the file.
SILKIT_CFG=""
if [[ -n "${REALTIME}" ]]; then
    SILKIT_CFG="${SCRIPT_DIR}/silkit_participant_cfg.json"
    if [[ ! -f "${SILKIT_CFG}" ]]; then
        echo "ERROR: config file not found: ${SILKIT_CFG}"
        exit 1
    fi
fi

# Build participant extra args
PARTICIPANT_ARGS=""
[[ -z "${SILKIT_CFG}" ]] && PARTICIPANT_ARGS="-l warn"
[[ -n "${STEP_US}" ]]    && PARTICIPANT_ARGS="${PARTICIPANT_ARGS} --sim-step-duration ${STEP_US}"
[[ -n "${FAST_FLAG}" ]]  && PARTICIPANT_ARGS="${PARTICIPANT_ARGS} --fast"
[[ -n "${SILKIT_CFG}" ]] && PARTICIPANT_ARGS="${PARTICIPANT_ARGS} --config ${SILKIT_CFG}"

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
# Helper: open a new terminal and run a command.
# On macOS this uses Terminal.app, on WSL it prefers Windows Terminal,
# and on Linux it falls back to a local terminal emulator.
# ---------------------------------------------------------------------------
open_terminal() {
    local title="$1"
    local cmd="$2"
    local tmpscript
    tmpscript="$(mktemp /tmp/silkit_demo_XXXXXX)"
    printf '#!/bin/bash\necho -ne "\\033]0;%s\\007"\ncd "%s"\n%s\nexec %s\n' "${title}" "${SCRIPT_DIR}" "${cmd}" "${TERMINAL_SHELL}" > "${tmpscript}"
    chmod +x "${tmpscript}"

    case "${PLATFORM}" in
        macos)
            osascript \
                -e "tell application \"Terminal\"" \
                -e "  activate" \
                -e "  do script \"${tmpscript}\"" \
                -e "end tell"
            ;;
        wsl)
            if command -v wt.exe >/dev/null 2>&1; then
                wt.exe new-tab --title "${title}" wsl.exe -d "${WSL_DISTRO_NAME}" bash "${tmpscript}"
            elif command -v cmd.exe >/dev/null 2>&1; then
                cmd.exe /c start "${title}" wsl.exe -d "${WSL_DISTRO_NAME}" bash "${tmpscript}"
            else
                echo "ERROR: No Windows terminal launcher found (expected wt.exe or cmd.exe in WSL)."
                exit 1
            fi
            ;;
        linux)
            if command -v x-terminal-emulator >/dev/null 2>&1; then
                x-terminal-emulator -T "${title}" -e bash "${tmpscript}" >/dev/null 2>&1 &
            elif command -v gnome-terminal >/dev/null 2>&1; then
                gnome-terminal --title="${title}" -- bash "${tmpscript}" >/dev/null 2>&1 &
            elif command -v konsole >/dev/null 2>&1; then
                konsole --new-tab -p tabtitle="${title}" -e bash "${tmpscript}" >/dev/null 2>&1 &
            elif command -v xterm >/dev/null 2>&1; then
                xterm -T "${title}" -e bash "${tmpscript}" >/dev/null 2>&1 &
            else
                echo "ERROR: No supported terminal emulator found."
                echo "Install one of: x-terminal-emulator, gnome-terminal, konsole, xterm"
                exit 1
            fi
            ;;
    esac
}

echo "Starting silkit_demo ..."
echo "  Registry        : ${REGISTRY}"
echo "  XcpServer       : ${XCP_SERVER}"
echo "  Publisher       : ${PUBLISHER}"
echo "  Subscriber      : ${SUBSCRIBER}"
echo "  SystemController: ${SYSCTRL}"
[[ -n "${STEP_US}" ]] && echo "  Step duration   : ${STEP_US} us" || echo "  Step duration   : 10000 us (default)"
if [[ -n "${FAST_FLAG}" ]]; then
    echo "  Mode            : as fast as possible"
elif [[ -n "${REALTIME}" ]]; then
    echo "  Mode            : real time (AnimationFactor=1.0)"
else
    echo "  Mode            : slow throttle (2 steps/s, default)"
fi
echo ""

# ---------------------------------------------------------------------------
# 1. Registry – start first and give it a moment to bind its port
# ---------------------------------------------------------------------------
open_terminal "sil-kit-registry" "\"${REGISTRY}\""
sleep 1

# ---------------------------------------------------------------------------
# 2. XCP Server – start before the system controller
# ---------------------------------------------------------------------------
open_terminal "SilKitXcpServer" "\"${XCP_SERVER}\"${PARTICIPANT_ARGS:+ ${PARTICIPANT_ARGS}}"

# ---------------------------------------------------------------------------
# 3. Publisher
# ---------------------------------------------------------------------------
open_terminal "SilKitDemoPublisher" "\"${PUBLISHER}\"${PARTICIPANT_ARGS:+ ${PARTICIPANT_ARGS}}"

# ---------------------------------------------------------------------------
# 4. Subscriber
# ---------------------------------------------------------------------------
open_terminal "SilKitDemoSubscriber" "\"${SUBSCRIBER}\"${PARTICIPANT_ARGS:+ ${PARTICIPANT_ARGS}}"


# ---------------------------------------------------------------------------
# 5. System Controller – start last so all participants are already connecting
# ---------------------------------------------------------------------------
sleep 1
open_terminal "sil-kit-system-controller" "\"${SYSCTRL}\" XcpServer Publisher Subscriber"

echo "All terminals launched."
