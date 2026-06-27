#include "HttpJenkinsClient.h"

#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

HttpJenkinsClient::HttpJenkinsClient(const JenkinsConfig &config)
    : m_config(config)
{}

bool HttpJenkinsClient::isDeployRunning(const QString &project, const QString &server)
{
    Q_UNUSED(server) // Jenkins jobs are per-project; server matching via job name convention

    // GET /api/json?tree=jobs[name,color]
    // curl handles both http:// and https:// transparently.
    const QString apiUrl = m_config.url + "/api/json?tree=jobs[name,color]";

    QStringList args = {
        "-s",
        "-u", m_config.username + ":" + m_config.apiToken,
        apiUrl
    };

    if (!m_config.sslVerify)
        args << "--insecure";

    QProcess proc;
    proc.start("curl", args);

    if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
        if (m_config.failOpen) {
            qWarning() << "[Jenkins] API unreachable — fail_open=true, treating as AUTHORIZED"
                       << "(deploy may be running)";
            return true;
        }
        qCritical() << "[Jenkins] API unreachable (curl exit=" << proc.exitCode()
                    << ") — fail_open=false, may generate false UNAUTHORIZED alerts!";
        return false;
    }

    const auto doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (doc.isNull()) {
        if (m_config.failOpen) {
            qWarning() << "[Jenkins] Malformed API response — fail_open=true, treating as AUTHORIZED";
            return true;
        }
        qCritical() << "[Jenkins] Malformed API response — fail_open=false, may generate false alerts!";
        return false;
    }

    for (const QJsonValue &v : doc.object()["jobs"].toArray()) {
        const QJsonObject job   = v.toObject();
        const QString     name  = job["name"].toString();
        const QString     color = job["color"].toString();

        // Strict matching: exact name OR name starts with project + "-"
        // e.g. project="webapp" matches "webapp" or "webapp-deploy" but NOT "my-webapp"
        const bool nameMatches = (name.compare(project, Qt::CaseInsensitive) == 0)
                              || name.startsWith(project + "-", Qt::CaseInsensitive);

        if (nameMatches && color.endsWith("_anime")) {
            qInfo() << "[Jenkins] Active deploy found:" << name;
            return true;
        }
    }
    return false;
}
