# shmtool

A command-line diagnostic tool for inspecting, validating, and cleaning the POSIX shared memory regions used by XCPlite in multi-process (SHM) mode.

The tool reads the `/xcpdata` and `/xcpqueue` shared memory segments that XCPlite applications create when compiled with `OPTION_SHM_MODE`.  


## Usage

```
shmtool [command] [options]
```

**Commands:**

| Command | Description |
|---------|-------------|
| `status` (default) | Print the contents of `/xcpdata` and `/xcpqueue` |
| `finalize` | Set `a2l_finalize_requested`, poll for acknowledgement, print status |
| `clean` | Remove `/xcpdata`, `/xcpqueue` and associated lock files |
| `help` | Print help text |

**Options:**

| Flag | Description |
|------|-------------|
| `-v, --verbose` | Show additional low-level details (offsets, pad fields) |
| `--timeout <ms>` | Polling timeout for the `finalize` command (default: 5000 ms) |


## Commands

### status

Prints a summary of all registered applications and the overall SHM header:

```bash
./build/shmtool
./build/shmtool status
./build/shmtool status -v   # more detail
```

Example output:
```
/xcpdata mmap found, size = 16384 bytes
================================================================================
  version            : 1.0.0
  declared size      : 4096 bytes  (this build: 4096)
  leader pid         : 12345
  app count          : 2 / 8
  A2L finalize req'd : no
--------------------------------------------------------------------------------
  App 0:  MyProject [server] epk=V1.0.0  pid=12345  [leader]
          a2l_name=(pending)  finalized=no  alive_counter=47
--------------------------------------------------------------------------------
  App 1:  SensorApp  epk=V1.0.0  pid=12346
          a2l_name=(pending)  finalized=no  alive_counter=23
--------------------------------------------------------------------------------
```

### finalize

Triggers A2L file generation in all registered applications by setting the `a2l_finalize_requested` flag, then polls until all apps acknowledge (set `a2l_finalized`) or the timeout expires:

```bash
./build/shmtool finalize
./build/shmtool finalize --timeout 10000   # 10 s timeout
```

Exit codes: `0` = all acknowledged, `2` = partial timeout.

### clean

Removes the shared memory regions and lock files left behind by a crashed or incorrectly stopped session:

```bash
./build/shmtool clean
```

Removes:
- `/xcpdata`
- `/xcpqueue`
- `/tmp/xcpdata.lock`
- `/tmp/xcpqueue.lock`

> Use `clean` only after all instrumented applications and the XCP server (or `xcpdaemon`) have stopped.


## Building

`shmtool` is built as part of the XCPlite CMake project:

```bash
cmake -B build -S .
cmake --build build --target shmtool
```

The binary is placed in `build/shmtool`.

> `shmtool` requires that the XCPlite library is compiled with `OPTION_SHM_MODE` enabled. The `status` and `finalize` commands are only available in that configuration; `clean` is always available.




## Related tools

- [`xcpdaemon`](../xcpdaemon/README.md) — XCP-on-Ethernet server for multi-process SHM sessions
- [`xcpclient`](../xcpclient/README.md) — XCP test client for connecting, measuring, and calibrating

See [docs/SHM.md](../../docs/SHM.md) for a detailed description of the XCPlite shared memory multi-process mode.  
