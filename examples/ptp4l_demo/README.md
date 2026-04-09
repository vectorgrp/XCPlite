# ptp4l Demo

## Overview

This demo shows how to use XCP on Ethernet with PTP (Precision Time Protocol) synchronized DAQ timestamps.

## How to use ptp4l with XCP

1. Set up PTP4L with hardware timestamping support on interface eth0
```bash
sudo ptp4l -4 -H -i eth0 -s --tx_timestamp_timeout 100 -l 5 -m
``` 

2. Set up PHC2SYS to synchronize system clock with PTP hardware clock
```bash
sudo phc2sys -c CLOCK_REALTIME -s eth0 -w  -l 5 -m
```

3. Run the ptp4l_demo with XCP
```bash
sudo ./build/ptp4l_demo 
```

4. (Optional) Use pmc to query PTP clock identities
```bash
 # Get the local clock identity
pmc -u -b 0 'GET DEFAULT_DATA_SET' | grep clockIdentity

# Get grandmaster clock identity
pmc -u -b 0 'GET PARENT_DATA_SET' | grep grandmasterIdentity

# Get current dataset (includes both)
pmc -u -b 0 'GET CURRENT_DATA_SET'

# Get synchronization state
# Port states: SLAVE - Synchronized to master, MASTER - Acting as master, LISTENING - Not synchronized, listening for master
pmc -u -b 0 'GET PORT_DATA_SET' | grep portState

```

Clock UUIDS are hardcoded in the demo source code and may need to be adjusted to match your PTP setup.

