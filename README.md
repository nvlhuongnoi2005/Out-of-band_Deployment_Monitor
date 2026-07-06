# Out-of-Band Deployment Monitor

Hệ thống phát hiện **shadow deployment** và **configuration drift** trên server
Linux: Agent theo dõi thay đổi file trên máy production, gửi event về Central,
Central đối chiếu với Deploy Window/Jenkins để phân loại thay đổi hợp lệ hay
trái phép, sau đó ghi audit log, đẩy dữ liệu sang Elasticsearch/Grafana, gửi
cảnh báo email và có thể kích hoạt Jenkins/Ansible để khôi phục trạng thái đúng.

Project này đang ở mức **MVP/demo-ready** cho luồng DevSecOps: phát hiện drift,
phân loại event, quan sát bằng dashboard, cảnh báo và remediation. Các phần như
mutual TLS, phân quyền người dùng, persistence dài hạn cho queue và test tự động
đầy đủ được xem là hướng phát triển tiếp theo.

## 1. Bài Toán

Trong vận hành thực tế, production server đôi khi bị thay đổi ngoài luồng CI/CD:

- Engineer SSH vào server và sửa file trực tiếp.
- Script tạm hoặc thao tác thủ công ghi đè cấu hình.
- File bị xóa/chmod/rename mà Jenkins không hề chạy deploy.
- Sau sự cố, team khó trả lời: ai sửa, sửa lúc nào, server nào bị drift, và có
  cần rollback hay không.

Hệ thống này giải quyết bằng cách đặt một Agent trên server cần giám sát. Mỗi
khi file trong thư mục project thay đổi, Agent gửi event về Central. Central chỉ
xem thay đổi là hợp lệ nếu đang có Deploy Window hoặc Jenkins job deploy đang
chạy; còn lại được gắn nhãn `UNAUTHORIZED_DRIFT`.

## 2. Kiến Trúc Tổng Quan

```text
Production VM / Server
  oob-agent
    - theo dõi filesystem bằng inotify hoặc eBPF/bpftrace
    - thu thập path, event type, uid, username, pid, process name
    - gửi event và heartbeat về Central qua HTTP/HTTPS

Monitoring Host
  oob-central
    - nhận event từ Agent
    - quản lý Deploy Window
    - kiểm tra trạng thái Jenkins job
    - phân loại AUTHORIZED_CHANGE / UNAUTHORIZED_DRIFT
    - ghi audit log JSON Lines
    - đẩy event sang Elasticsearch
    - gửi SMTP alert
    - trigger Jenkins remediation nếu bật

External Systems
  Jenkins -> Ansible -> Production VM
  Elasticsearch -> Grafana Dashboard
```

Luồng chính:

```text
Agent -> Central -> Audit Log -> Elasticsearch -> Grafana
                  +-> Email Alert
                  +-> Jenkins Remediation -> Ansible Restore
```

## 3. Tech Stack

| Nhóm | Tech stack | Vai trò |
|---|---|---|
| Ngôn ngữ chính | C++17 | Xây dựng `oob-agent` và `oob-central` |
| Framework/runtime | Qt 6 Core, Qt 6 Network | Event loop, config, timer, HTTP client, process, file I/O |
| Build system | CMake 3.16+ | Build binary Agent/Central |
| File monitoring | Linux inotify | Backend mặc định, nhẹ, phù hợp demo phổ thông |
| File monitoring nâng cao | eBPF qua `bpftrace` | Bắt event ở tầng kernel, lấy PID/UID chính xác hơn |
| Agent transport | HTTP/HTTPS + JSON | Agent gửi event và heartbeat về Central |
| Central HTTP server | `cpp-httplib` | REST API cho event, deploy window, heartbeat, health check |
| Config | JSON | Cấu hình Agent, Central, Jenkins, Elasticsearch, SMTP |
| Decision engine | C++ Chain of Responsibility | Quyết định `AUTHORIZED_CHANGE` hoặc `UNAUTHORIZED_DRIFT` |
| CI/CD integration | Jenkins | Source-of-truth cho deploy job đang chạy |
| Remediation | Jenkins + Ansible + OpenSSH | Re-deploy/restore file chuẩn khi phát hiện drift |
| HTTP integration | `curl` subprocess | Gọi Jenkins API và Elasticsearch API |
| Audit local | JSON Lines | Lưu audit log bền vững trên file |
| Observability | Elasticsearch + Grafana | Truy vấn và hiển thị event/drift theo thời gian |
| Email alert | SMTP, Python 3 `smtplib` | Gửi cảnh báo khi có drift trái phép |
| Service manager | systemd | Chạy Agent/Central như Linux service |
| Packaging | Debian `.deb` scripts | Đóng gói cài đặt cho Agent và Central |

