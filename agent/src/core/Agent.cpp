#include "Agent.h"
#include "watcher/IFileWatcher.h"
#include "watcher/InotifyWatcher.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>

Agent::Agent(const QString &configPath, QObject *parent)
    : QObject(parent)
    , m_config(AgentConfig::loadFromFile(configPath))
{}

Agent::~Agent()
{
    stop();
}

bool Agent::start()
{
    qInfo() << "[Agent] ID      :" << m_config.agentId;
    qInfo() << "[Agent] Server  :" << m_config.server;
    qInfo() << "[Agent] Project :" << m_config.project;
    qInfo() << "[Agent] Watching:" << m_config.watchDirs;
    qInfo() << "[Agent] Central :" << m_config.centralUrl;

    m_watcher = new InotifyWatcher(this);
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
    if (m_watcher) {
        m_watcher->stop();
        m_watcher = nullptr;
    }
    qInfo() << "[Agent] Stopped.";
}

void Agent::onFileEvent(const FileEvent &event)
{
    // Đính kèm thông tin từ config vào event
    FileEvent enriched = event;
    enriched.agentId = m_config.agentId;
    enriched.server  = m_config.server;
    enriched.project = m_config.project;

    // In ra console dạng JSON (Ngày 4 sẽ gửi HTTP thay vì in)
    QJsonObject obj;
    obj["event_id"]    = enriched.eventId;
    obj["server"]      = enriched.server;
    obj["project"]     = enriched.project;
    obj["path"]        = enriched.path;
    obj["event_type"]  = eventTypeToString(enriched.eventType);
    obj["timestamp"]   = enriched.timestamp.toString(Qt::ISODateWithMs);
    obj["uid"]         = enriched.uid;
    obj["username"]    = enriched.username;
    obj["pid"]         = enriched.pid;
    obj["process_name"]= enriched.processName;

    QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    qInfo().noquote() << "[EVENT]" << json;
}

void Agent::onWatcherError(const QString &message)
{
    qCritical() << "[Agent] Watcher error:" << message;
}
