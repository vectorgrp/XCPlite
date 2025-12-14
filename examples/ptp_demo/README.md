# PTP Demo

## Overview


## Hardware Requirements

Check ethernet interface supports hardware timestamping:
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


