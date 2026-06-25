#pragma once

#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include "config/AgentConfig.h"
#include "FileEvent.h"

class IFileWatcher;
class IEventReporter;

class Agent : public QObject
{
    Q_OBJECT
public:
    explicit Agent(IFileWatcher   *watcher,
                   IEventReporter *reporter,
                   const AgentConfig &config,
                   QObject *parent = nullptr);

    bool start();
    void stop();

private slots:
    void onFileEvent(const FileEvent &event);
    void onWatcherError(const QString &message);
    void onHeartbeatTick();

private:
    AgentConfig            m_config;
    IFileWatcher          *m_watcher;          // not owned
    IEventReporter        *m_reporter;         // not owned
    QTimer                *m_heartbeatTimer  = nullptr;
    QNetworkAccessManager *m_netManager      = nullptr;
};
