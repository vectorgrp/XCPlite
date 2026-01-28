#!/bin/bash
# Script to start two PTP masters and a phc2sys process, wait for user input, then clean up
# Exits cleanly on error or keyboard interrupt

set -e

# Start PTP masters as background processes
sudo ptp4l -H -i enp2s0f0 -p /dev/ptp3 -m --tx_timestamp_timeout=100 --domainNumber=2 -l 5 --verbose=1 &
PTP1_PID=$!
sudo ptp4l -H -i enp5s0 -p /dev/ptp0 -m --tx_timestamp_timeout=100 --domainNumber=2 -l 5 --verbose=1 &
PTP2_PID=$!
sudo phc2sys -s /dev/ptp0 -c enp2s0f0 -m -l 7 -O 0 &
PHC2SYS_PID=$!

# Function to kill all started processes
cleanup() {
    echo "Cleaning up..."
    sudo kill $PTP1_PID $PTP2_PID $PHC2SYS_PID 2>/dev/null || true
    wait $PTP1_PID $PTP2_PID $PHC2SYS_PID 2>/dev/null || true
    echo "All PTP processes killed."
}

# Trap signals and errors
trap cleanup EXIT INT TERM

echo "PTP masters and phc2sys started. Press Enter to stop and clean up."
read -r _

exit 0
