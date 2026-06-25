#!/bin/bash
# Install oob-agent and oob-central as systemd services.
# Usage: sudo ./deploy/install.sh

set -e
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
DEPLOY_DIR="$REPO_DIR/deploy"

echo "=== Installing Out-of-Band Deployment Monitor ==="

# Binaries
install -m 755 "$BUILD_DIR/agent/oob-agent"              /usr/local/bin/oob-agent
install -m 755 "$BUILD_DIR/central-service/oob-central"  /usr/local/bin/oob-central
echo "Binaries installed to /usr/local/bin/"

# Agent config
mkdir -p /etc/oob-agent
if [ ! -f /etc/oob-agent/config.json ]; then
    install -m 644 "$REPO_DIR/mock/agent-config.json" /etc/oob-agent/config.json
    echo "Agent config installed to /etc/oob-agent/config.json"
else
    echo "Agent config already exists — skipping (edit /etc/oob-agent/config.json manually)"
fi

# Systemd units
install -m 644 "$DEPLOY_DIR/oob-agent.service"   /etc/systemd/system/
install -m 644 "$DEPLOY_DIR/oob-central.service" /etc/systemd/system/
echo "Systemd units installed"

# Enable
systemctl daemon-reload
systemctl enable oob-agent oob-central
echo ""

# Start central first, then agent
systemctl restart oob-central
sleep 2
systemctl restart oob-agent

echo ""
echo "=== Status ==="
systemctl status oob-central --no-pager -l
echo ""
systemctl status oob-agent --no-pager -l
