# silkit_demo — SIL Kit Pub/Sub Demo with XCP Measurement and Calibration

This example demonstrates how to combine **SIL Kit** (SIL Kit – Open-Source Library for Connecting Software-in-the-Loop Environments) with **XCPlite** to add live measurement and calibration to one or more SIL Kit simulation participants.  

In the example, two participants (derived from the original **SIL Kit** Publisher/Subscriber example) are provided.  
The Publisher produces some test data on each simulation step and makes the internal values observable via XCP.  
The Subscriber receives that data and also exposes the received values via XCP.  
There are some tunable parameter to demonstrate thread-safe calibration and parameter persistence.  

Compared to other XCP integration examples, this demo focuses on 2 special aspects:

- Simulation time vs. real time: The demo can be configured to use either the virtual simulation time (which advances non-realtime in fixed steps) or real wall-clock time for XCP event timestamps. 
- Multi application support: The demo can be run with either separate XCP servers for participants, or a single XCP server in multi-application shared memory mode used for both participants (default).

The XCP server can be realized as a separate participant (in `XcpServer.cpp`) or integrated into the participants.  
The participant started first automatically becomes the XCP server.

For distributed simulations on multiple machines, there has to be one XCP server per machine.  


## Single or Multi Application Mode

Default option settings are non real-time with a single separate XCP server participant and XCP in multi application mode.    
For the other options you need to change the source code (remove propagating the simulated time and modify the XCP mode given to XcpServerInit as appropriate).
Using multi application mode requires to compile the XCP library in shared memory mode (#define `OPTION_SHM_MODE` in xcplib_cfg.h).  
Note that the multi application mode is still in experimental state yet and only supported on POSIX-compliant platforms (Linux, macOS, QNX), not on Windows.


### Option 1 - Separate XCP servers for each participant

Separate XCP servers for each participant:

| Executable            | XCP server port |
|-----------------------|-----------------|
| `SilKitDemoPublisher` | TCP 5555        |
| `SilKitDemoSubscriber`| TCP 5556        |

This option requires to setup 2 different XCP client devices in CANape, one for each participant.  

### Option 2 - Single XCP server in multi-application shared memory mode

A single XCP server in multi-application shared memory mode for all participants on the same machine.  
The XCP server port is then fixed to standard XCP port 5555.  

This option needs to configure only one XCP client in the XCP tool (e.g. CANape device), which can access signals and parameters from all participants.  
The A2L files for the participants are automatically merged and all symbols get prefixed with the participant name (e.g., `Publisher._temperature` and `Subscriber._temperature`) to avoid name clashes.

Without a dedicated XCP server participant, the user participant that starts first becomes the XCP server, the other participants automatically detect the running XCP server and connect to it in shared memory mode. 
There are no performance penalties compared to the single-application mode, because the shared memory mode uses the same lock-less mechanisms for data acquisition and calibration. The only side effect is, that the applications share a few cache lines for the data acquisition queue and the calibration RCU.  

See the documentation of XCPlite SHM mode (in docs/SHM.md) for more details on the multi-application shared memory mode.  

---

## Simulated Time

The XCP events can be timestamped with either the virtual simulation time (which advances non real-time in fixed steps) or real wall-clock time.  

To use non real time targets with CANape, time synchronisation must be configured to 'Simulation' (Settings / Project Settings / Miscellaneous / Synchronization / Simulation). Then configure the XCP device as time master (Device Configuration / Right click on device / Time master).  




## Building

Build or download a SIL Kit release package from https://github.com/vectorgrp/sil-kit/releases.  
The build tree of the git repository also works.

### Build XCPlite

Build and install xcplite:

```bash
cd /path/to/XCPlite
./build.sh release lib install
# Installs to build/install/ by default
```

---

## Build sil-kit and the demo

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

There is a build script `build.sh` that does the above with some default paths — edit it if needed.
---

## Running

### Quick start (macOS)

On macOS, `run.sh` opens all required Terminal.app windows automatically.  
By default SIL Kit slows the simulation to approximately 2 steps per second so that log output stays readable. Pass `-f` when you want maximum throughput or `-r` for real-time execution.

### Manual start

Open separate terminals. All commands are relative to the silkit_demo directory.

**Terminal 0 — SIL Kit Registry:**

```bash
/path/to/sil-kit/_build/debug/Debug/sil-kit-registry
# Example: /Users/Rainer.Zaiser/git/sil-kit/_build/debug/Debug/sil-kit-registry
```

**Terminal 1 — XCP Server participant** 

Start the XCP server participant. Default is (XCP on UDP, port 5555):

```bash
./build/SilKitXcpServer
```


**Terminal 2 — Demo Publisher participant**:

```bash
./build/SilKitDemoPublisher
# fast
# ./build/SilKitDemoPublisher --sim-step-duration 10000 --fast
# realtime
# ./build/SilKitDemoPublisher --sim-step-duration 10000 --config silkit_participant_cfg.json
```

**Terminal 3 — Demo Subscriber participant**:

```bash
./build/SilKitDemoSubscriber
#fast
# ./build/SilKitDemoSubscriber --sim-step-duration 10000 --fast
# realtime
# ./build/SilKitDemoSubscriber --sim-step-duration 10000 --config silkit_participant_cfg.json
```

**Terminal 4 — System Controller** 

Starts synchronized simulation:  

```bash
/path/to/sil-kit/_build/debug/Debug/sil-kit-system-controller XcpServer Publisher Subscriber
# Example: /Users/Rainer.Zaiser/git/sil-kit/_build/debug/Debug/sil-kit-system-controller XcpServer Publisher Subscriber
```

The `--sim-step-duration <us>` and `--fast` flags can be passed directly to both participant binaries when starting manually.


**Terminal 5 — Test**

Check status of the XCP server:

```bash
../../build/shmtool status -v

/xcpdata mmap found, size = 32768 bytes
================================================================================
  magic              : 0x5843504C4954455F  (valid XCPLITE_)
  version            : 1.0.0
  declared size      : 31208 bytes  (this build: 31208)
  leader pid         : 84680
  app count          : 3 / 8
  A2L finalized      : yes
  A2L finalize req'd : no
--------------------------------------------------------------------------------
  App 0:  XcpServer [server] epk=V1.0  pid=84680  [leader]
          a2l_name=XcpServer_include_V1.0.a2l  finalized=yes  alive_counter=0
--------------------------------------------------------------------------------
  App 1:  Publisher  epk=V1.7  pid=84745  
          a2l_name=Publisher_include_V1.7.a2l  finalized=yes  alive_counter=1
--------------------------------------------------------------------------------
  App 2:  Subscriber  epk=V1.7  pid=84763  
          a2l_name=Subscriber_include_V1.7.a2l  finalized=yes  alive_counter=1
--------------------------------------------------------------------------------
SHM Header:
  magic=0x5843504C4954455F, version=010000, size=31208
  leader_pid=84680, app_count=3, a2l_finalized=1, a2l_finalize_requested=0
  ecu_epk='a9eb2df2261d400e'
Apps (3):
  [0] 'XcpServer', epk='V1.0', alive pid=84680 leader server, init_mode=8A, a2l_name='XcpServer_include_V1.0.a2l', alive_counter=0
  [1] 'Publisher', epk='V1.7', alive pid=84745, init_mode=82, a2l_name='Publisher_include_V1.7.a2l', alive_counter=1
  [2] 'Subscriber', epk='V1.7', alive pid=84763, init_mode=82, a2l_name='Subscriber_include_V1.7.a2l', alive_counter=1
Events (4):
  [0] 'DoWorkSync', cycle_ns=0, index=0, app_id=2, daq_first=65535, flags=0x00
  [1] 'Gps', cycle_ns=0, index=0, app_id=2, daq_first=65535, flags=0x00
  [2] 'Temp', cycle_ns=0, index=0, app_id=2, daq_first=65535, flags=0x00
  [3] 'DoWorkSync', cycle_ns=0, index=0, app_id=1, daq_first=65535, flags=0x00
CalSegs (2):
  [0] 'epk', size=32, app_id=0, seg_num=0
  [1] 'kParameters', size=24, app_id=1, seg_num=1

```


Use the test XCP client (see tools/xcpclient)to verify the XCP server is running, A2L file can be uploaded and parsed, and symbols are visible:

```bash

xcpclient --upload-a2l --udp  --list-mea .   --list-cal .

Calibration variables:
 Publisher.kParameters.counter_max 0:80010000  = 1000
 Publisher.kParameters.delay_us 0:80010004  = 1000
 Publisher.kParameters.signal_amplitude 0:80010008 = 1
 Publisher.kParameters.use_simulated_time 0:80010010  = 1


Measurement variables:
 Publisher._counter 3:0x00C00000 event 3 2 byte unsigned
 Publisher._temperature 4:0x00C00000 event 3 8 byte float
 Subscriber._counter 3:0x00000120 event 0 2 byte unsigned
 Subscriber._temperature 3:0x00800140 event 2 8 byte float
 Publisher._gps_data.latitude 5:0x00C00000 event 3 8 byte float
 Publisher._gps_data.longitude 5:0x00C00008 event 3 8 byte float
 Publisher._gps_data.signal 5:0x00C00010 event 3 8 byte float
 Subscriber._gps_data.latitude 3:0x00400128 event 1 8 byte float
 Subscriber._gps_data.longitude 3:0x00400130 event 1 8 byte float
 Subscriber._gps_data.signal 3:0x00400138 event 1 8 byte float
```


Use CANape:

The are 2 different CANape projects included:

```
silkit_demo/
├── CANape      # CANape project with 2 separate XCP devices for Publisher and Subscriber
├── CANape_SHM  # CANape project with a single XCP device for multi application shared memory mode
```

---

## SIL Kit Architecture and API

See the sil-kit repository and documentation for more details on the architecture and API of SIL Kit:
https://github.com/vectorgrp/sil-kit

Here is the link to the original SIL Kit Publisher/Subscriber example:
https://github.com/vectorgrp/sil-kit/tree/main/Demos/communication/PubSub



### Core Concepts

SIL Kit is a **simulation integration middleware** — it connects multiple processes (called *participants*) in a shared virtual bus. Each participant has a unique name and connects to a central **Registry** process (`sil-kit-registry`, default port 8500).

The registry is a **connection broker**, not a message broker. It manages participant discovery and coordinates the establishment of direct **peer-to-peer TCP connections** between participants. Once participants are connected to each other, all message exchange happens directly between them — the registry is no longer involved in message routing.  

The two participants `Publisher` and `Subscriber` exchange data through a **Pub/Sub** service — a topic-based, typed byte-stream channel (similar in concept to ROS2 topics or MQTT).

---

### The `ApplicationBase` Class

`ApplicationBase` (in `include/ApplicationBase.hpp`) wraps the entire SIL Kit lifecycle into a simple template that is inherited by both `Publisher` and `Subscriber`.

```
Run()
 ├── SetupParticipant()       → SilKit::CreateParticipant(...)
 ├── CreateControllers()      → [your override]
 ├── SetupLifecycle()         → registers CommunicationReadyHandler → InitControllers()
 ├── SetupSync() or           → registers SimulationStepHandler → DoWorkSync()
 │   SetupAsync()             → starts worker thread → DoWorkAsync()
 ├── Launch()                 → starts lifecycle state machine
 └── WaitUntilDone()          → blocks until shutdown
```

The four virtual methods you override in sub-classes are the only integration points you need to implement.

---

### The Four Override Methods

#### `CreateControllers()`
Called **before** the lifecycle starts and before any participant connects. This is where you create SIL Kit *controllers* (publishers, subscribers, etc.) and other resources like the XCP server.

In `PublisherDemo.cpp`:
```cpp
_gpsPublisher = GetParticipant()->CreateDataPublisher("GpsPublisher", dataSpecGps, 0);
XcpEthServerInit(...);  // XCP server set up here
DaqCreateEvent(PublisherTask);
```

In `SubscriberDemo.cpp`, subscriber callbacks are registered here as lambdas. When a message arrives, those lambdas run and trigger a DAQ event — this is how XCP captures data on the **subscriber side** (event-driven, not time-driven).

#### `InitControllers()`
Called inside the **`CommunicationReadyHandler`** — all participants are now connected and in a ready state, but simulation has not started yet. This is the right place to send *initial* messages if needed, because other participants are guaranteed to be listening.

Both `Publisher` and `Subscriber` leave this empty here — no initialization exchange is required before the simulation loop starts.

#### `DoWorkSync(std::chrono::nanoseconds now)`
Called on **every simulation step** when running in **time-synchronized mode** (the default, without `--async`). All participants advance in lockstep — no participant moves to the next step until all have finished the current one.

`ApplicationBase::SetupSync()` registers this under the hood:
```cpp
_timeSyncService->SetSimulationStepHandler([this](nanoseconds now, ...) {
    DoWorkSync(now);
}, _arguments.duration);  // default step = 1ms
```

The `now` parameter is the **virtual simulation time**, not wall-clock time. It advances by `duration` each step. A `sil-kit-system-controller` process must be running to coordinate all participants.

In `PublisherDemo.cpp`, `DoWorkSync` publishes GPS and temperature data every step and triggers the DAQ event, so XCP measurement data is captured at the simulation rate.

#### `DoWorkAsync()`
Called in a **loop in a separate worker thread** when you pass `--async`. There is no coordinated simulation time — the participant runs freely at wall-clock speed. The loop continues until a stop signal arrives.

```cpp
// from ApplicationBase::WorkerThread():
while (true) {
    DoWorkAsync();
    if (_stopWorkFuture.wait_for(wait) == future_status::ready) break;
    // optionally sleep between iterations
}
```

Use `--async` when you don't need deterministic co-simulation time (e.g., for real-time or hardware-in-the-loop scenarios). In `--async` mode no system controller is required if you also pass `--autonomous`.

---

### SIL Kit API Objects

| Object | How it is created | Purpose |
|--------|-------------------|---------|
| `IParticipant` | `SilKit::CreateParticipant(config, name, registryUri)` | Main handle; creates all other services |
| `IDataPublisher` | `GetParticipant()->CreateDataPublisher(name, spec, history)` | Sends byte blobs on a topic; `spec` includes topic name and media type |
| `IDataSubscriber` | `GetParticipant()->CreateDataSubscriber(name, spec, callback)` | Receives blobs; callback fires asynchronously on arrival |
| `ILifecycleService` | `GetParticipant()->CreateLifecycleService({mode})` | Manages the participant state machine; `Coordinated` mode requires system controller |
| `ITimeSyncService` | `_lifecycleService->CreateTimeSyncService()` | Registers the step handler for synchronized simulation |
| `ISystemMonitor` | `GetParticipant()->CreateSystemMonitor()` | Observe other participants' states |

---

### Data Flow and XCP Integration

```
Publisher process                      Subscriber process
─────────────────                      ──────────────────
DoWorkSync(now)                        [arrival callback fires]
  │                                           │
  ├─ PublishGPSData()                         ├─ gps_data decoded
  │    └─ _gpsPublisher->Publish(gps_data)    └─ DaqTriggerEventExt(GpsReceived, this)
  │
  └─ DaqTriggerEventExt(PublisherTask, this)
       └─ XCP DAQ captures gps_data
```

- XCP measurement on the **publisher** side is **time-triggered** (every simulation step).
- XCP measurement on the **subscriber** side is **event-triggered** (on each incoming message).

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
    ├── XcpServer.cpp   # XCP server participant
    ├── PublisherDemo.cpp   # Publisher demo with XCP instrumentation
    └── SubscriberDemo.cpp  # Subscriber demo with XCP instrumentation
```
