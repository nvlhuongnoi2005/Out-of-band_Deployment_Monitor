# Kiến Trúc Hệ Thống — Out-of-Band Deployment Monitor

## 1. Tổng quan

Hệ thống phát hiện **Shadow Deployment** — các thay đổi trên production server xảy ra ngoài luồng CI/CD (Jenkins + Ansible). Gồm hai tiến trình độc lập viết bằng C++17 + Qt 6:

- **oob-agent** — chạy trên từng production server, theo dõi filesystem
- **oob-central** — dịch vụ trung tâm, phân loại sự kiện và phản ứng

---

## 2. Sơ đồ kiến trúc

```
┌─────────────────────────────────────────────────────────────────────┐
│                     PRODUCTION SERVER                               │
│                                                                     │
│  /opt/webapp/  (watched directory)                                  │
│       │                                                             │
│       │  kernel events                                              │
│       ▼                                                             │
│  ┌──────────────────────────────────────────────┐                  │
│  │           IFileWatcher  (Strategy)           │                  │
│  │                                              │                  │
│  │  ┌─────────────────┐  ┌────────────────────┐ │                  │
│  │  │ InotifyWatcher  │  │   EbpfWatcher      │ │                  │
│  │  │ (default)       │  │ (--watcher ebpf)   │ │                  │
│  │  │                 │  │                    │ │                  │
│  │  │ inotify syscall │  │ bpftrace subprocess│ │                  │
│  │  │ + QSocketNotif. │  │ eBPF tracepoints   │ │                  │
│  │  │ /proc lookup    │  │ uid/pid từ kernel  │ │                  │
│  │  └─────────────────┘  └────────────────────┘ │                  │
│  └──────────────────────┬───────────────────────┘                  │
│                         │ FileEvent{path, type, uid, pid, comm}    │
│                         ▼                                          │
│  ┌──────────────────────────────────────────────┐                  │
│  │             Agent (QObject)                  │                  │
│  │  - Nhận FileEvent từ watcher                 │                  │
│  │  - Forward sang EventReporter                │                  │
│  │  - Quản lý heartbeat timer (30s)             │                  │
│  └──────────────────────┬───────────────────────┘                  │
│                         │                                          │
│  ┌──────────────────────▼───────────────────────┐                  │
│  │          EventReporter                       │                  │
│  │  - QQueue<FileEvent>  (in-process buffer)    │                  │
│  │  - QNetworkAccessManager → HTTP/HTTPS POST   │                  │
│  │  - Retry tự động (QTimer, mặc định 5s)       │                  │
│  │  - Heartbeat → POST /api/v1/heartbeat        │                  │
│  └──────────────────────────────────────────────┘                  │
│                         │  HTTPS POST /api/v1/events               │
└─────────────────────────│───────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   CENTRAL SERVICE  (oob-central)                    │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  HTTP Server  (cpp-httplib, synchronous)                     │   │
│  │  POST /api/v1/events  ·  POST /api/v1/deploy-window          │   │
│  │  POST /api/v1/heartbeat  ·  GET /health                      │   │
│  └───────────────────────┬──────────────────────────────────────┘   │
│                          │                                          │
│                          ▼                                          │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │               DecisionEngine                                 │   │
│  │                                                              │   │
│  │   1. DeployWindowManager.isOpen(project, server)?            │   │
│  │          YES ──────────────────────────► AUTHORIZED_CHANGE   │   │
│  │          NO                                                  │   │
│  │           │                                                  │   │
│  │   2. IJenkinsClient.isDeployRunning(project)?                │   │
│  │          YES ──────────────────────────► AUTHORIZED_CHANGE   │   │
│  │          NO                                                  │   │
│  │           │                                                  │   │
│  │           └───────────────────────────► UNAUTHORIZED_DRIFT   │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  ┌──────────────────┐    ┌────────────────────────────────────────┐ │
│  │ DeployWindow     │    │ IJenkinsClient  (Strategy)             │ │
│  │ Manager          │    │                                        │ │
│  │                  │    │ ┌──────────────────────────────────┐   │ │
│  │ QMap<key, expiry>│    │ │ HttpJenkinsClient                │   │ │
│  │ QMutex           │    │ │ curl subprocess, HTTP/HTTPS      │   │ │
│  │ TTL: valid_until │    │ │ color.endsWith("_anime") = busy  │   │ │
│  │ hoặc ttl_sec     │    │ └──────────────────────────────────┘   │ │
│  │ max 86400s       │    │ ┌──────────────────────────────────┐   │ │
│  └──────────────────┘    │ │ MockJenkinsClient                │   │ │
│                          │ │ đọc JSON file (dev/test)         │   │ │
│                          │ └──────────────────────────────────┘   │ │
│                          └────────────────────────────────────────┘ │
│                                                                     │
│          Khi UNAUTHORIZED_DRIFT                                     │
│          ┌───────────────────────────────────────────────────┐      │
│          │                                                   │      │
│          ▼                   ▼                    ▼          │      │
│  ┌──────────────┐   ┌─────────────────┐  ┌──────────────┐   │      │
│  │ AuditLogger  │   │  SmtpNotifier   │  │  Jenkins     │   │      │
│  │              │   │                 │  │  Remediator  │   │      │
│  │ JSON Lines   │   │ Python3 smtplib │  │              │   │      │
│  │ /var/log/    │   │ QTemporaryFile  │  │ curl HTTPS   │   │      │
│  │ oob-audit.log│   │ (creds không   │  │ POST /job/   │   │      │
│  │ QMutex       │   │  hiện trên ps) │  │ {proj}/build │   │      │
│  └──────┬───────┘   └─────────────────┘  └──────┬───────┘   │      │
│         │                                        │           │      │
│         ▼                                        │           │      │
│  ┌──────────────┐                                │           │      │
│  │ Elasticsearch│◄── ElasticsearchClient         │           │      │
│  │ Client       │    (curl subprocess,           │           │      │
│  │              │     HTTP/HTTPS,                │           │      │
│  │              │     direct index API)          │           │      │
│  └──────────────┘                                │           │      │
└─────────────────────────────────────────────────┼───────────┘      
                                                  │
                          ┌───────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    EXTERNAL SYSTEMS                                  │
│                                                                     │
│  ┌──────────────┐  ┌──────────────────┐  ┌────────────────────┐    │
│  │   Jenkins    │  │  Elasticsearch   │  │   Grafana          │    │
│  │              │  │  8.x             │  │   Dashboard        │    │
│  │ Status check │  │  index oob-audit │  │   port 3000        │    │
│  │ + Re-trigger │  │  HTTP/HTTPS      │  │   ES datasource    │    │
│  │   pipeline   │  │                  │  │   Drift KPI        │    │
│  └──────────────┘  └──────────────────┘  └────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Thành phần chi tiết

### 3.1 Agent (`oob-agent`)

| Module | Class | Công nghệ | Nhiệm vụ |
|---|---|---|---|
| Watcher (inotify) | `InotifyWatcher` | inotify + `QSocketNotifier` | Recursive watch, event loop integration |
| Watcher (eBPF) | `EbpfWatcher` | `bpftrace` subprocess | eBPF tracepoints, uid/pid từ kernel |
| Process info | `ProcHelper` | `/proc/[pid]/status`, `/proc/[pid]/cmdline` | uid, username, process name khi dùng inotify |
| Event reporter | `EventReporter` | `QNetworkAccessManager` | Queue + retry + HTTP/HTTPS POST |
| Agent core | `Agent` | `QObject`, `QTimer` | Orchestration + heartbeat 30s |
| Config | `AgentConfig` | `QJsonDocument` | Load `agent-config.json` |

**Shared struct** (`shared/FileEvent.h`):
```cpp
struct FileEvent {
    QString   eventId;    // UUID
    QString   path;       // đường dẫn đầy đủ
    EventType eventType;  // CREATE / MODIFY / DELETE / ATTRIB / MOVED_FROM / MOVED_TO
    QDateTime timestamp;  // UTC, ISO 8601 với Z suffix
    int       uid;        // user id thực hiện thay đổi
    QString   username;   // tên user
    int       pid;        // process id
    QString   processName;// tên tiến trình
};
```

**Watcher backends** (chọn bằng `--watcher`):

| Backend | Điều kiện uid/pid | Yêu cầu |
|---|---|---|
| `inotify` (default) | Từ `/proc` sau khi event → có race condition với short-lived process | Không cần thêm |
| `ebpf` | Từ kernel tại thời điểm syscall → chính xác tuyệt đối | `bpftrace` + root |

### 3.2 Central Service (`oob-central`)

| Module | Class | Công nghệ | Nhiệm vụ |
|---|---|---|---|
| HTTP Server | `CentralService` | `cpp-httplib` (header-only) | REST API, `bind_to_port` + `listen_after_bind` |
| Deploy Window | `DeployWindowManager` | `QMap + QMutex` | TTL-based window, thread-safe |
| Decision | `DecisionEngine` | Chain of Responsibility | Window → Jenkins → UNAUTHORIZED |
| Jenkins (real) | `HttpJenkinsClient` | `curl` subprocess | HTTP/HTTPS, kiểm tra `color` endsWith `_anime` |
| Jenkins (mock) | `MockJenkinsClient` | `QJsonDocument` | Đọc file JSON cho dev/test |
| Audit | `AuditLogger` | `QFile + QMutex` | JSON Lines, mở-đóng mỗi lần write, trả `bool` |
| ES | `ElasticsearchClient` | `curl` subprocess | Direct index API, HTTP/HTTPS |
| Email | `SmtpNotifier` | Python3 `smtplib` + `QTemporaryFile` | Credentials không lộ trên `ps aux` |
| Remediation | `JenkinsRemediator` | `curl` subprocess | Re-trigger Jenkins pipeline qua HTTP/HTTPS |

### 3.3 Interfaces (Strategy Pattern)

```
IFileWatcher          IJenkinsClient        INotifier
     │                     │                   │
     ├── InotifyWatcher     ├── HttpJenkinsClient ├── SmtpNotifier
     └── EbpfWatcher        └── MockJenkinsClient └── (extensible)
