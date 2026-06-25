#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include "core/CentralService.h"
#include "notification/SmtpNotifier.h"
#include "remediation/AnsibleTrigger.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("oob-central");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Out-of-Band Deployment Monitor — Central Service");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOption({"p", "port"}, "Port to listen on", "port", "8080");
    QCommandLineOption auditOption({"a", "audit-log"}, "Audit log path (JSON Lines)", "path", "/tmp/oob-audit.log");

    // Elasticsearch
    QCommandLineOption esHostOption("es-host",  "Elasticsearch host (empty = disabled)", "host", "");
    QCommandLineOption esPortOption("es-port",  "Elasticsearch port", "port", "9200");
    QCommandLineOption esIndexOption("es-index","Elasticsearch index", "index", "oob-audit");
    QCommandLineOption esUserOption("es-user",  "Elasticsearch username (cloud)", "user", "");
    QCommandLineOption esPassOption("es-pass",  "Elasticsearch password (cloud)", "pass", "");
    QCommandLineOption esHttpsOption("es-https","Use HTTPS for Elasticsearch");

    // Jenkins (empty = use file-based mock)
    QCommandLineOption jenkinsUrlOption("jenkins-url",   "Jenkins base URL (e.g. http://localhost:8081)", "url", "");
    QCommandLineOption jenkinsUserOption("jenkins-user", "Jenkins username", "user", "admin");
    QCommandLineOption jenkinsTokenOption("jenkins-token","Jenkins API token or password", "token", "");

    // Ansible auto-remediation (FR-12)
    QCommandLineOption ansiblePlaybookOption("ansible-playbook",
        "Ansible playbook path (empty = disabled)", "path", "");
    QCommandLineOption ansibleInventoryOption("ansible-inventory",
        "Ansible inventory path", "path", "mock/ansible/inventory");

    // SMTP alerts
    QCommandLineOption smtpHostOption("smtp-host", "SMTP host (empty = disabled)", "host", "");
    QCommandLineOption smtpPortOption("smtp-port", "SMTP port", "port", "587");
    QCommandLineOption smtpUserOption("smtp-user", "SMTP username", "user", "");
    QCommandLineOption smtpPassOption("smtp-pass", "SMTP password", "pass", "");
    QCommandLineOption smtpFromOption("smtp-from", "Sender address", "email", "");
    QCommandLineOption smtpToOption("smtp-to",   "Admin alert address", "email", "");

    parser.addOption(portOption);
    parser.addOption(auditOption);
    parser.addOption(esHostOption);
    parser.addOption(esPortOption);
    parser.addOption(esIndexOption);
    parser.addOption(esUserOption);
    parser.addOption(esPassOption);
    parser.addOption(esHttpsOption);
    parser.addOption(jenkinsUrlOption);
    parser.addOption(jenkinsUserOption);
    parser.addOption(jenkinsTokenOption);
    parser.addOption(ansiblePlaybookOption);
    parser.addOption(ansibleInventoryOption);
    parser.addOption(smtpHostOption);
    parser.addOption(smtpPortOption);
    parser.addOption(smtpUserOption);
    parser.addOption(smtpPassOption);
    parser.addOption(smtpFromOption);
    parser.addOption(smtpToOption);
    parser.process(app);

    qInfo() << "=== Out-of-Band Deployment Monitor — Central Service v0.1.0 ===";

    std::unique_ptr<AnsibleTrigger> ansible;
    if (!parser.value(ansiblePlaybookOption).isEmpty()) {
        ansible = std::make_unique<AnsibleTrigger>(
            parser.value(ansiblePlaybookOption),
            parser.value(ansibleInventoryOption));
    }

    std::unique_ptr<SmtpNotifier> notifier;
    if (!parser.value(smtpHostOption).isEmpty()) {
        notifier = std::make_unique<SmtpNotifier>(
            parser.value(smtpHostOption),
            parser.value(smtpPortOption).toInt(),
            parser.value(smtpUserOption),
            parser.value(smtpPassOption),
            parser.value(smtpFromOption),
            parser.value(smtpToOption));
    }

    JenkinsConfig jenkins;
    jenkins.url      = parser.value(jenkinsUrlOption);
    jenkins.username = parser.value(jenkinsUserOption);
    jenkins.apiToken = parser.value(jenkinsTokenOption);

    CentralService service(
        parser.value(portOption).toInt(),
        parser.value(auditOption),
        parser.value(esHostOption),
        parser.value(esPortOption).toInt(),
        parser.value(esIndexOption),
        parser.value(esUserOption),
        parser.value(esPassOption),
        parser.isSet(esHttpsOption),
        jenkins,
        notifier.get(),
        ansible.get());

    if (!service.start())
        return 1;

    return app.exec();
}