## 4. Tính Năng Chính

- Giám sát các event file: `CREATE`, `MODIFY`, `DELETE`, `ATTRIB`,
  `MOVED_FROM`, `MOVED_TO`.
- Hỗ trợ hai watcher backend: `inotify` và `ebpf`.
- Thu thập thông tin user/process: `uid`, `username`, `pid`, `process_name`.
- Agent có heartbeat và queue retry trong memory khi Central tạm mất kết nối.
- Central expose REST API:
  - `GET /health`
  - `POST /api/v1/events`
  - `POST /api/v1/deploy-window`
  - `POST /api/v1/heartbeat`
- Deploy Window giúp tránh cảnh báo nhầm trong lúc Jenkins/Ansible deploy hợp lệ.
- Jenkins cross-check để xác nhận job deploy đang chạy.
- Audit log dạng JSON Lines.
- Elasticsearch indexing và Grafana dashboard.
- SMTP email alert khi có `UNAUTHORIZED_DRIFT`.
- Jenkins auto-remediation để khôi phục file bằng pipeline/Ansible.
- Script đo latency, tài nguyên và kịch bản mất kết nối.
- Script đóng gói `.deb` và systemd unit cho Agent/Central.

## 5. Cấu Trúc Repository

```text
agent/              Source code oob-agent
central-service/    Source code oob-central
shared/             Model dùng chung, ví dụ FileEvent
deploy/             systemd unit, installer, script đóng gói .deb
mock/               Config mẫu, Jenkinsfile demo, dữ liệu mock
docs/               Requirements, architecture, API contract
scripts/            Script đo latency, resource, disconnect/retry
third-party/        cpp-httplib header
```

Tài liệu quan trọng:

- `docs/requirements.md`: yêu cầu FR/NFR.
- `docs/architecture.md`: kiến trúc chi tiết và quyết định thiết kế.
- `docs/api-contract.md`: schema API và audit log.

## 6. Yêu Cầu Môi Trường

Build host:

- Linux.
- CMake 3.16+.
- Compiler hỗ trợ C++17.
- Qt 6 Core và Qt 6 Network.
- `dpkg-dev` nếu cần đóng gói `.deb`.

Agent VM:

- Ubuntu 22.04/24.04 khuyến nghị.
- `libqt6core6`, `libqt6network6`.
- `bpftrace` nếu chạy backend eBPF.
- Kernel hỗ trợ eBPF/tracepoint nếu dùng eBPF.

Central host:

- Jenkins nếu test deploy/remediation.
- Ansible và OpenSSH client trên Jenkins node/container.
- Elasticsearch nếu bật indexing.
- Grafana nếu làm dashboard.
- Python 3 nếu bật SMTP email alert.

## 7. Build Project

Build toàn bộ:

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

Kiểm tra binary:

```bash
./build/central-service/oob-central --help
./build/agent/oob-agent --help
```

## 8. Chạy Nhanh Trên Một Máy

Chuẩn bị thư mục demo:

```bash
mkdir -p /tmp/demo-prod-app/config
```

Chạy Central ở terminal 1:

```bash
./build/central-service/oob-central --audit-log /tmp/oob-audit.log
```

Chạy Agent bằng inotify ở terminal 2:

```bash
./build/agent/oob-agent --config mock/agent-config.json --watcher inotify
```

Theo dõi audit log ở terminal 3:

```bash
tail -f /tmp/oob-audit.log
```

Tạo drift trái phép:

