# Bảng Yêu Cầu Hệ Thống

## 1. Yêu cầu chức năng bắt buộc (Mandatory)

| ID | Tên | Mô tả | Tiêu chí chấp nhận | Module |
|---|---|---|---|---|
| FR-01 | Giám sát file system | Agent phát hiện mọi thay đổi file trong thư mục project | Bắt được CREATE, MODIFY, DELETE, ATTRIB, MOVED_FROM, MOVED_TO trong thư mục và tất cả subdirectory | Agent / FileWatcher |
| FR-02 | Capture thông tin user | Mỗi event phải có uid/username của người thực hiện thay đổi | Event JSON phải có trường `uid` và `username`; nếu không lấy được ghi rõ lý do | Agent / FileWatcher |
| FR-03 | Gửi event về Central | Agent đóng gói event thành JSON và gửi HTTP POST | Central Service nhận được event trong vòng < 5 giây kể từ khi file thay đổi | Agent / EventReporter |
| FR-04 | Retry khi mất kết nối | Agent không được mất event khi Central tạm thời không khả dụng | Buffer local trong memory, tự động gửi lại khi Central online trở lại | Agent / LocalBuffer |
| FR-05 | Nhận event từ Agent | Central Service expose REST endpoint nhận event | POST /api/v1/events trả về 200 OK khi nhận thành công | Central / AgentListener |
| FR-06 | Quản lý Deploy Window | Central nhận thông báo từ Jenkins, mở/đóng cửa sổ deploy hợp lệ | Deploy Window được mở khi nhận OPEN và tự động hết hạn sau TTL | Central / DeployWindowManager |
| FR-07 | Phân loại sự kiện | Central phân biệt thay đổi hợp lệ và trái phép | Thay đổi trong Deploy Window → AUTHORIZED_CHANGE; ngoài → UNAUTHORIZED_DRIFT | Central / DecisionEngine |
| FR-08 | Ghi audit log | Mọi event phải được ghi thành JSON Lines | Log có đủ: server, project, path, event_type, timestamp, username, classification | Central / AuditLogger |
| FR-09 | Cảnh báo vi phạm | Khi phát hiện UNAUTHORIZED_DRIFT phải bắn alert | Alert được gửi trong vòng < 60 giây kể từ khi file thay đổi | Central / AlertManager |

## 2. Yêu cầu chức năng khuyến khích (Recommended)

| ID | Tên | Mô tả | Module |
|---|---|---|---|
| FR-10 | eBPF monitoring | Thay thế inotify bằng eBPF để giám sát ở tầng kernel | Agent / EbpfWatcher |
| FR-11 | Grafana Dashboard | Dashboard hiển thị Configuration Drift theo thời gian, đánh dấu server vi phạm | Grafana / dashboard.json |
| FR-12 | Auto-Remediation | Khi phát hiện UNAUTHORIZED_DRIFT, gọi Jenkins API để re-trigger pipeline deploy, Ansible khôi phục lại trạng thái đúng | Central / JenkinsRemediator |
| FR-13 | Push Elasticsearch | Central gọi trực tiếp ES REST API thay vì dùng Filebeat | Central / ElasticsearchClient |
| FR-14 | Đóng gói Linux service | Agent đóng gói thành systemd service, phân phối qua RPM/DEB | deploy/ |

## 3. Yêu cầu phi chức năng (Non-functional)

| ID | Tên | Yêu cầu | Cách đo |
|---|---|---|---|
| NFR-01 | Độ trễ phát hiện | Thời gian từ lúc file bị sửa đến lúc có log/alert < 60 giây | Đo bằng timestamp: event_time vs detected_at trong audit log |
| NFR-02 | Tài nguyên Agent | CPU < 2% và RSS memory < 50 MB trong điều kiện tải bình thường | `pidstat -u -r -p $(pgrep shadow-agent) 1 60` |
| NFR-03 | Không cảnh báo nhầm | Zero false positive khi Ansible đang deploy hợp lệ | Chạy test case UC-01, xác nhận không có UNAUTHORIZED_DRIFT alert |
| NFR-04 | Độ bền Agent | Agent tự khởi động lại nếu crash | Cấu hình `Restart=always` trong systemd unit |
| NFR-05 | Không mất event | Không mất event trong 5 phút mất kết nối | Test: dừng Central 5 phút, restart, xác nhận event được gửi lại |

## 4. Ràng buộc kỹ thuật

| Hạng mục | Ràng buộc |
|---|---|
| Ngôn ngữ | C++17 hoặc C++20 |
| Framework | Qt 6.x (Core, Network) |
| Build system | CMake 3.16+ |
| OS target | Linux (kernel >= 5.4 cho inotify; >= 5.8 cho eBPF) |
| Giao thức Agent-Central | HTTP/1.1 REST, JSON payload |
| Định dạng log | JSON Lines (một JSON object mỗi dòng) |
| Thư mục demo | /tmp/demo-prod-app/ |

## 5. Phạm vi không bao gồm (Out of Scope)

- Giám sát network traffic (chỉ giám sát file system).
- Hỗ trợ Windows server.
- Tích hợp với CI/CD platform khác ngoài Jenkins (trong phạm vi demo).
- Mutual TLS giữa Agent và Central (đưa vào hướng phát triển).
