#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>

#include "core/Agent.h"
#include "config/AgentConfig.h"
#include "watcher/InotifyWatcher.h"
#include "reporter/EventReporter.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("oob-agent");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Out-of-Band Deployment Monitor — Agent");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOption({"c", "config"},
        "Path to config file", "config", "/etc/oob-monitor/agent-config.json");
    parser.addOption(configOption);
    parser.process(app);

    qInfo() << "=== Out-of-Band Deployment Monitor — Agent v0.1.0 ===";

    const AgentConfig config = AgentConfig::loadFromFile(parser.value(configOption));

    InotifyWatcher watcher;
    EventReporter  reporter(config.centralUrl, config.retryIntervalSec);
    Agent          agent(&watcher, &reporter, config);

    if (!agent.start())
        return 1;

    return app.exec();
}
