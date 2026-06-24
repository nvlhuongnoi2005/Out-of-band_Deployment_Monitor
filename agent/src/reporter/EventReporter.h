#pragma once

#include "IEventReporter.h"
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Kế thừa cả QObject (để dùng signal/slot/timer) và IEventReporter (interface)
class EventReporter : public QObject, public IEventReporter
{
    Q_OBJECT
public:
    explicit EventReporter(const QString &centralUrl,
                           int retryIntervalSec = 5,
                           QObject *parent = nullptr);

    void enqueue(const FileEvent &event) override;
    int  pendingCount() const override { return m_queue.size(); }

signals:
    void eventSent(const QString &eventId);
    void sendFailed(const QString &eventId, const QString &reason);

private slots:
    void trySendNext();
    void onReplyFinished(QNetworkReply *reply);

private:
    static QByteArray serialize(const FileEvent &event);

    QString                m_centralUrl;
    int                    m_retryIntervalSec;
    QQueue<FileEvent>      m_queue;
    QNetworkAccessManager *m_manager;
    QTimer                *m_retryTimer;
    bool                   m_isSending = false;
};
