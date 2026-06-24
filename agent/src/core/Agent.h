#pragma once

#include <QObject>
#include "config/AgentConfig.h"
#include "FileEvent.h"

class IFileWatcher;
class IEventReporter;

class Agent : public QObject
{
    Q_OBJECT
public:
    // Dependency Injection: Agent không biết InotifyWatcher hay EventReporter
    // Chỉ biết interface → dễ swap, dễ test
    explicit Agent(IFileWatcher   *watcher,
                   IEventReporter *reporter,
                   const AgentConfig &config,
                   QObject *parent = nullptr);

    bool start();
    void stop();

private slots:
    void onFileEvent(const FileEvent &event);
    void onWatcherError(const QString &message);

private:
    AgentConfig    m_config;
    IFileWatcher  *m_watcher;   // không owned — caller quản lý lifetime
    IEventReporter *m_reporter; // không owned — caller quản lý lifetime
};
