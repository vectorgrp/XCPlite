#!/bin/bash

# Build script for external_example
# This script demonstrates the complete workflow for using xcplite as an external library

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color


# Get the repository root (two levels up from this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
EXAMPLE_DIR="${SCRIPT_DIR}"
INSTALL_DIR="${REPO_ROOT}/build/install"

echo ""
echo -e "${BLUE}Repository root:${NC} ${REPO_ROOT}"
echo -e "${BLUE}Installation directory:${NC} ${INSTALL_DIR}"
echo -e "${BLUE}Example directory:${NC} ${EXAMPLE_DIR}"
echo ""


# Clean previous build if requested
if [ "$1" == "clean" ]; then
    echo "Cleaning previous build..."
    rm -rf build
fi



# Step 1: Build xcplite in the main project
echo -e "${GREEN}Step 1: Building xcplite ...${NC}"
echo "----------------------------------------"
cd "${REPO_ROOT}"
./build.sh release install


# Step 2: Build the external example
echo -e "${GREEN}Step 2: Building external_example ...${NC}"
echo "----------------------------------------"
cd "${EXAMPLE_DIR}"
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="${INSTALL_DIR}"
cmake --build build



