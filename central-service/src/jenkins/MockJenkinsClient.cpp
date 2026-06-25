#include "MockJenkinsClient.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

MockJenkinsClient::MockJenkinsClient(const QString &stateFilePath)
    : m_stateFilePath(stateFilePath)
{}

bool MockJenkinsClient::isDeployRunning(const QString &project, const QString &server)
{
    QFile f(m_stateFilePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[Jenkins] Cannot read state file:" << m_stateFilePath
                   << "— treating as no active deploy";
        return false;
    }

    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "[Jenkins] Invalid JSON in state file";
        return false;
    }

    const QJsonArray jobs = doc.object().value("active_deployments").toArray();
    for (const QJsonValue &v : jobs) {
        QJsonObject job = v.toObject();
        // Match on project AND server (both must match)
        if (job["project"].toString() == project &&
            job["server"].toString()  == server)
        {
            qInfo() << "[Jenkins] Active deploy found for project=" + project
                    << "server=" + server;
            return true;
        }
    }
    return false;
}
