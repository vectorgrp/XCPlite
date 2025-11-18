#!/bin/bash

# BPF Demo Test Script for Raspberry Pi
# This script automates the complete test workflow for the BPF demo

set -e

TARGET_HOST="rainer@192.168.0.206"
PROJECT_DIR="~/XCPlite-RainerZ"

echo "=== BPF Demo Test on Raspberry Pi ==="
echo "Target: $TARGET_HOST"
echo "Project: $PROJECT_DIR"
echo

# Function to run command on Pi
run_on_pi() {
    echo "Running on Pi: $1"
    ssh "$TARGET_HOST" "$1"
}

# Function to run command on Pi with pseudo-tty (for interactive commands)
run_on_pi_interactive() {
    echo "Running on Pi (interactive): $1"
    ssh -t "$TARGET_HOST" "$1"
}

echo "Step 1: Syncing project to Pi..."
rsync -avz --delete \
    --exclude=build/ \
    --exclude=.git/ \
    --exclude="*.o" \
    --exclude="*.out" \
    --exclude="*.a" \
    ./ "$TARGET_HOST:$PROJECT_DIR/"

echo
echo "Step 2: Configuring CMake on Pi..."
run_on_pi "cd $PROJECT_DIR && cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug"

echo
echo "Step 3: Building BPF program on Pi..."
run_on_pi "cd $PROJECT_DIR/examples/bpf_demo && ./build_bpf.sh"

echo
echo "Step 4: Building bpf_demo application on Pi..."
run_on_pi "cd $PROJECT_DIR && cmake --build build --target bpf_demo"

echo
echo "Step 5: Checking BPF demo binary..."
run_on_pi "cd $PROJECT_DIR && ls -la build/bpf_demo.out"

echo
echo "=== Build completed successfully! ==="
echo
echo "To run the BPF demo manually:"
echo "  ssh -t $TARGET_HOST 'cd $PROJECT_DIR && sudo ./build/bpf_demo.out'"
echo
echo "To test process creation:"
echo "  ssh $TARGET_HOST 'cd $PROJECT_DIR/examples/bpf_demo && ./test_processes.sh'"
echo
echo "Or run the automated test:"
read -p "Do you want to run the BPF demo now? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo
    echo "Starting BPF demo on Pi..."
    echo "Press Ctrl+C to stop the demo"
    echo "---"
    run_on_pi_interactive "cd $PROJECT_DIR && sudo ./build/bpf_demo.out"
else
    echo "Manual test skipped. Use the commands above to run manually."
fi

echo
echo "=== Test completed ==="
