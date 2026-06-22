#include "Agent.h"
#include "watcher/IFileWatcher.h"
#include <QDebug>

Agent::Agent(const QString &configPath, QObject *parent)
    : QObject(parent)
    , m_config(AgentConfig::loadFromFile(configPath))
{
}

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
    qInfo() << "[Agent] Started. FileWatcher will be attached on Day 3.";
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
