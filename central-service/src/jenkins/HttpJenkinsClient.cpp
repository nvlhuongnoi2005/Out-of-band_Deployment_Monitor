#include "HttpJenkinsClient.h"

#include <httplib.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>

HttpJenkinsClient::HttpJenkinsClient(const QString &baseUrl,
                                     const QString &username,
                                     const QString &apiToken)
    : m_baseUrl(baseUrl), m_username(username), m_apiToken(apiToken)
{}

bool HttpJenkinsClient::isDeployRunning(const QString &project, const QString &server)
{
    Q_UNUSED(server) // Jenkins jobs are per-project; server matching via job name convention

    const QUrl url(m_baseUrl);
    httplib::Client cli(url.host().toStdString(), url.port(8080));
    cli.set_connection_timeout(3);
    cli.set_read_timeout(3);
    cli.set_basic_auth(m_username.toStdString(), m_apiToken.toStdString());

    // GET /api/json?tree=jobs[name,color] — list all jobs with status
    auto res = cli.Get("/api/json?tree=jobs[name,color]");
    if (!res || res->status != 200) {
        // KNOWN LIMITATION: treating unreachable Jenkins as "no deploy running".
        // This may cause false UNAUTHORIZED_DRIFT alerts when Jenkins is down.
        qCritical() << "[Jenkins] API unreachable (HTTP"
                    << (res ? res->status : 0)
                    << ") — false UNAUTHORIZED alerts are possible!";
        return false;
    }

    const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(res->body));
    if (doc.isNull()) return false;

    for (const QJsonValue &v : doc.object()["jobs"].toArray()) {
        const QJsonObject job  = v.toObject();
        const QString    name  = job["name"].toString();
        const QString    color = job["color"].toString();

        if (name.contains(project, Qt::CaseInsensitive) && color.endsWith("_anime")) {
            qInfo() << "[Jenkins] Active deploy found:" << name;
            return true;
        }
    }
    return false;
}
