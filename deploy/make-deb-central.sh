#!/bin/bash
# Build Debian package for oob-central only.
# Install on the monitoring/control server — NOT on production servers.
#
# Usage: ./deploy/make-deb-central.sh [build-dir]
# Requires: dpkg-dev  (apt install dpkg-dev)

set -e

PACKAGE="oob-central"
VERSION="0.1.0"
ARCH="amd64"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-$REPO_DIR/build}"
PKG_DIR="$SCRIPT_DIR/${PACKAGE}_${VERSION}_${ARCH}"

CENTRAL_BIN="$BUILD_DIR/central-service/oob-central"

echo "=== Building $PACKAGE $VERSION ==="

if [ ! -f "$CENTRAL_BIN" ]; then
    echo "ERROR: $CENTRAL_BIN not found. Build first:"
    echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
    exit 1
fi

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/DEBIAN"
mkdir -p "$PKG_DIR/usr/local/bin"
mkdir -p "$PKG_DIR/etc/oob-central"
mkdir -p "$PKG_DIR/etc/logrotate.d"
mkdir -p "$PKG_DIR/lib/systemd/system"

# ── Binary ────────────────────────────────────────────────────────────────────
cp "$CENTRAL_BIN" "$PKG_DIR/usr/local/bin/oob-central"
strip --strip-unneeded "$PKG_DIR/usr/local/bin/oob-central"

# ── Systemd unit + logrotate ──────────────────────────────────────────────────
cp "$SCRIPT_DIR/oob-central.service"    "$PKG_DIR/lib/systemd/system/oob-central.service"
cp "$SCRIPT_DIR/logrotate.d/oob-audit"  "$PKG_DIR/etc/logrotate.d/oob-audit"

# ── Default config ────────────────────────────────────────────────────────────
cp "$REPO_DIR/mock/central-config.json.example" "$PKG_DIR/etc/oob-central/config.json"

# ── DEBIAN/control ────────────────────────────────────────────────────────────
cat > "$PKG_DIR/DEBIAN/control" << EOF
Package: $PACKAGE
Version: $VERSION
Architecture: $ARCH
Maintainer: OOB Monitor <noreply@example.com>
Depends: libqt6core6 | libqt6core6t64, libqt6network6 | libqt6network6t64, curl, python3
Description: Out-of-Band Deployment Monitor — Central Service
 Decision and audit service. Receives file-change events from oob-agent,
 classifies them as AUTHORIZED_CHANGE or UNAUTHORIZED_DRIFT by comparing
 against Jenkins pipeline state, writes audit logs, pushes to Elasticsearch,
 sends email alerts, and optionally re-triggers Jenkins to remediate drift.
 .
 Install this package on the monitoring server only.
 Production servers run oob-agent (separate package).
EOF

cat > "$PKG_DIR/DEBIAN/conffiles" << 'EOF'
/etc/oob-central/config.json
/etc/logrotate.d/oob-audit
EOF

cat > "$PKG_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e
systemctl daemon-reload
systemctl enable oob-central.service
echo ""
echo "oob-central installed. Next steps:"
echo "  1. Edit /etc/oob-central/config.json  (set jenkins.url, jenkins.token, smtp, ...)"
echo "  2. systemctl start oob-central"
echo "  3. systemctl status oob-central"
EOF
chmod 755 "$PKG_DIR/DEBIAN/postinst"

cat > "$PKG_DIR/DEBIAN/prerm" << 'EOF'
#!/bin/bash
set -e
systemctl stop    oob-central.service 2>/dev/null || true
systemctl disable oob-central.service 2>/dev/null || true
EOF
chmod 755 "$PKG_DIR/DEBIAN/prerm"

# ── Build ─────────────────────────────────────────────────────────────────────
dpkg-deb --build --root-owner-group "$PKG_DIR"
DEB_FILE="${PKG_DIR}.deb"
echo ""
echo "Package : $DEB_FILE"
echo "Install : sudo dpkg -i $DEB_FILE"
echo "Remove  : sudo dpkg -r $PACKAGE"
