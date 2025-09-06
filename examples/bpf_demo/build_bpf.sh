#!/bin/bash

# Build script for BPF demo
# This script should be run on a Linux system

set -e

echo "Building BPF demo..."

# Check if we're on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "Warning: BPF is only supported on Linux. Building without BPF support."
    exit 0
fi

# Check for required tools
command -v clang >/dev/null 2>&1 || { echo "Error: clang not found. Please install clang."; exit 1; }
command -v llvm-strip >/dev/null 2>&1 || { echo "Error: llvm-strip not found. Please install LLVM tools."; exit 1; }

# Build the BPF program
echo "Building BPF program..."
cd "$(dirname "$0")/src"
make clean || true  # Don't fail if clean fails
make all

# Copy to build directory if it exists
if [ -d "../../../build" ]; then
    echo "Installing BPF object to build directory..."
    make install
    echo "BPF program built and installed successfully!"
else
    echo "Build directory not found. You may need to run cmake configure first."
    echo "BPF program built successfully in $(pwd)"
fi

echo "To run the demo on Linux with BPF support:"
echo "1. Ensure you have root privileges (needed for BPF programs)"
echo "2. Run: sudo ./bpf_demo.out"
