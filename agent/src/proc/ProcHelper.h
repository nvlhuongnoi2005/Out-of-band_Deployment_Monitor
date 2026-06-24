#pragma once

#include <QString>
#include <sys/types.h>

// Utility namespace — tra cứu thông tin process/user từ /proc và stat
// Tách khỏi InotifyWatcher để EbpfWatcher (sau này) cũng dùng lại được
namespace ProcHelper {

// Lấy UID của file owner qua stat()
uid_t statUid(const QString &path);

// Tìm PID của process đang giữ file mở (quét /proc/[pid]/fd/)
// Trả về -1 nếu không tìm thấy
int findPidByOpenFile(const QString &path);

// Lấy UID thực (real UID) của một process từ /proc/[pid]/status
uid_t uidOfPid(int pid);

// Tra tên user từ UID qua /etc/passwd (thread-safe với getpwuid_r)
QString uidToUsername(uid_t uid);

// Lấy tên process từ /proc/[pid]/comm
QString pidToProcessName(int pid);

} // namespace ProcHelper
