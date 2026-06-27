#!/bin/bash
# Build Debian package for oob-agent only.
# Install on every production server that needs monitoring.
#
# Usage: ./deploy/make-deb-agent.sh [build-dir]
# Requires: dpkg-dev  (apt install dpkg-dev)

set -e

PACKAGE="oob-agent"
VERSION="0.1.0"
ARCH="amd64"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-$REPO_DIR/build}"
PKG_DIR="$SCRIPT_DIR/${PACKAGE}_${VERSION}_${ARCH}"

AGENT_BIN="$BUILD_DIR/agent/oob-agent"

echo "=== Building $PACKAGE $VERSION ==="

if [ ! -f "$AGENT_BIN" ]; then
    echo "ERROR: $AGENT_BIN not found. Build first:"
    echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
    exit 1
fi

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/DEBIAN"
mkdir -p "$PKG_DIR/usr/local/bin"
mkdir -p "$PKG_DIR/etc/oob-agent"
mkdir -p "$PKG_DIR/lib/systemd/system"

# ── Binary ────────────────────────────────────────────────────────────────────
cp "$AGENT_BIN" "$PKG_DIR/usr/local/bin/oob-agent"
strip --strip-unneeded "$PKG_DIR/usr/local/bin/oob-agent"

# ── Systemd unit ─────────────────────────────────────────────────────────────
cp "$SCRIPT_DIR/oob-agent.service" "$PKG_DIR/lib/systemd/system/oob-agent.service"

# ── Default config ────────────────────────────────────────────────────────────
cat > "$PKG_DIR/etc/oob-agent/config.json" << 'EOF'
{
  "agent_id":               "agent-01",
  "server":                 "prod-server-01",
  "project":                "webapp",
  "watch_dirs":             ["/opt/webapp"],
  "central_url":            "http://central-host:8080",
  "retry_interval_sec":     5,
  "heartbeat_interval_sec": 30
}
EOF

# ── DEBIAN/control ────────────────────────────────────────────────────────────
cat > "$PKG_DIR/DEBIAN/control" << EOF
Package: $PACKAGE
Version: $VERSION
Architecture: $ARCH
Maintainer: OOB Monitor <noreply@example.com>
Depends: libqt6core6 | libqt6core6t64, libqt6network6 | libqt6network6t64
Recommends: bpftrace
Description: Out-of-Band Deployment Monitor — Agent
 Lightweight filesystem sensor. Detects file changes on a production
 server and reports them to oob-central for classification.
 .
 Install this package on every server you want to monitor.
 The central service (oob-central package) runs separately.
EOF

cat > "$PKG_DIR/DEBIAN/conffiles" << 'EOF'
/etc/oob-agent/config.json
EOF

cat > "$PKG_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e
systemctl daemon-reload
systemctl enable oob-agent.service
echo ""
echo "oob-agent installed. Next steps:"
echo "  1. Edit /etc/oob-agent/config.json  (set central_url, watch_dirs, server name)"
echo "  2. systemctl start oob-agent"
echo "  3. systemctl status oob-agent"
EOF
chmod 755 "$PKG_DIR/DEBIAN/postinst"

cat > "$PKG_DIR/DEBIAN/prerm" << 'EOF'
#!/bin/bash
set -e
systemctl stop    oob-agent.service 2>/dev/null || true
systemctl disable oob-agent.service 2>/dev/null || true
EOF
chmod 755 "$PKG_DIR/DEBIAN/prerm"

# ── Build ─────────────────────────────────────────────────────────────────────
dpkg-deb --build --root-owner-group "$PKG_DIR"
DEB_FILE="${PKG_DIR}.deb"
echo ""
echo "Package : $DEB_FILE"
echo "Install : sudo dpkg -i $DEB_FILE"
echo "Remove  : sudo dpkg -r $PACKAGE"
