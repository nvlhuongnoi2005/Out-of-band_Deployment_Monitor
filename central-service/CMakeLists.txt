find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(Threads REQUIRED)

set(CENTRAL_SOURCES
    src/main.cpp
    src/core/CentralService.h
    src/core/CentralService.cpp
    src/core/AuditLogger.h
    src/core/AuditLogger.cpp
    src/jenkins/IJenkinsClient.h
    src/jenkins/MockJenkinsClient.h
    src/jenkins/MockJenkinsClient.cpp
    src/jenkins/HttpJenkinsClient.h
    src/jenkins/HttpJenkinsClient.cpp
    src/decision/DecisionEngine.h
    src/decision/DecisionEngine.cpp
    src/window/DeployWindowManager.h
    src/window/DeployWindowManager.cpp
    src/elasticsearch/ElasticsearchClient.h
    src/elasticsearch/ElasticsearchClient.cpp
    src/notification/INotifier.h
    src/notification/SmtpNotifier.h
    src/notification/SmtpNotifier.cpp
    src/remediation/AnsibleTrigger.h
    src/remediation/AnsibleTrigger.cpp
)

add_executable(oob-central ${CENTRAL_SOURCES})

target_include_directories(oob-central PRIVATE
    src/
    ${CMAKE_SOURCE_DIR}/third-party
)

target_link_libraries(oob-central PRIVATE
    Qt6::Core
    Qt6::Network
    shared
    Threads::Threads
)
