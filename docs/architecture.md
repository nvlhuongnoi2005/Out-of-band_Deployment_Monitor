# Kiến Trúc Tổng Thể

## 1. Sơ đồ kiến trúc

```
╔══════════════════════════════════════════════════════════════════╗
║                      PRODUCTION SERVER                           ║
║                                                                  ║
║  ┌────────────────────────────────────────────────────────────┐  ║
║  │                    C++/Qt Agent                            │  ║
║  │                                                            │  ║
║  │  Kernel                                                    │  ║
║  │  ┌──────────────────┐                                      │  ║
║  │  │ inotify (Linux)  │  IN_CREATE, IN_MODIFY,               │  ║
║  │  │ via QSocketNotif.│  IN_DELETE, IN_ATTRIB,               │  ║
║  │  └────────┬─────────┘  IN_MOVED_FROM, IN_MOVED_TO         │  ║
║  │           │                                                │  ║
║  │           ▼                                                │  ║
║  │  ┌────────────────────────────────────────────────┐        │  ║
║  │  │  FileWatcher                                   │        │  ║
║  │  │  - Recursive directory watch                   │        │  ║
║  │  │  - Lookup uid/username từ /proc/[pid]/status   │        │  ║
║  │  │  - Normalize event thành FileEvent struct      │        │  ║
║  │  └──────────────────────┬─────────────────────────┘        │  ║
║  │                         │ FileEvent                        │  ║
║  │                         ▼                                  │  ║
║  │  ┌────────────────────────────────────────────────┐        │  ║
║  │  │  EventQueue (QQueue + QMutex)                  │        │  ║
║  │  │  - Thread-safe buffer                          │        │  ║
║  │  │  - LocalBuffer khi mất kết nối Central         │        │  ║
║  │  └──────────────────────┬─────────────────────────┘        │  ║
║  │                         │                                  │  ║
║  │                         ▼                                  │  ║
║  │  ┌────────────────────────────────────────────────┐        │  ║
║  │  │  EventReporter (QNetworkAccessManager)         │        │  ║
║  │  │  - Serialize JSON (QJsonDocument)              │        │  ║
║  │  │  - HTTP POST /api/v1/events                    │        │  ║
║  │  │  - Retry khi thất bại                          │        │  ║
║  │  │  - Heartbeat 30s                               │        │  ║
║  │  └──────────────────────┬─────────────────────────┘        │  ║
║  └─────────────────────────│──────────────────────────────────┘  ║
║                            │                                     ║
║  /tmp/demo-prod-app/       │ HTTPS  POST /api/v1/events          ║
║  (monitored directory)     │                                     ║
╚════════════════════════════│═════════════════════════════════════╝
                             │
                             ▼
╔══════════════════════════════════════════════════════════════════╗
║              Central Reconciliation Service (C++/Qt)             ║
║                                                                  ║
║  ┌─────────────────┐     ┌──────────────────────────────────┐   ║
║  │  AgentListener  │────▶│         DecisionEngine           │   ║
║  │  (HTTP Server)  │     │                                  │   ║
║  │                 │     │  1. Có Deploy Window không?      │   ║
║  │  POST /events   │     │         │                        │   ║
║  │  POST /deploy-  │     │    YES──┤──NO                    │   ║
║  │       window    │     │         │    │                   │   ║
║  └─────────────────┘     │   AUTHOR│    ▼                   │   ║
║                          │   IZED  │  Gọi JenkinsClient     │   ║
║  ┌─────────────────┐     │         │  xác nhận thêm         │   ║
║  │ DeployWindow    │◀────│         │    │                   │   ║
║  │ Manager         │     │         │  UNAUTHORIZED_DRIFT    │   ║
║  │ (QMap +         │     └─────────┼────────────────────────┘   ║
║  │  QTimer TTL)    │               │                            ║
║  └─────────────────┘      ┌────────┴──────────┐                 ║
║                           │                   │                 ║
║                      AUTHORIZED          UNAUTHORIZED           ║
║                           │                   │                 ║
║                           ▼                   ▼                 ║
║                    ┌─────────────┐    ┌───────────────────┐     ║
║                    │ AuditLogger │    │   AlertManager    │     ║
║                    │ JSON Lines  │    │ + AuditLogger     │     ║
║                    │ → Filebeat  │    │ + AnsibleTrigger  │     ║
║                    │ → ES        │    │   (optional)      │     ║
║                    └─────────────┘    └───────────────────┘     ║
╚══════════════════════════════════════════════════════════════════╝
          │                    │                    │
          ▼                    ▼                    ▼
   ┌─────────────┐   ┌──────────────────┐  ┌──────────────────┐
   │ Jenkins API │   │  Elasticsearch   │  │  Ansible         │
   │ (hoặc mock) │   │  (EFK Stack)     │  │  (remediation)   │
   └─────────────┘   └────────┬─────────┘  └──────────────────┘
                              │
                              ▼
                       ┌─────────────┐
                       │   Grafana   │
                       │  Dashboard  │
                       │ (Drift KPI) │
                       └─────────────┘
```

