# silkit_demo — SIL Kit Pub/Sub Demo with XCP Measurement

This example demonstrates how to combine **SIL Kit** (a virtual bus simulation framework by Vector Informatik) with **XCPlite** to add live measurement and calibration to a SIL Kit simulation participant.

Two participants (from the original **SIL Kit** example) are provided:

| Executable            | SIL Kit role   | XCP server port |
|-----------------------|----------------|-----------------|
| `SilKitDemoPublisher` | Data Publisher | TCP 5555        |
| `SilKitDemoSubscriber`| Data Subscriber| TCP 5556        |

The Publisher produces GPS (latitude/longitude) and temperature data on each simulation step and makes these values observable via XCP.  
The Subscriber receives that data and also exposes the received values via XCP.

Connect **CANape** or another XCP client tool to `localhost:5555` / `localhost:5556` to measure the signals in real time.

---



### SilKit

Build or download a SIL Kit release package from https://github.com/vectorgrp/sil-kit/releases.  
The build tree of the git repository also works.

### xcplite

Build and install xcplite:

```bash
cd /path/to/XCPlite
./build.sh release lib install
# Installs to build/install/ by default
```

---

## Build

```bash
cd examples/silkit_demo
cmake -B build \
      -DSilKit_DIR=/path/to/sil-kit/_install/debug/lib/cmake/SilKit \
      -DCMAKE_PREFIX_PATH=/path/to/XCPlite/build/install
cmake --build build
```

`SilKit_DIR` must point to the directory containing `SilKitConfig.cmake`.  
When building from the git repo, install SilKit first:
```bash
cmake --install /path/to/sil-kit/_build/debug --prefix /path/to/sil-kit/_install/debug
```
Then `SilKit_DIR` = `_install/debug/lib/cmake/SilKit`.

Or using `CMAKE_PREFIX_PATH` for both (if SilKit is installed to a standard prefix):

```bash
cmake -B build \
      -DCMAKE_PREFIX_PATH="/path/to/sil-kit/_install/debug;/path/to/XCPlite/build/install"
cmake --build build
```

---

## Running

Open **four** separate terminals. All commands are relative to the build output directory.

**Terminal 1 — SIL Kit Registry:**
```bash
/path/to/sil-kit/_build/debug/Debug/sil-kit-registry
```

**Terminal 2 — Publisher** (XCP on TCP 5555):
```bash
./build/SilKitDemoPublisher
```

**Terminal 3 — Subscriber** (XCP on TCP 5556):
```bash
./build/SilKitDemoSubscriber
```

**Terminal 4 — System Controller** (starts synchronized simulation):
```bash
/path/to/sil-kit/_build/debug/Debug/sil-kit-system-controller Publisher Subscriber
```

### Autonomous mode (no system controller needed)

```bash
# Terminal 1
/path/to/sil-kit/_build/debug/Debug/sil-kit-registry

# Terminal 2
./build/SilKitDemoPublisher --async --autonomous

# Terminal 3
./build/SilKitDemoSubscriber --async --autonomous
```

---

## XCP Signals

### Publisher (port 5555)

| Signal name        | Type   | Description                  |
|--------------------|--------|------------------------------|
| `_xcp_latitude`    | double | GPS latitude in degrees      |
| `_xcp_longitude`   | double | GPS longitude in degrees     |
| `_xcp_temperature` | double | Temperature in Celsius        |

DAQ event: `PublisherTask` — triggered once per simulation step.

### Subscriber (port 5556)

| Signal name        | Type   | Description                       |
|--------------------|--------|-----------------------------------|
| `_xcp_latitude`    | double | Received GPS latitude in degrees  |
| `_xcp_longitude`   | double | Received GPS longitude in degrees |
| `_xcp_temperature` | double | Received temperature in Celsius   |

DAQ events: `GpsReceived`, `TemperatureReceived` — triggered in the data receive callbacks.

---

## Project structure

```
silkit_demo/
├── CMakeLists.txt          # Independent build configuration
├── README.md               # This file
├── include/
│   ├── ApplicationBase.hpp    # SIL Kit demo lifecycle helper (bundled)
│   ├── CommandlineParser.hpp  # Command-line argument parser (bundled)
│   ├── SignalHandler.hpp      # Signal handling helper (bundled)
│   └── PubSubDemoCommon.hpp   # Shared data types and serialization
└── src/
    ├── PublisherDemo.cpp   # Publisher with XCP instrumentation
    └── SubscriberDemo.cpp  # Subscriber with XCP instrumentation
```
