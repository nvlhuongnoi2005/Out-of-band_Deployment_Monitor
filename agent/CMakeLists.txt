find_package(Qt6 REQUIRED COMPONENTS Core Network)

set(AGENT_SOURCES
    src/main.cpp
    src/core/Agent.h
    src/core/Agent.cpp
    src/config/AgentConfig.h
    src/config/AgentConfig.cpp
    src/watcher/IFileWatcher.h
    src/watcher/InotifyWatcher.h
    src/watcher/InotifyWatcher.cpp
    src/watcher/EbpfWatcher.h
    src/watcher/EbpfWatcher.cpp
    src/reporter/IEventReporter.h
    src/reporter/EventReporter.h
    src/reporter/EventReporter.cpp
    src/proc/ProcHelper.h
    src/proc/ProcHelper.cpp
)

add_executable(oob-agent ${AGENT_SOURCES})

target_include_directories(oob-agent PRIVATE
    src/
    ${CMAKE_SOURCE_DIR}/third-party
)

target_link_libraries(oob-agent PRIVATE
    Qt6::Core
    Qt6::Network
    shared
)
