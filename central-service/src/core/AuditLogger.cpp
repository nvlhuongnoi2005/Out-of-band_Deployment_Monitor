#include "AuditLogger.h"

#include <QFile>
#include <QMutexLocker>
#include <QDebug>

AuditLogger::AuditLogger(const QString &logPath)
    : m_logPath(logPath)
{}

bool AuditLogger::write(const QString &jsonLine)
{
    QMutexLocker locker(&m_mutex);
    // Open and close on every write so logrotate's copytruncate can safely
    // truncate the file without us holding a stale fd pointing at the old inode.
    QFile f(m_logPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[Audit] Cannot open log file:" << m_logPath;
        return false;
    }
    f.write((jsonLine + "\n").toUtf8());
    return true;
}