```

---

## 4. Luồng dữ liệu

### Luồng A — Deploy hợp lệ (AUTHORIZED_CHANGE)

```
Jenkins START pipeline
    → POST /api/v1/deploy-window {"action":"OPEN","project":"webapp",
                                  "server":"prod-01","valid_until":"..."}
    → DeployWindowManager mở window (TTL tối đa 86400s)

Ansible chạy → thay đổi file trên server
    → InotifyWatcher / EbpfWatcher phát hiện
    → Agent: serialize → POST /api/v1/events
    → Central: DecisionEngine.isOpen() = true → AUTHORIZED_CHANGE
    → AuditLogger ghi JSON Line → ElasticsearchClient đẩy lên ES

Jenkins END pipeline (post { always })
    → POST /api/v1/deploy-window {"action":"CLOSE",...}
    → DeployWindowManager đóng window
```

### Luồng B — Shadow Deployment (UNAUTHORIZED_DRIFT)

```
Attacker / engineer SSH vào server → sửa file ngoài luồng CI/CD
    → InotifyWatcher / EbpfWatcher phát hiện (< 1s)
    → ProcHelper.findPidByOpenFile() → lấy uid, username, process name
    → Agent enqueue FileEvent → POST /api/v1/events

Central nhận event:
    → DecisionEngine: isOpen() = NO → isDeployRunning() = NO
    → Classification = UNAUTHORIZED_DRIFT

    ┌── AuditLogger.write() → /var/log/oob-audit.log (JSON Line)
    │        └── ElasticsearchClient.index() → Elasticsearch
    │
    ├── SmtpNotifier.notify() → email cảnh báo (background subprocess)
    │
    └── JenkinsRemediator.trigger() [nếu --jenkins-remediate]:
            cooldown check (300s per project)
            → curl -X POST https://jenkins/job/{project}/build
            → Jenkins re-chạy pipeline → Ansible deploy lại
            → Jenkinsfile mở Deploy Window trước khi deploy
            → Các file changes từ re-deploy = AUTHORIZED_CHANGE
