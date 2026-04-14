#!/bin/bash
# Build script for XCPlite silkit_demo
# Builds XCPlite (if needed), installs SilKit (if needed), then builds the demo.

set -e

GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
XCPLITE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
XCPLITE_INSTALL="${XCPLITE_ROOT}/build/install"

# ---------------------------------------------------------------------------
# SilKit paths – adjust to your environment
# ---------------------------------------------------------------------------
SILKIT_BUILD_DIR="${SILKIT_BUILD_DIR:-/Users/Rainer.Zaiser/git/sil-kit/_build/debug}"
SILKIT_INSTALL_DIR="${SILKIT_INSTALL_DIR:-/Users/Rainer.Zaiser/git/sil-kit/_install/debug}"
SILKIT_CMAKE_DIR="${SILKIT_INSTALL_DIR}/lib/cmake/SilKit"

echo ""
echo -e "${BLUE}xcplite root   :${NC} ${XCPLITE_ROOT}"
echo -e "${BLUE}xcplite install:${NC} ${XCPLITE_INSTALL}"
echo -e "${BLUE}SilKit build   :${NC} ${SILKIT_BUILD_DIR}"
echo -e "${BLUE}SilKit install :${NC} ${SILKIT_INSTALL_DIR}"
echo ""

# Clean
if [ "$1" == "clean" ]; then
    echo "Cleaning previous build..."
    rm -rf "${SCRIPT_DIR}/build"
fi

# ---------------------------------------------------------------------------
# Step 1: Build & install xcplite
# ---------------------------------------------------------------------------
echo -e "${GREEN}Step 1: Building and installing xcplite ...${NC}"
echo "------------------------------------------------------------"
cd "${XCPLITE_ROOT}"
./build.sh release install

# ---------------------------------------------------------------------------
# Step 2: Install SilKit from its build tree (idempotent)
# Only the 'bin' (shared library) and 'dev' (headers + cmake config) components
# are installed. This avoids errors from utility binaries (sil-kit-monitor etc.)
# that may not have been built.
# ---------------------------------------------------------------------------
echo -e "${GREEN}Step 2: Installing SilKit to ${SILKIT_INSTALL_DIR} ...${NC}"
echo "------------------------------------------------------------"
cmake --install "${SILKIT_BUILD_DIR}" --prefix "${SILKIT_INSTALL_DIR}" --component bin
cmake --install "${SILKIT_BUILD_DIR}" --prefix "${SILKIT_INSTALL_DIR}" --component dev

if [ ! -f "${SILKIT_CMAKE_DIR}/SilKitConfig.cmake" ]; then
    echo "ERROR: SilKitConfig.cmake not found at ${SILKIT_CMAKE_DIR}"
    echo "Please build SilKit first:  cmake --preset debug  &&  cmake --build _build/debug"
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 3: Configure and build the silkit_demo
# ---------------------------------------------------------------------------
echo -e "${GREEN}Step 3: Building silkit_demo ...${NC}"
echo "------------------------------------------------------------"
cd "${SCRIPT_DIR}"
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSilKit_DIR="${SILKIT_CMAKE_DIR}" \
    -DCMAKE_PREFIX_PATH="${XCPLITE_INSTALL}"
cmake --build build

echo ""
echo -e "${GREEN}Build successful.${NC}"
echo "Binaries: ${SCRIPT_DIR}/build/"
echo "  SilKitDemoPublisher"
echo "  SilKitDemoSubscriber"
echo ""
echo "Run the demo with:  ./run.sh"
