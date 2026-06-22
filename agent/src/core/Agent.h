#pragma once

#include <QObject>
#include <QString>
#include "config/AgentConfig.h"

class IFileWatcher;

class Agent : public QObject
{
    Q_OBJECT
public:
    explicit Agent(const QString &configPath, QObject *parent = nullptr);
    ~Agent() override;

    bool start();
    void stop();

private:
    AgentConfig   m_config;
    IFileWatcher *m_watcher = nullptr;  // Day 3: gắn InotifyWatcher vào đây
};
