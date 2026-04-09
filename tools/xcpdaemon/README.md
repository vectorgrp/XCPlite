# XCPlite Daemon

This application serves as a daemon for multi-application measurement and calibration use cases.

In principle, it is just another XCP instrumented application in multi application (SHM) mode, configured to insist on being the XCP server.  
All other applications must be configured to never start an XCP server, or if they are in auto mode, they must be started after the daemon.  
It creates the master A2L file and manages the binary calibration data persistence file.  

It has some own measurement and calibration objects to monitor the system and multiple XCP/SHM instrumented applications.  


## Usage

```
xcpdaemon [OPTIONS]
xcpdaemon [OPTIONS] status
xcpdaemon [OPTIONS] clean
xcpdaemon [OPTIONS] cleanall
xcpdaemon help
```

**Options:**

| Flag | Description |
|------|-------------|
| `-l, --log-level <0-4>` | Log level: 0=off 1=error 2=warn 3=info 4=debug (default: 3) |
| `-p, --port <port>` | XCP server port (default: 5555) |
| `-a, --addr <ip>` | Bind address (default: 0.0.0.0) |
| `--tcp` / `--udp` | Transport protocol (default: UDP) |
| `-q, --queue-size <n>` | Measurement queue size in bytes (default: 32768) |
| `-d, --daemonize` | Fork to background, write PID to `/tmp/xcpdaemon.pid` |
| `-h, --help` | Show help message |

**Commands (execute and exit):**

| Command | Description |
|---------|-------------|
| `status` | Show registered applications and SHM state and exit |
| `clean` | Unlink shared memory (`/xcpdata`, `/xcpqueue`) and delete `main.bin` / `main.a2l` |
| `cleanall` | Delete all finalized application A2L files listed in SHM, then clean |
| `help` | Show help message |

Use clean commands only after all apps and daemon have stopped !!!  



## Running in the foreground

```sh
# Start with default settings
./build/xcpdaemon

# Start on a different port with debug logging of XCP commands
./build/xcpdaemon --port 5556 --log-level 4
```

Press **Ctrl-C** to stop gracefully.


## Running as a background daemon

Use `--daemonize` to fork to the background. The daemon detaches from the terminal and writes its PID to `/tmp/xcpdaemon.pid`.

```sh
# Start as daemon
./build/xcpdaemon --daemonize

# Check status
./build/xcpdaemon status

# Stop daemon
kill $(cat /tmp/xcpdaemon.pid)
```


## Running as a service on a Linux machine

### Sync, build and install to remote machine

```sh
rsync -avz --exclude=build/ --exclude=.git/ ./ rainer@192.168.0.206:~/XCPlite-RainerZ/
ssh rainer@192.168.0.206 "cd ~/XCPlite-RainerZ && cmake -B build -S . && cmake --build build"
```

### Install as systemd service

Using systemd is the preferred way to run `xcpdaemon` on Linux.  
It handles background execution, automatic restart on failure, and logging via `journalctl` — without needing `--daemonize`.

**1. Edit the unit file** to match your installation path and user:

```sh
nano tools/xcpdaemon/xcpdaemon.service
# Adjust: User=, WorkingDirectory=, ExecStart=
```

**2. Install and enable the service:**

```sh
sudo cp tools/xcpdaemon/xcpdaemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable xcpdaemon   # start automatically on boot
sudo systemctl start xcpdaemon
```

**3. Useful commands:**

```sh
systemctl status xcpdaemon         # check if running
systemctl stop xcpdaemon           # stop
systemctl restart xcpdaemon        # restart
systemctl reload xcpdaemon         # send SIGHUP (print status to journal)

# Follow live output (like tail -f)
journalctl -u xcpdaemon -f

# Show all output since last start
journalctl -u xcpdaemon -n 100
```


### Appendix


Two helper scripts are provided in `tools/xcpdaemon/`:

| Script | Purpose |
|--------|---------|
| `install_service.sh` | Install and enable xcpdaemon.service on Linux (run on target machine with sudo) |
| `xcpdaemon_ctl.sh` | Remote control via SSH |

