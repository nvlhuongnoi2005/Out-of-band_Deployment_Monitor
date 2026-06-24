#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include "core/CentralService.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("oob-central");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Out-of-Band Deployment Monitor — Central Reconciliation Service");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOption({"p", "port"},
        "Port to listen on", "port", "8080");
    QCommandLineOption auditOption({"a", "audit-log"},
        "Path to audit log file (JSON Lines)", "path", "/tmp/oob-audit.log");
    parser.addOption(portOption);
    parser.addOption(auditOption);
    parser.process(app);

    qInfo() << "=== Out-of-Band Deployment Monitor — Central Service v0.1.0 ===";

    CentralService service(
        parser.value(portOption).toInt(),
        parser.value(auditOption));

    if (!service.start())
        return 1;

    return app.exec();
}
