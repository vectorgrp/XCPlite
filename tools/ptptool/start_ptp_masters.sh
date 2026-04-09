#!/bin/bash
# Script to start two PTP masters and a phc2sys process, wait for user input, then clean up
# Exits cleanly on error or keyboard interrupt

set -e

# Start PTP masters as background processes

#-----------------------------------------
# Intel X520/X540 dual-port 10GbE NICs

# 10G1: 192.168.2.10, enp2s0f1, /dev/ptp4, Domain: 1, 68b983.fffe.014797
sudo ptp4l -H -i enp2s0f1 -p /dev/ptp4 3m --tx_timestamp_timeout=100 --domainNumber=1 -l 5 --verbose=1 &
# sudo ../../build/ptptool -m -i enp2s0f1 --domain 1 -l 4 &

# 10G2: 192.168.1.10, enp2s0f0, /dev/ptp3, Domain: 1, 68b983.fffe.014798
# sudo ptp4l -H -i enp2s0f0 -p /dev/ptp3 -m --tx_timestamp_timeout=100 --domainNumber=2 -l 5 --verbose=1 &

PTP1_PID=$!

#-----------------------------------------
## Intel i210/i350 1GbE NICs

# 1G1: 192.168.3.10, enp4s0, /dev/ptp0, Domain: 3, 68b983.fffe.014799
sudo ptp4l -H -i enp5s0 -p /dev/ptp0 -m --tx_timestamp_timeout=100 --domainNumber=4 -l 5 --verbose=1 &
# sudo ../../build/ptptool -m -i enp5s0 --domain 4 -l 4 &

# No such device error !!!!!!!!!!!!
# 1G2: 192.168.4.10, enp5s0, /dev/ptp1, Domain: 4,
# sudo ptp4l -H -i enp4s0 -p /dev/ptp1 -m --tx_timestamp_timeout=100 --domainNumber=3 -l 5 --verbose=1 &

PTP2_PID=$!

#-----------------------------------------

# Start phc2sys to synchronize  enp2s0f1 with enp5s0 (/dev/ptp0)
sudo phc2sys -s /dev/ptp0 -c enp2s0f1 -m -l 7 -O 0 &

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