```bash
echo '{"version":"HACKED"}' > /tmp/demo-prod-app/config/app.json
```

Kết quả mong đợi trong audit log:

```json
"classification":"UNAUTHORIZED_DRIFT"
```

## 9. Demo Host + VM

Mô hình demo khuyến nghị:

```text
Host:
  - oob-central
  - Jenkins
  - Elasticsearch
  - Grafana

Linux VM:
  - oob-agent
  - thư mục được giám sát: /tmp/demo-prod-app
```

Lưu ý quan trọng: trong config Agent trên VM, `central_url` phải dùng IP của
Host, ví dụ `http://192.168.1.10:8080`. Không dùng `localhost` hoặc `127.0.0.1`
vì khi chạy trong VM, hai địa chỉ đó trỏ về chính VM.

### 9.1 Chạy Central Trên Host

Tạo config thật từ template:

```bash
cp mock/central-config.json.example mock/central-config.json
nano mock/central-config.json
```

Chạy Central:

```bash
./build/central-service/oob-central --config mock/central-config.json
```

Health check:

```bash
curl http://localhost:8080/health
```

Kết quả mong đợi:

```json
{"status":"ok","service":"oob-central"}
```

### 9.2 Cài Agent Trên VM

Đóng gói Agent trên Host:

```bash
./deploy/make-deb-agent.sh
scp deploy/oob-agent_0.1.0_amd64.deb USER@VM_IP:/tmp/
```

Cài trên VM:

```bash
sudo apt update
sudo dpkg -i /tmp/oob-agent_0.1.0_amd64.deb
sudo apt install -f -y
```

Sửa config Agent:

```bash
sudo nano /etc/oob-agent/config.json
```

Đây cũng là đường dẫn mặc định của `oob-agent` nếu chạy service hoặc chạy binary
mà không truyền `--config`.

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

Tạo thư mục watch và chạy service:

```bash
sudo mkdir -p /tmp/demo-prod-app/config
sudo systemctl restart oob-agent
sudo systemctl status oob-agent
sudo journalctl -u oob-agent -f
```

Nếu dùng eBPF thành công, log sẽ có dạng:

```text
[Agent] Backend: eBPF (bpftrace)
[eBPF] Probes attached, monitoring active
```

Nếu môi trường eBPF/bpftrace không ổn định, có thể chuyển sang `inotify` để demo
chức năng phát hiện drift trước.

## 10. Luồng Demo Chính

### 10.1 Unauthorized Drift

Trên VM:

```bash
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

Nếu bật SMTP/Jenkins remediation, Central cũng gửi email alert và trigger Jenkins
để restore.

### 10.2 Authorized Change Bằng Deploy Window

Mở Deploy Window trên Host:

```bash
curl -s -X POST http://localhost:8080/api/v1/deploy-window \
  -H "Content-Type: application/json" \
  -d '{"action":"OPEN","project":"webapp","server":"prod-server-01","ttl_sec":120}'
```

Sửa file trên VM:

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

### 10.3 Authorized Change Bằng Jenkins

Trong luồng đầy đủ, Jenkins mở Deploy Window trước khi chạy Ansible và đóng
window ở bước `post { always }`. Các thay đổi file do Ansible tạo ra sẽ được
Central phân loại là `AUTHORIZED_CHANGE`.

Pipeline mẫu nằm ở:

```text
mock/Jenkinsfile
```

## 11. Elasticsearch Và Grafana

Bật Elasticsearch trong `mock/central-config.json`:

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

Kiểm tra dữ liệu:

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

Grafana không đọc trực tiếp `/tmp/oob-audit.log`; Grafana đọc dữ liệu từ
Elasticsearch.

## 12. SMTP Email Alert

Bật SMTP trong `mock/central-config.json`:

```json
"smtp": {
  "host": "smtp.gmail.com",
  "port": 587,
  "user": "your-sender@gmail.com",
  "pass": "YOUR_GMAIL_APP_PASSWORD",
  "from": "your-sender@gmail.com",
  "to": "admin-alert@gmail.com"
}
```

Central dùng `SmtpNotifier`, chạy Python 3 `smtplib` qua `QProcess` và
`QTemporaryFile`. Cách này giúp tránh để SMTP password xuất hiện trực tiếp trong
danh sách process.

Nếu dùng Gmail SMTP, cần dùng Gmail App Password thay vì mật khẩu đăng nhập
thông thường.

## 13. Jenkins Auto-Remediation

Khi bật remediation, luồng xử lý là:

```text
UNAUTHORIZED_DRIFT
  -> Central gọi Jenkins /job/{project}/buildWithParameters
  -> Jenkins mở Deploy Window
  -> Jenkins gọi ansible-playbook
  -> Ansible restore file trên VM
  -> Jenkins đóng Deploy Window
  -> Event restore được phân loại AUTHORIZED_CHANGE
