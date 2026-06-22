#pragma once

#include <QObject>

class CentralService : public QObject
{
    Q_OBJECT
public:
    explicit CentralService(int port, QObject *parent = nullptr);
    ~CentralService() override;

    bool start();
    void stop();

private:
    int  m_port;
    bool m_running = false;
    // Day 5: HTTP server, DeployWindowManager, DecisionEngine sẽ được thêm vào đây
};
