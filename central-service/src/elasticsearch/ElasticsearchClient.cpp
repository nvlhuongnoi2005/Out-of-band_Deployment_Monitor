#include "ElasticsearchClient.h"

#include <QProcess>
#include <QTemporaryFile>
#include <QDebug>

ElasticsearchClient::ElasticsearchClient(const QString &host, int port,
                                         const QString &index,
                                         const QString &username,
                                         const QString &password,
                                         bool           useHttps)
    : m_host(host), m_port(port), m_index(index)
    , m_username(username), m_password(password)
    , m_useHttps(useHttps)
{}

bool ElasticsearchClient::index(const QString &jsonDoc)
{
    const QString scheme = m_useHttps ? "https" : "http";
    const QString url    = QString("%1://%2:%3/%4/_doc")
                           .arg(scheme, m_host).arg(m_port).arg(m_index);

    // Write body to temp file — avoids shell escaping issues with JSON content
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    if (!tmp.open()) {
        qWarning() << "[ES] Cannot create temp file";
        return false;
    }
    tmp.write(jsonDoc.toUtf8());
    tmp.flush();

    QStringList args = {
        "-s", "-o", "/dev/null", "-w", "%{http_code}",
        "-X", "POST",
        "-H", "Content-Type: application/json",
        "-d", "@" + tmp.fileName(),
        url
    };

    if (!m_username.isEmpty())
        args << "-u" << (m_username + ":" + m_password);

    QProcess proc;
    proc.start("curl", args);

    if (!proc.waitForFinished(5000)) {
        qWarning() << "[ES] curl timeout";
        proc.kill();
        return false;
    }

    const QString statusCode = proc.readAllStandardOutput().trimmed();
    if (statusCode != "200" && statusCode != "201") {
        qWarning() << "[ES] Index failed — HTTP" << statusCode;
        return false;
    }

    qDebug() << "[ES] Indexed OK (HTTP" << statusCode << ")";
    return true;
}
