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
    QFile f(m_logPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[Audit] Cannot open log file:" << m_logPath;
        return false;
    }
    f.write((jsonLine + "\n").toUtf8());
    return true;
}
