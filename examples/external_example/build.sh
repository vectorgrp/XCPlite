#!/bin/bash

# Build script for external_example
# This script demonstrates the complete workflow for using xcplite as an external library

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo ""
echo "========================================"
echo "  External Example Build Script"
echo "========================================"
echo ""

# Get the repository root (two levels up from this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
EXAMPLE_DIR="${SCRIPT_DIR}"

echo -e "${BLUE}Repository root:${NC} ${REPO_ROOT}"
echo -e "${BLUE}Example directory:${NC} ${EXAMPLE_DIR}"
echo ""

# Step 1: Build xcplite in the main project
echo -e "${GREEN}Step 1: Building xcplite...${NC}"
echo "----------------------------------------"
cd "${REPO_ROOT}"

# Configure with local install prefix
if [ ! -d "build" ]; then
    echo "Configuring xcplite build..."
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
fi

# Build the library
echo "Building xcplite..."
cmake --build build --target xcplite

echo ""

# Step 2: Install xcplite to local staging directory
echo -e "${GREEN}Step 2: Installing xcplite to local staging directory...${NC}"
echo "----------------------------------------"
cmake --install build

INSTALL_DIR="${REPO_ROOT}/build/install"
echo -e "${BLUE}Installation directory:${NC} ${INSTALL_DIR}"
echo ""

# Step 3: Build the external example
echo -e "${GREEN}Step 3: Building external_example...${NC}"
echo "----------------------------------------"
cd "${EXAMPLE_DIR}"

# Clean previous build if requested
if [ "$1" == "clean" ]; then
    echo "Cleaning previous build..."
    rm -rf build
fi

# Configure the external example
echo "Configuring external_example..."
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="${INSTALL_DIR}"

# Build the external example
echo "Building external_example..."
cmake --build build

echo ""

# Step 4: Success message
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Build completed successfully!${NC}"
echo -e "${GREEN}========================================${NC}"

