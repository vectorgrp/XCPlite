# silkit_demo — SIL Kit Pub/Sub Demo with XCP Measurement

This example demonstrates how to combine **SIL Kit** (SIL Kit – Open-Source Library for Connecting Software-in-the-Loop Environments) with **XCPlite** to add live measurement and calibration to one or more SIL Kit simulation participants.  

In the example, two participants (derived from the original **SIL Kit** Publisher/Subscriber example) are provided.  
The Publisher produces some test data on each simulation step and makes the internal values observable via XCP.  
The Subscriber receives that data and also exposes the received values via XCP.

In CANape, the time synchronisation mode must be set to simulation (in project settings, time source), because the simulation time may run faster or slower than real time.  

There are 2 options to run the demo.  


### Option 1

To separate XCP servers for publisher and subscriber

| Executable            | SIL Kit role   | XCP server port |
|-----------------------|----------------|-----------------|
| `SilKitDemoPublisher` | Data Publisher | TCP 5555        |
| `SilKitDemoSubscriber`| Data Subscriber| TCP 5556        |


### Option 2

A single XCP server in multi-application shared memory mode for both publisher and subscriber.  

Compile the XCP library in multi-application shared memory mode (#define `OPTION_SHM_MODE` in xcplib_cfg.h), which then runs both participants with the same XCP server port (e.g., 5555).  
The participant that starts first becomes the XCP server, the other participant automatically detects the running server and connects to it in shared memory mode.

Note that the XCP multi application shared memory mode is only supported on POSIX-compliant platforms (Linux, macOS, QNX) and not on Windows, and is still in experimental state.


---



### SilKit

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

On macOS, `run.sh` opens all four required Terminal.app windows automatically:

```bash
./run.sh              # default: 1ms steps, slow throttle (2 steps/s)
./run.sh -f           # as fast as possible
./run.sh -r           # approximately real time (AnimationFactor=1.0)
./run.sh -d 10000     # 10ms steps, slow throttle
./run.sh -d 10000 -f  # 10ms steps, as fast as possible
./run.sh -d 10000 -r  # 10ms steps, approximately real time
./run.sh -h           # show help
```

| Option | Description |
|--------|-------------|
| `-d <us>` | Simulation step duration in microseconds (default: `1000` = 1 ms) |
| `-f` | Run as fast as possible — removes all throttling |
| `-r` | Run in approximately real time — uses SIL Kit `AnimationFactor=1.0` via a generated participant config file |

`-f` and `-r` are mutually exclusive.

By default SIL Kit slows the simulation to approximately 2 steps per second so that log output stays readable. Pass `-f` when you want maximum throughput or `-r` for real-time execution.

### Manual start

Open **four** separate terminals. All commands are relative to the silkit_demo directory.

**Terminal 1 — SIL Kit Registry:**

```bash
/path/to/sil-kit/_build/debug/Debug/sil-kit-registry
/Users/Rainer.Zaiser/git/sil-kit/_build/debug/Debug/sil-kit-registry
```

**Terminal 2 — Publisher** (XCP on TCP 5555):

```bash
./build/SilKitDemoPublisher
/Users/Rainer.Zaiser/git/XCPlite-RainerZ/examples/silkit_demo/
# fast
build/SilKitDemoPublisher --sim-step-duration 10000 --fast
# realtime
build/SilKitDemoPublisher --sim-step-duration 10000 --config silkit_participant_cfg.json

```

**Terminal 3 — Subscriber** (XCP on TCP 5556):

```bash
./build/SilKitDemoSubscriber
/Users/Rainer.Zaiser/git/XCPlite-RainerZ/examples/silkit_demo/
#fast
build/SilKitDemoSubscriber --sim-step-duration 10000 --fast
# realtime
build/SilKitDemoSubscriber --sim-step-duration 10000 --config silkit_participant_cfg.json
```

**Terminal 4 — System Controller** (starts synchronized simulation):

```bash
/path/to/sil-kit/_build/debug/Debug/sil-kit-system-controller Publisher Subscriber
/Users/Rainer.Zaiser/git/sil-kit/_build/debug/Debug/sil-kit-system-controller Publisher Subscriber
```

The `--sim-step-duration <us>` and `--fast` flags can be passed directly to both participant binaries when starting manually.

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

## SIL Kit Architecture and API

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
Publisher process                    Subscriber process
─────────────────                    ──────────────────
DoWorkSync(now)                      [arrival callback fires]
  │                                        │
  ├─ PublishGPSData()                      ├─ gpsData decoded
  │    └─ _gpsPublisher->Publish(bytes)    ├─ _latitude = ...
  │          │                             └─ DaqTriggerEventExt(GpsReceived, this)
  │          └── SIL Kit registry ────────────────────────────────────────►
  │
  └─ DaqTriggerEventExt(PublisherTask, this)
       └─ XCP DAQ captures _latitude, _longitude, _temperatur
```

- XCP measurement on the **publisher** side is **time-triggered** (every simulation step).
- XCP measurement on the **subscriber** side is **event-triggered** (on each incoming message).
- Both run independent XCP servers (ports 5555 and 5556) so CANape can connect to each separately.

---

## XCP Signals

### Publisher (port 5555)

| Signal name        | Type   | Description                  |
|--------------------|--------|------------------------------|
| `_latitude`    | double | GPS latitude in degrees      |
| `_longitude`   | double | GPS longitude in degrees     |
| `_temperatur` | double | Temperature in Celsius        |

DAQ event: `PublisherTask` — triggered once per simulation step.

### Subscriber (port 5556)

| Signal name        | Type   | Description                       |
|--------------------|--------|-----------------------------------|
| `_latitude`    | double | Received GPS latitude in degrees  |
| `_longitude`   | double | Received GPS longitude in degrees |
| `_temperatur` | double | Received temperature in Celsius   |

DAQ events: `GpsReceived`, `TempReceived` — triggered in the data receive callbacks.

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
