# PTP Demo

## PTP Observer

This demo showcases a PTP (Precision Time Protocol, IEEE 1588) observer instrumented with XCP.  
The observer captures PTP SYNC and FOLLOW_UP messages and calculates overall drift and jitter.  
Running on a Linux system with good hardware time stamping support, the observer can measure the quality of the PTP master.  

The implementation of PTP is very basic and assumes there is only one PTP master and one clock domain on the network. The filtering and clock servo algorithms are also simplistic and need significant time to stabilize to obtain a reliable estimation of master clock jitter.  

The demo must be run with root privileges to access hardware time stamping features and the PTP ports.

```bash
sudo ./build/ptp_demo.out

SYNC (seqId=7266, timestamp= 2912265698 from 172.31.31.6 - 00:00:00:00:00:00:00:00
FOLLOW_UP (seqId=7266, timestamp= 0 from 172.31.31.6 - 00:00:00:00:00:00:00:00
syncUpdate:
  t1 (SYNC tx) = 15.12.2025 19:38:58 +986476716ns (1765827538986476716)
  correction     = 368ns
  t2 (SYNC rx)  = 15.12.2025 19:38:21 +165487586ns (1765827501165487586)
  master_drift     = -27650ns/s
  cycle_time          = 999999040ns
  master_offset_raw   = 37820989130ns
  master_offset_norm  = 40863820ns
  master_offset_comp  = -40863830ns
  master_offset       = -13ns
  master_jitter       = -10ns
  master_jitter_rms   = 6.9282ns

```


## Hardware Requirements

Check ethernet interface supports hardware time stamping:
```bash
ip link show # Find your ethernet interface name, e.g., eth0
sudo ethtool -T eth0  # Replace eth0 with your interface name
```

Check for PTP hardware clock devices:
```bash
ls -l /dev/ptp*
```

Check kernel support:
```bash
cat /boot/config-$(uname -r) | grep -i timestamp
```



## CANape Screenshot

![CANape Screenshot](CANape.png)


