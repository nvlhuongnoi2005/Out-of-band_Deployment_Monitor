#include "EventReporter.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

EventReporter::EventReporter(const QString &centralUrl,
                             int retryIntervalSec,
                             QObject *parent)
    : QObject(parent)
    , m_centralUrl(centralUrl)
    , m_retryIntervalSec(retryIntervalSec)
    , m_manager(new QNetworkAccessManager(this))
    , m_retryTimer(new QTimer(this))
{
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &EventReporter::trySendNext);
    connect(m_manager,    &QNetworkAccessManager::finished,
            this,         &EventReporter::onReplyFinished);
}

void EventReporter::enqueue(const FileEvent &event)
{
    m_queue.enqueue(event);
    qInfo().noquote() << "[EVENT]" << serialize(event);

    if (!m_isSending)
        trySendNext();
}

void EventReporter::trySendNext()
{
    if (m_queue.isEmpty() || m_isSending) return;

    const FileEvent &event = m_queue.head();
    QNetworkRequest request(QUrl(m_centralUrl + "/api/v1/events"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Send one event at a time (m_isSending gate) to preserve arrival order at Central
    // and avoid flooding the queue with concurrent retries.
    m_manager->post(request, serialize(event));
    m_isSending = true;

    qInfo() << "[Reporter] →" << eventTypeToString(event.eventType)
            << event.path;
}

void EventReporter::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    m_isSending = false;

    const FileEvent sent = m_queue.head();
    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300) {
        m_queue.dequeue();
        qInfo() << "[Reporter] Sent OK:" << sent.eventId
                << "| Remaining:" << m_queue.size();
        emit eventSent(sent.eventId);

        if (!m_queue.isEmpty())
            trySendNext();
        return;
    }

    const QString reason = reply->error() != QNetworkReply::NoError
                               ? reply->errorString()
                               : QString("HTTP %1").arg(httpStatus);

    qWarning() << "[Reporter] Failed:" << reason
               << "— retry in" << m_retryIntervalSec << "s";
    emit sendFailed(sent.eventId, reason);
    m_retryTimer->start(m_retryIntervalSec * 1000);
}

QByteArray EventReporter::serialize(const FileEvent &event)
{
    QJsonObject obj;
    obj["event_id"]     = event.eventId;
    obj["agent_id"]     = event.agentId;
    obj["server"]       = event.server;
    obj["project"]      = event.project;
    obj["path"]         = event.path;
    obj["event_type"]   = eventTypeToString(event.eventType);
    obj["timestamp"]    = event.timestamp.toString(Qt::ISODateWithMs);
    obj["uid"]          = event.uid;
    obj["username"]     = event.username;
    obj["pid"]          = event.pid;
    obj["process_name"] = event.processName;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
