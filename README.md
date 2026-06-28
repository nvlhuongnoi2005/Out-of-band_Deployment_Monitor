# Out-of-Band Deployment Monitor

Hệ thống DevSecOps dùng để phát hiện **Shadow Deployment** và
**Configuration Drift** trên Linux production server.

Ý tưởng chính: Agent chạy trên production server để giám sát thay đổi file.
Khi có file bị tạo, sửa, xóa hoặc đổi quyền, Agent gửi event về Central Service.
Central sẽ đối chiếu với Jenkins/Deploy Window để phân loại thay đổi đó là hợp lệ
hay trái phép, sau đó ghi audit log, đẩy dữ liệu vào Elasticsearch/Grafana, gửi
email cảnh báo và có thể trigger Jenkins để tự khôi phục file chuẩn.

## 1. Kiến Trúc Tổng Quan

```text
Production VM
  oob-agent
    - giám sát filesystem bằng eBPF/bpftrace
    - fallback được sang inotify
    - lấy path, event type, uid, username, pid, process_name
    - gửi JSON event về Central

Host / Monitoring Server
  oob-central
    - nhận event từ Agent
    - kiểm tra Deploy Window
    - kiểm tra Jenkins job đang chạy
    - phân loại AUTHORIZED_CHANGE hoặc UNAUTHORIZED_DRIFT
    - ghi audit log JSON Lines
    - đẩy event vào Elasticsearch
    - gửi email alert
    - trigger Jenkins remediation nếu phát hiện drift

Observability
  Elasticsearch -> Grafana Dashboard
```

Luồng chính:

```text
Agent -> Central -> Audit Log -> Elasticsearch -> Grafana
                    |
                    +-> Email Alert
                    +-> Jenkins Remediation
```

## 2. Tính Năng Chính

- Giám sát các event file:
  - `CREATE`
  - `MODIFY`
  - `DELETE`
  - `ATTRIB`
  - `MOVED_FROM`
  - `MOVED_TO`
- Agent dùng eBPF/bpftrace để bắt event ở tầng kernel.
- Có inotify watcher để fallback khi eBPF không khả dụng.
- Event có thông tin:
  - file path
  - event type
  - uid
  - username
  - pid
  - process name
  - timestamp
- Central phân loại:
  - `AUTHORIZED_CHANGE`
  - `UNAUTHORIZED_DRIFT`
- Ghi audit log dạng JSON Lines.
- Đẩy event vào Elasticsearch.
- Hiển thị trên Grafana.
- Gửi email alert khi phát hiện drift trái phép.
- Trigger Jenkins job để restore file.
- Đóng gói Agent/Central thành Linux service qua `.deb`.

## 3. Cấu Trúc Thư Mục

```text
agent/              Source code của oob-agent
central-service/    Source code của oob-central
shared/             Model FileEvent dùng chung
deploy/             Script đóng gói, systemd service, logrotate
mock/               Config mẫu và Jenkinsfile demo
docs/               Tài liệu yêu cầu, kiến trúc, API contract
scripts/            Script đo latency và tài nguyên
third-party/        cpp-httplib header
```

## 4. Yêu Cầu Môi Trường

Máy build/Host:

- Linux
- CMake 3.16+
- Compiler hỗ trợ C++17
- Qt 6 Core và Qt 6 Network
- `dpkg-dev` nếu muốn build package `.deb`

VM chạy Agent:

- Khuyến nghị Ubuntu 22.04 hoặc 24.04
- `libqt6core6`
- `libqt6network6`
- `bpftrace` nếu chạy eBPF
- Kernel hỗ trợ eBPF/tracepoint

Central Host:

- Jenkins
- Elasticsearch
- Grafana
- Python 3 nếu bật SMTP email alert

## 5. Build Project

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Build riêng Agent:

```bash
cmake --build build --target oob-agent
```

Build riêng Central:

```bash
cmake --build build --target oob-central
```

## 6. Chạy Central Trên Host

Tạo config thật từ config mẫu:

```bash
cp mock/central-config.json.example mock/central-config.json
nano mock/central-config.json
```

Ví dụ phần Jenkins:

```json
"jenkins": {
  "url": "http://localhost:8081",
  "user": "admin",
  "token": "YOUR_JENKINS_API_TOKEN",
  "remediation_vm_ip": "192.168.1.16",
  "remediation_vm_user": "lap",
  "remediation_server": "prod-server-01",
  "ssl_verify": true,
  "remediate": true,
  "fail_open": false
}
```

Chạy Central:

```bash
./build/central-service/oob-central --config mock/central-config.json
```

Kiểm tra health:

```bash
curl http://localhost:8080/health
```

Kết quả đúng:

```json
{"status":"ok","service":"oob-central"}
```

## 7. Đóng Gói Và Cài Agent Trên VM

Trên Host:

```bash
./deploy/make-deb-agent.sh
scp deploy/oob-agent_0.1.0_amd64.deb lap@VM_IP:/tmp/
```

