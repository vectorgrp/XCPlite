#!/bin/bash
# Build script for XCPlite silkit_demo
#
#   ./build.sh [debug|release|relwithdebinfo] [local] [clean]
#
#   debug | release | relwithdebinfo   CMake build type (default: debug)
#   local                              Build against this repository's working tree instead of cloning xcplite
#   clean                              Remove the build directory first
#
# Arguments can be given in any order.
#
# SIL Kit and xcplite (shm configuration) are fetched and built from source via CMake FetchContent,
# see CMakeLists.txt. Nothing has to be installed. The first build clones and compiles SIL Kit,
# which takes a few minutes.
# To build against a local sil-kit checkout (with submodules), set SILKIT_SOURCE_DIR:
#   SILKIT_SOURCE_DIR=../../../sil-kit ./build.sh

set -e

GREEN='\033[0;32m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

usage() {
    sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

BUILD_TYPE=Debug
CLEAN=false
CMAKE_ARGS=()

for arg in "$@"; do
    case "$arg" in
        debug)          BUILD_TYPE=Debug ;;
        release)        BUILD_TYPE=Release ;;
        relwithdebinfo) BUILD_TYPE=RelWithDebInfo ;;
        local)          CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_XCPLITE=${REPO_ROOT}") ;;
        clean)          CLEAN=true ;;
        -h|--help)      usage; exit 0 ;;
        *)              echo "Unknown argument: $arg"; echo ""; usage; exit 1 ;;
    esac
done

if [[ -n "${SILKIT_SOURCE_DIR:-}" ]]; then
    CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_SILKIT=$(cd "${SILKIT_SOURCE_DIR}" && pwd)")
fi

if [[ "$CLEAN" == true ]]; then
    echo "Removing ${SCRIPT_DIR}/build"
    rm -rf "${SCRIPT_DIR}/build"
fi

cd "${SCRIPT_DIR}"
cmake -B build -S . -DCMAKE_BUILD_TYPE=${BUILD_TYPE} "${CMAKE_ARGS[@]}"
# Limit the parallel jobs to the number of cores. A plain --parallel is an unlimited 'make -j',
# which exhausts the memory of small targets (Raspberry Pi) when compiling SIL Kit.
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cmake --build build --parallel "${JOBS}"

echo ""
echo -e "${GREEN}Build successful.${NC}"
echo "Demo binaries   : ${SCRIPT_DIR}/build/"
echo "  SilKitDemoPublisher, SilKitDemoSubscriber, SilKitXcpServer, shmtool"
echo "SIL Kit binaries: ${SCRIPT_DIR}/build/${BUILD_TYPE}/"
echo "  sil-kit-registry, sil-kit-system-controller, sil-kit-monitor"
echo ""
echo "Run the demo with ./run.sh or manually:"
echo "./build/${BUILD_TYPE}/sil-kit-registry"
echo "./build/SilKitDemoPublisher --sim-step-duration 10000 --fast"
echo "./build/SilKitDemoSubscriber --sim-step-duration 10000 --fast"
echo "./build/${BUILD_TYPE}/sil-kit-system-controller Publisher Subscriber"
