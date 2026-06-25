#include "DeployWindowManager.h"

#include <QMutexLocker>
#include <QDebug>

QString DeployWindowManager::key(const QString &project, const QString &server)
{
    return project + ":" + server;
}

void DeployWindowManager::open(const QString &project, const QString &server, int ttlSec)
{
    QMutexLocker locker(&m_mutex);
    const QDateTime expiry = QDateTime::currentDateTime().addSecs(ttlSec);
    m_windows[key(project, server)] = expiry;
    qInfo() << "[Window] OPEN project=" + project
            << "server=" + server
            << "expires=" + expiry.toString(Qt::ISODate);
}

void DeployWindowManager::close(const QString &project, const QString &server)
{
    QMutexLocker locker(&m_mutex);
    const QString k = key(project, server);
    if (m_windows.remove(k))
        qInfo() << "[Window] CLOSE project=" + project << "server=" + server;
}

bool DeployWindowManager::isOpen(const QString &project, const QString &server)
{
    QMutexLocker locker(&m_mutex);
    const QString k = key(project, server);
    if (!m_windows.contains(k))
        return false;

    if (QDateTime::currentDateTime() > m_windows[k]) {
        m_windows.remove(k);
        qInfo() << "[Window] EXPIRED project=" + project << "server=" + server;
        return false;
    }
    return true;
}