## 2. Thành phần hệ thống

### Agent (chạy trên mỗi production server)

| Module | Công nghệ | Nhiệm vụ |
|---|---|---|
| FileWatcher | inotify + QSocketNotifier | Bắt sự kiện file system |
| EventQueue | QQueue + QMutex | Buffer thread-safe |
| EventReporter | QNetworkAccessManager | Gửi HTTP POST |
| LocalBuffer | QQueue (in-memory) | Lưu tạm khi offline |
| Heartbeat | QTimer | Ping Central mỗi 30 giây |

### Central Reconciliation Service

| Module | Công nghệ | Nhiệm vụ |
|---|---|---|
| AgentListener | QHttpServer / cpp-httplib | Nhận event từ Agent |
| DeployWindowManager | QMap + QTimer | Quản lý cửa sổ deploy hợp lệ |
| JenkinsClient | QNetworkAccessManager | Gọi Jenkins REST API |
| DecisionEngine | C++ logic | Phân loại AUTHORIZED / UNAUTHORIZED |
| AuditLogger | QFile (JSON Lines) | Ghi audit log |
| AlertManager | HTTP / webhook | Bắn alert khi có vi phạm |
| AnsibleTrigger | QProcess | Trigger Ansible playbook (optional) |

## 3. Luồng dữ liệu chính

### Luồng A — Deploy hợp lệ (không alert)

```
Jenkins START job
    → POST /api/v1/deploy-window {action: "OPEN", server, project, valid_until}
    → Central mở Deploy Window
    → Ansible thay đổi file
    → Agent phát hiện → gửi event
    → Central: trong Deploy Window? YES → AUTHORIZED_CHANGE → log only
Jenkins END job
    → POST /api/v1/deploy-window {action: "CLOSE"}
    → Central đóng Deploy Window
```

### Luồng B — Shadow Deployment (có alert)

```
User SSH vào server → sửa/xóa/chmod file
    → inotify báo event
    → Agent: lấy uid/username từ /proc → gửi event
    → Central: Deploy Window? NO → UNAUTHORIZED_DRIFT
    → Ghi audit log JSON
    → Bắn alert
    → Optional: trigger Ansible restore
    ← Toàn bộ trong < 60 giây
```

## 4. Quyết định công nghệ

| Quyết định | Lựa chọn | Lý do |
|---|---|---|
| File system monitoring | inotify (mandatory) → eBPF (optional) | inotify: đủ dùng, ít phức tạp; eBPF: không bị bypass |
| Agent-Central transport | HTTP/JSON (REST) | Dễ debug, tương thích Jenkins/ES |
| HTTP server (Central) | cpp-httplib (header-only) | Không phụ thuộc Qt version |
| Event serialization | QJsonDocument | Sẵn có trong Qt Core |
| Audit storage | JSON Lines file → Filebeat → ES | Đơn giản, dễ mở rộng |
| Alert | HTTP webhook | Tích hợp bất kỳ monitoring system |
