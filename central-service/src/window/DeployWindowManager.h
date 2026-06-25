#pragma once

#include <QString>
#include <QMap>
#include <QDateTime>
#include <QMutex>

// Tracks active deploy windows per (project, server) pair.
// Ansible/Jenkins signals OPEN before deploying and CLOSE after.
// While a window is open, file changes are AUTHORIZED — not shadow deployment.
// Windows also expire automatically via TTL to prevent stale open windows.
class DeployWindowManager {
public:
    // Open a deploy window. ttlSec = max duration before auto-expiry.
    void open(const QString &project, const QString &server, int ttlSec = 300);

    // Close a deploy window early (Ansible signaled completion).
    void close(const QString &project, const QString &server);

    // Returns true if a non-expired window exists for this project+server.
    bool isOpen(const QString &project, const QString &server);

private:
    static QString key(const QString &project, const QString &server);

    QMutex              m_mutex;
    QMap<QString, QDateTime> m_windows; // key → expiry time
};