```

### Luồng C — Heartbeat (Health Check)

```
Agent QTimer mỗi 30s
    → POST /api/v1/heartbeat {"agent_id", "server", "status":"healthy", "timestamp"}
    → Central log info (hook cho future online/offline tracking)
```

---

## 5. Mô hình triển khai

```
┌──────────────────────────────────────┐
│  Production Server (VM hoặc bare metal)
│                                      │
│  oob-agent (systemd service)         │
│  config: /etc/oob-agent/config.json  │
│                                      │
│  Theo dõi: /opt/webapp/ (ví dụ)      │
└──────────────┬───────────────────────┘
               │ HTTPS :8080
               │
┌──────────────▼───────────────────────┐
│  Monitoring Server (có thể cùng máy) │
│                                      │
│  oob-central  :8080 (systemd)        │
│  Elasticsearch :9200 (Docker)        │
│  Grafana       :3000 (Docker)        │
│  Jenkins       :8081 (Docker)        │
└──────────────────────────────────────┘
```

**Log rotation** (`/etc/logrotate.d/oob-audit`):
- Daily rotate, giữ 14 ngày, nén, `copytruncate`

**Packaging**: `./deploy/make-deb-agent.sh` và `./deploy/make-deb-central.sh` → hai gói `.deb` riêng biệt, postinst tự enable systemd units

---

## 6. Các quyết định thiết kế

| Quyết định | Lựa chọn | Lý do |
|---|---|---|
| File monitoring backend | Strategy: inotify (default) / eBPF (bpftrace) | Có thể swap không ảnh hưởng agent logic |
| HTTP server | `cpp-httplib` header-only, `std::thread` | Không phụ thuộc Qt version, đơn giản |
| Transport | HTTP hoặc HTTPS (QNetworkAccessManager) | `central_url` dùng `https://` là đủ |
| Jenkins status check | `curl` subprocess thay `httplib::Client` | curl hỗ trợ HTTPS natively, không cần link OpenSSL |
| Elasticsearch push | `curl` subprocess | Hỗ trợ HTTPS, no extra Qt deps |
| SMTP | Python3 `smtplib` qua `QTemporaryFile` | Credentials không hiện trên `ps aux` |
| Auto-remediation | Gọi Jenkins API re-trigger | Jenkins là source-of-truth; pipeline có credentials và opens deploy window tự động |
| Audit log | JSON Lines file + ES | File: durable ngay cả khi ES down; ES: queryable |
| Deploy window | TTL với `valid_until` (tuyệt đối) hoặc `ttl_sec` (tương đối) | Jenkins dùng `valid_until`; test thủ công dùng `ttl_sec` |
| Idempotency (remediation) | Cooldown map `project → last_trigger_epoch`, 300s | Tránh trigger liên tục khi nhiều events dồn về |

---

## 7. Security considerations

| Điểm | Biện pháp |
|---|---|
| Credentials Jenkins trong memory | Không ghi vào disk, chỉ truyền qua HTTPS |
| SMTP credentials | `QTemporaryFile` script → không hiện trên `ps aux` |
| Elasticsearch credentials | Passed via `--es-user/--es-pass`, không hardcode |
| Self-signed cert Jenkins | `--jenkins-insecure` flag (tắt verify khi cần) |
| Agent → Central TLS | `central_url: "https://..."` trong config.json |
| Deploy window bounded | TTL tối đa 86400s, reject nếu `valid_until` đã qua |