```

Điều kiện cần:

- Jenkins job name nên khớp với `project` trong Agent config.
  - Ví dụ: `project = webapp` thì Jenkins job nên tên `webapp`.
- Jenkins job có các parameter:
  - `VM_IP`
  - `VM_USER`
  - `OOB_SERVER_NAME`
- Jenkins credential SSH có ID:
  - `vm-deploy-key`
- Jenkins node/container chạy được `ansible-playbook`.
- Jenkins job cần lấy source từ SCM hoặc workspace phải có file
  `ansible/deploy-webapp.yml`.
- Nếu Jenkins chạy trong Docker và Central chạy trên host, `CENTRAL_URL` trong
  `mock/Jenkinsfile` đang dùng `http://172.17.0.1:8080`. Nếu môi trường mạng
  khác Docker bridge mặc định, đổi URL này cho đúng host Central.

Playbook `ansible/deploy-webapp.yml` hiện restore các file demo trong
`/tmp/demo-prod-app`:

- `config/app.json`
- `config/db.conf`
- `scripts/healthcheck.sh`

Cooldown remediation hiện là 30 giây theo project để tránh trigger Jenkins liên
tục khi một lần drift tạo nhiều event.

## 14. Test Và Đo Lường

Các nhóm test chính:

| Nhóm | Mục tiêu |
|---|---|
| Smoke test | Build project và kiểm tra `/health` |
| Functional test | Agent bắt CREATE/MODIFY/DELETE/ATTRIB |
| Decision test | Phân loại đúng `AUTHORIZED_CHANGE` và `UNAUTHORIZED_DRIFT` |
| Audit/alert test | Ghi JSON Lines, gửi alert, trigger remediation |
| Reliability test | Agent retry khi Central tạm dừng |
| Non-functional test | Đo latency, CPU, RSS, false positive, chịu tải ghi file đồng thời |

Đo latency:

```bash
WATCH_DIR=/tmp/demo-prod-app AUDIT_LOG=/tmp/oob-audit.log N=5 \
  ./scripts/measure-latency.sh
```

Pass khi `Max < 60000 ms`.

Đo tài nguyên Agent:

```bash
sudo apt install -y sysstat
pidstat -u -r -p $(pgrep -x oob-agent | head -1) 1 60
```

Stress test ghi file đồng thời:

```bash
THREADS=10 MILESTONES="100 200 500" \
  WATCH_DIR=/tmp/demo-prod-app AUDIT_LOG=/tmp/oob-audit.log \
  ./scripts/stress-file-writes.sh
```

Kịch bản này tạo 10 worker ghi file đồng thời vào thư mục Agent đang watch. Mỗi
mốc sẽ ghi lần lượt 100, 200 và 500 file, sau đó báo:

- thời gian hoàn thành workload;
- throughput theo files/second;
- số event xuất hiện trong audit log;
- CPU peak và RSS peak của `oob-agent` trong lúc chịu tải.

Kết quả đo mẫu trên mô hình Host + VM:

| Workload | Elapsed | Throughput | CPU peak Agent | RSS peak Agent | Audit trên Central |
|---|---:|---:|---:|---:|---:|
| 100 file / 10 thread | 431 ms | 232.02 files/s | 0.20% | 15472 KB | Chưa đối chiếu trên Host |
| 200 file / 10 thread | 497 ms | 402.41 files/s | 0.30% | 15540 KB | Chưa đối chiếu trên Host |
| 500 file / 10 thread | 2238 ms | 223.41 files/s | 0.50% | 15880 KB | 500/500 event |

