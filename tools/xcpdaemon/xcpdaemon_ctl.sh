#!/bin/bash
# xcpdaemon_ctl.sh — control xcpdaemon on a remote Linux machine via SSH
#
# Usage (run on macOS/dev machine):
#   ./tools/xcpdaemon/xcpdaemon_ctl.sh <command> [options]
#
# Commands:
#   start              Start the xcpdaemon service (via systemd)
#   stop               Stop the xcpdaemon service
#   restart            Restart the xcpdaemon service
#   status             Show service status + SHM status
#   reload             Send SIGHUP (prints SHM status to journal)
#   log                Follow live log output (Ctrl-C to exit)
#   run                Run xcpdaemon in foreground (for testing, not as service)
#   clean              Run 'xcpdaemon clean' on the Pi
#   cleanall           Run 'xcpdaemon cleanall' on the Pi
#   install            Install and enable xcpdaemon.service on the Pi

set -euo pipefail

TARGET="${TARGET:-rainer@192.168.0.206}"
REMOTE_DIR="${REMOTE_DIR:-~/XCPlite-RainerZ}"
BINARY="$REMOTE_DIR/build/xcpdaemon"
CMD="${1:-status}"

ssh_run() { ssh "$TARGET" "$@"; }

case "$CMD" in
    start)
        echo "Starting xcpdaemon service on $TARGET..."
        ssh_run "sudo systemctl start xcpdaemon"
        ssh_run "systemctl status xcpdaemon --no-pager"
        ;;
    stop)
        echo "Stopping xcpdaemon service on $TARGET..."
        ssh_run "sudo systemctl stop xcpdaemon"
        ;;
    restart)
        echo "Restarting xcpdaemon service on $TARGET..."
        ssh_run "sudo systemctl restart xcpdaemon"
        ssh_run "systemctl status xcpdaemon --no-pager"
        ;;
    status)
        echo "=== systemd status ==="
        ssh_run "systemctl status xcpdaemon --no-pager" || true
        echo ""
        echo "=== SHM status ==="
        ssh_run "$BINARY status" || true
        ;;
    reload)
        echo "Sending SIGHUP to xcpdaemon on $TARGET (prints SHM status to journal)..."
        ssh_run "sudo systemctl reload xcpdaemon"
        ;;
    log)
        echo "Following xcpdaemon log on $TARGET (Ctrl-C to exit)..."
        ssh "$TARGET" "journalctl -u xcpdaemon -f"
        ;;
    run)
        echo "Running xcpdaemon in foreground on $TARGET (Ctrl-C to stop)..."
        ssh -t "$TARGET" "$BINARY ${*:2}"
        ;;
    clean)
        echo "Running 'xcpdaemon clean' on $TARGET..."
        ssh_run "$BINARY clean"
        ;;
    cleanall)
        echo "Running 'xcpdaemon cleanall' on $TARGET..."
        ssh_run "$BINARY cleanall"
        ;;
    install)
        echo "Installing xcpdaemon service on $TARGET..."
        ssh_run "cd $REMOTE_DIR && sudo bash tools/xcpdaemon/install_service.sh"
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status|reload|log|run|clean|cleanall|install}"
        exit 1
        ;;
esac
