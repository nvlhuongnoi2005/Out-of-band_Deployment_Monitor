#!/bin/bash
# Install oob-central on the monitoring server.
# Usage: sudo ./deploy/install-central.sh
#
# Chỉ chạy trên MÁY GIÁM SÁT (monitoring server).
# KHÔNG cài lên production server — dùng install-agent.sh cho production servers.

set -e

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
DEPLOY_DIR="$REPO_DIR/deploy"

echo "=== Installing oob-central ==="

# Binary
install -m 755 "$BUILD_DIR/central-service/oob-central" /usr/local/bin/oob-central
echo "Binary   : /usr/local/bin/oob-central"

# Systemd unit
install -m 644 "$DEPLOY_DIR/oob-central.service" /etc/systemd/system/oob-central.service
echo "Service  : /etc/systemd/system/oob-central.service"

# Logrotate
install -m 644 "$DEPLOY_DIR/logrotate.d/oob-audit" /etc/logrotate.d/oob-audit
echo "Logrotate: /etc/logrotate.d/oob-audit"

systemctl daemon-reload
systemctl enable oob-central

echo ""
echo "=== Xong. Bước tiếp theo ==="
echo "  Chỉnh ExecStart trong service file nếu cần thêm flags:"
echo "  systemctl edit oob-central"
echo ""
echo "  Ví dụ flags đầy đủ:"
echo "  /usr/local/bin/oob-central \\"
echo "    --port 8080 \\"
echo "    --audit-log /var/log/oob-audit.log \\"
echo "    --jenkins-url https://jenkins:8443 \\"
echo "    --jenkins-user admin \\"
echo "    --jenkins-token <token> \\"
echo "    --jenkins-remediate \\"
echo "    --es-host localhost --es-port 9200 \\"
echo "    --smtp-host smtp.gmail.com --smtp-port 587 \\"
echo "    --smtp-user you@gmail.com --smtp-pass <app-pass> \\"
echo "    --smtp-from you@gmail.com --smtp-to admin@company.com"
echo ""
echo "  systemctl start oob-central"
echo "  systemctl status oob-central"
