#!/bin/bash
# install_service.sh 
# install xcpdaemon as a systemd service on Linux
#
# Usage (run on the target machine):
#   bash tools/xcpdaemon/install_service.sh [install_dir]
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
INSTALL_DIR="${1:-$REPO_DIR}"
BINARY="$INSTALL_DIR/build/xcpdaemon"
SERVICE_SRC="$SCRIPT_DIR/xcpdaemon.service"
SERVICE_DEST="/etc/systemd/system/xcpdaemon.service"
USER_NAME="${SUDO_USER:-$(logname 2>/dev/null || echo "$USER")}"

if [[ $EUID -ne 0 ]]; then
    echo "Error: this script must be run with sudo" >&2
    exit 1
fi

if [[ ! -x "$BINARY" ]]; then
    echo "Error: binary not found or not executable: $BINARY" >&2
    echo "Build it first with:  ./build.sh all" >&2
    exit 1
fi

# Generate service file from template with real paths and user
sed \
    -e "s|User=rainer|User=$USER_NAME|g" \
    -e "s|WorkingDirectory=.*|WorkingDirectory=$INSTALL_DIR|g" \
    -e "s|ExecStart=.*|ExecStart=$BINARY|g" \
    "$SERVICE_SRC" > "$SERVICE_DEST"

echo "Installed: $SERVICE_DEST"
cat "$SERVICE_DEST"

systemctl daemon-reload
systemctl enable xcpdaemon
echo ""
echo "Service enabled. Start it with:"
echo "  sudo systemctl start xcpdaemon"
echo "  systemctl status xcpdaemon"
echo "  journalctl -u xcpdaemon -f"
