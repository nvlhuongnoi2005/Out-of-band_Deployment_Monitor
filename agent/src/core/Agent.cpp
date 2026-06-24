#include "Agent.h"
#include "watcher/IFileWatcher.h"
#include "reporter/IEventReporter.h"
#include <QDebug>

Agent::Agent(IFileWatcher   *watcher,
             IEventReporter *reporter,
             const AgentConfig &config,
             QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_watcher(watcher)
    , m_reporter(reporter)
{}

bool Agent::start()
{
    qInfo() << "[Agent] ID      :" << m_config.agentId;
    qInfo() << "[Agent] Server  :" << m_config.server;
    qInfo() << "[Agent] Project :" << m_config.project;
    qInfo() << "[Agent] Watching:" << m_config.watchDirs;
    qInfo() << "[Agent] Central :" << m_config.centralUrl;

    connect(m_watcher, &IFileWatcher::fileEventDetected,
            this,      &Agent::onFileEvent);
    connect(m_watcher, &IFileWatcher::errorOccurred,
            this,      &Agent::onWatcherError);

    if (!m_watcher->start(m_config.watchDirs)) {
        qCritical() << "[Agent] Failed to start file watcher";
        return false;
    }

    qInfo() << "[Agent] Started. Listening for file events...";
    return true;
}

void Agent::stop()
{
    if (m_watcher && m_watcher->isRunning())
        m_watcher->stop();
    qInfo() << "[Agent] Stopped.";
}

void Agent::onFileEvent(const FileEvent &event)
{
    // Agent chỉ làm một việc: enrich event với context từ config rồi forward
    FileEvent enriched   = event;
    enriched.agentId = m_config.agentId;
    enriched.server  = m_config.server;
    enriched.project = m_config.project;

    m_reporter->enqueue(enriched);
}

void Agent::onWatcherError(const QString &message)
{
    qCritical() << "[Agent] Watcher error:" << message;
}