Trên VM:

```bash
sudo apt update
sudo dpkg -i /tmp/oob-agent_0.1.0_amd64.deb
sudo apt install -f -y
```

Sửa config Agent:

```bash
sudo nano /etc/oob-agent/config.json
```

Ví dụ:

```json
{
  "agent_id": "agent-01",
  "server": "prod-server-01",
  "project": "webapp",
  "watch_dirs": ["/tmp/demo-prod-app"],
  "central_url": "http://HOST_IP:8080",
  "watcher_backend": "ebpf",
  "retry_interval_sec": 5,
  "heartbeat_interval_sec": 30
}
```

Start Agent:

```bash
sudo systemctl restart oob-agent
sudo systemctl status oob-agent
sudo journalctl -u oob-agent -f
```

Nếu eBPF chạy đúng, log sẽ có:

```text
[Agent] Backend: eBPF (bpftrace)
[eBPF] Probes attached, monitoring active
```

## 8. Demo Phát Hiện Drift Trái Phép

Trên VM:

```bash
mkdir -p /tmp/demo-prod-app/config
echo '{"version":"HACKED"}' > /tmp/demo-prod-app/config/app.json
chmod 777 /tmp/demo-prod-app/config/app.json
rm /tmp/demo-prod-app/config/app.json
```

Trên Host:

```bash
tail -f /tmp/oob-audit.log
```

Kết quả mong đợi:

```json
"classification":"UNAUTHORIZED_DRIFT"
```

Với eBPF, event có thể có thêm:

```json
"uid": 1000,
"username": "lap",
"pid": 12345,
"process_name": "bash"
```

## 9. Demo Thay Đổi Hợp Lệ

Mở Deploy Window trên Host:

```bash
curl -s -X POST http://localhost:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"OPEN","project":"webapp","server":"prod-server-01","ttl_sec":120}'
```

Trên VM sửa file:

```bash
echo '{"version":"2.0.0","deployed_by":"jenkins"}' > /tmp/demo-prod-app/config/app.json
```

Đóng Deploy Window:

```bash
curl -s -X POST http://localhost:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"CLOSE","project":"webapp","server":"prod-server-01"}'
```

Kết quả mong đợi:

```json
"classification":"AUTHORIZED_CHANGE"
```

## 10. Elasticsearch Và Grafana

Trong `mock/central-config.json`, bật Elasticsearch:

```json
"elasticsearch": {
  "host": "localhost",
  "port": 9200,
  "index": "oob-audit",
  "user": "",
  "pass": "",
  "https": false
}
```

Kiểm tra dữ liệu trong Elasticsearch:

```bash
curl http://localhost:9200/oob-audit/_count
curl "http://localhost:9200/oob-audit/_search?pretty&size=3"
```

Cấu hình Grafana Elasticsearch datasource:

```text
URL: http://elasticsearch:9200
Index: oob-audit
Time field: @timestamp
```

Lưu ý: Grafana không đọc trực tiếp `/tmp/oob-audit.log`. Grafana đọc dữ liệu từ Elasticsearch.

## 11. Jenkins Auto-Remediation

Luồng remediation:

```text
UNAUTHORIZED_DRIFT
  -> Central gọi Jenkins /job/{project}/buildWithParameters
  -> Jenkins mở Deploy Window
  -> Jenkins SSH/Ansible vào VM để restore file
  -> Jenkins đóng Deploy Window
  -> File restore được phân loại AUTHORIZED_CHANGE
```

Điều kiện quan trọng:

- Jenkins job name phải khớp với `project` trong Agent config.
  - Ví dụ: `project = webapp` thì Jenkins job nên tên `webapp`.
- Jenkins job cần có parameter:
  - `VM_IP`
  - `VM_USER`
  - `OOB_SERVER_NAME`
- Jenkins credential SSH phải có ID:
  - `vm-deploy-key`

Jenkinsfile mẫu:

```text
mock/Jenkinsfile
```

Cooldown remediation hiện là 30 giây để tránh trigger Jenkins liên tục khi một drift tạo nhiều file event.

## 12. Đo Tài Nguyên Agent

Trên VM:

```bash
sudo apt install -y sysstat
pidstat -u -r -p $(pgrep -x oob-agent | head -1) 1 60
```

Kết quả đo mẫu:

```text
Average CPU: 0.03%
Average RSS: 15232 KB (~14.9 MB)
Average MEM: 0.38%
```

Kết luận: Agent tiêu thụ tài nguyên thấp trong điều kiện bình thường.

## 13. Ghi Chú Bảo Mật

- Không commit `mock/central-config.json` vì có thể chứa Jenkins token và SMTP password.
- Nên dùng Jenkins API token thay vì mật khẩu đăng nhập web.
- Nếu dùng Gmail SMTP, dùng Gmail App Password.
- Production thực tế nên bổ sung authentication/TLS giữa Agent và Central.

