#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <memory>

#include "core/Agent.h"
#include "config/AgentConfig.h"
#include "watcher/InotifyWatcher.h"
#include "watcher/EbpfWatcher.h"
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
    QCommandLineOption watcherOption({"w", "watcher"},
        "File watcher backend: inotify (default) or ebpf (requires bpftrace + root)",
        "watcher", "inotify");

    parser.addOption(configOption);
    parser.addOption(watcherOption);
    parser.process(app);

    qInfo() << "=== Out-of-Band Deployment Monitor — Agent v0.1.0 ===";

    const AgentConfig  config  = AgentConfig::loadFromFile(parser.value(configOption));
    // CLI flag takes precedence; fall back to watcher_backend from config file
    const QString      backend = parser.isSet(watcherOption)
                                     ? parser.value(watcherOption).toLower()
                                     : config.watcherBackend.toLower();

    std::unique_ptr<IFileWatcher> watcher;
    if (backend == "ebpf") {
        if (!EbpfWatcher::isAvailable()) {
            qCritical() << "[Agent] eBPF backend requested but bpftrace not found.";
            qCritical() << "        Install: sudo apt install bpftrace";
            return 1;
        }
        watcher = std::make_unique<EbpfWatcher>();
        qInfo() << "[Agent] Backend: eBPF (bpftrace)";
    } else {
        watcher = std::make_unique<InotifyWatcher>();
        qInfo() << "[Agent] Backend: inotify";
    }

    EventReporter reporter(config.centralUrl, config.retryIntervalSec);
    Agent         agent(watcher.get(), &reporter, config);

    if (!agent.start())
        return 1;

    return app.exec();
}
