#!/bin/bash

# Test script to create processes for BPF demo testing
# This script creates several child processes to trigger BPF events

echo "Starting process creation test..."
echo "This will create several child processes to demonstrate BPF monitoring"

# Function to create a simple process
create_test_process() {
    local name=$1
    local duration=$2
    echo "Creating test process: $name"
    (sleep $duration) &
    echo "Process $name created with PID: $!"
}

# Create several test processes
for i in {1..5}; do
    create_test_process "test_proc_$i" $((i + 1))
    sleep 0.5
done

echo "Test processes created. Check the BPF demo output for detected events."
echo "Waiting for processes to complete..."
wait
echo "All test processes completed."
