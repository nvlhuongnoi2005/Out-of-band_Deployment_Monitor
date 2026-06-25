#pragma once

#include <QString>
#include <QMutex>

// Thread-safe JSON Lines writer. Each write() appends one line to the log file.
// httplib handler threads call this concurrently, so mutex is required.
class AuditLogger {
public:
    explicit AuditLogger(const QString &logPath);
    bool write(const QString &jsonLine);
    QString logPath() const { return m_logPath; }

private:
    QString m_logPath;
    QMutex  m_mutex;
};