Với mô hình Host + VM, script chạy trên VM có thể báo `Audit: 0/N (WARN)` nếu
không đọc trực tiếp được audit log của Central trên Host. Khi đó kiểm tra event
thật trên Host bằng `grep` theo `Run id`, ví dụ:

```bash
grep -c "stress-500-1783308223-6322" /tmp/oob-audit.log
```

Kết quả `500` nghĩa là Central đã ghi nhận đủ 500/500 event cho mốc 500 file.

Mục tiêu theo NFR:

- CPU Agent < 2%.
- RSS memory < 50 MB.
- Log/alert xuất hiện trong < 60 giây.
- Không có `UNAUTHORIZED_DRIFT` khi deploy hợp lệ qua Jenkins/Deploy Window.
- Với stress test mini, Agent không crash và audit log nhận được phần lớn event
  trong thời gian quan sát.

## 15. Đóng Gói Và Cài Dịch Vụ

Đóng gói Agent:

```bash
./deploy/make-deb-agent.sh
```

Đóng gói Central:

```bash
./deploy/make-deb-central.sh
```

Cài package:

```bash
sudo dpkg -i deploy/oob-agent_0.1.0_amd64.deb
sudo dpkg -i deploy/oob-central_0.1.0_amd64.deb
sudo apt install -f -y
```

Quản lý service:

```bash
sudo systemctl restart oob-agent
sudo systemctl restart oob-central
sudo journalctl -u oob-agent -f
sudo journalctl -u oob-central -f
```

Config mặc định:

```text
/etc/oob-agent/config.json
/etc/oob-central/config.json
```

## 16. Bảo Mật Và Lưu Ý Vận Hành

- Không commit `mock/central-config.json` vì có thể chứa Jenkins token và SMTP
  password.
- Chỉ commit `mock/central-config.json.example`.
- Nên dùng Jenkins API token thay vì mật khẩu đăng nhập web.
- Nếu dùng HTTPS self-signed cho Jenkins, cấu hình `ssl_verify` phù hợp trong
  môi trường demo.
- `central_url` có thể dùng `https://...` nếu Central được đặt sau TLS/reverse
  proxy.
- Deploy Window có TTL tối đa để tránh window mở vô hạn.
- Audit log local vẫn giữ được dữ liệu khi Elasticsearch tạm thời chưa bật.

## 17. Giới Hạn Hiện Tại Và Hướng Phát Triển

Giới hạn hiện tại:

- Queue retry của Agent đang ở memory, chưa persist xuống disk.
- Chưa có mutual TLS/authentication bắt buộc giữa Agent và Central.
- Chưa có RBAC/API auth cho Central.
- Test chủ yếu là manual/acceptance script, chưa phải test automation đầy đủ.
- eBPF phụ thuộc kernel, quyền root và phiên bản `bpftrace` của môi trường chạy.

Hướng phát triển:

- Persist local buffer để không mất event khi Agent restart.
- Bổ sung TLS/mTLS và API token cho Agent-Central.
- Thêm dashboard JSON mẫu cho Grafana.
- Thêm unit/integration test tự động.
- Mở rộng remediation policy theo project/server.
- Bổ sung cơ chế agent inventory và trạng thái online/offline trên Central.

## 18. Tóm Tắt Cho Báo Cáo

Out-of-Band Deployment Monitor là một hệ thống DevSecOps phát hiện thay đổi file
ngoài luồng CI/CD trên production server. Agent C++/Qt giám sát filesystem bằng
inotify hoặc eBPF/bpftrace và gửi event về Central. Central C++/Qt expose REST
API bằng `cpp-httplib`, đối chiếu Deploy Window/Jenkins để phân loại event thành
`AUTHORIZED_CHANGE` hoặc `UNAUTHORIZED_DRIFT`, ghi audit log JSON Lines, đẩy dữ
liệu sang Elasticsearch/Grafana, gửi email qua SMTP và có thể trigger
Jenkins/Ansible để tự khôi phục khi phát hiện drift trái phép.
