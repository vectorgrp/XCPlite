#!/bin/bash
# Run script for silkit_demo
# Opens 4 Terminal.app windows on macOS:
#   1. sil-kit-registry
#   2. SilKitDemoPublisher  
#   3. SilKitDemoSubscriber 
#   4. sil-kit-system-controller  (starts the simulation)
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
PARTICIPANT_ARGS="-l warn"
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
# Helper: open a new Terminal.app window and run a command.
# Uses a temp script file to avoid quoting/escaping issues with osascript.
# ---------------------------------------------------------------------------
open_terminal() {
    local title="$1"
    local cmd="$2"
    local tmpscript
    tmpscript="$(mktemp /tmp/silkit_demo_XXXXXX)"
    printf '#!/bin/bash\necho -ne "\\033]0;%s\\007"\ncd "%s"\n%s\nexec zsh\n' "${title}" "${SCRIPT_DIR}" "${cmd}" > "${tmpscript}"
    chmod +x "${tmpscript}"
    osascript \
        -e "tell application \"Terminal\"" \
        -e "  activate" \
        -e "  do script \"${tmpscript}\"" \
        -e "end tell"
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
open_terminal "sil-kit-registry" "\"${REGISTRY}\"; exec zsh"
sleep 1

# ---------------------------------------------------------------------------
# 2. XCP Server – start before the system controller
# ---------------------------------------------------------------------------
open_terminal "SilKitXcpServer" "\"${XCP_SERVER}\"${PARTICIPANT_ARGS:+ ${PARTICIPANT_ARGS}}; exec zsh"

# ---------------------------------------------------------------------------
# 3. Publisher
# ---------------------------------------------------------------------------
open_terminal "SilKitDemoPublisher" "\"${PUBLISHER}\"${PARTICIPANT_ARGS:+ ${PARTICIPANT_ARGS}}; exec zsh"

# ---------------------------------------------------------------------------
# 4. Subscriber
# ---------------------------------------------------------------------------
open_terminal "SilKitDemoSubscriber" "\"${SUBSCRIBER}\"${PARTICIPANT_ARGS:+ ${PARTICIPANT_ARGS}}; exec zsh"


# ---------------------------------------------------------------------------
# 5. System Controller – start last so all participants are already connecting
# ---------------------------------------------------------------------------
sleep 1
open_terminal "sil-kit-system-controller" "\"${SYSCTRL}\" XcpServer Publisher Subscriber; exec zsh"

echo "All terminals launched."
