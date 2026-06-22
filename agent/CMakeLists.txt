find_package(Qt6 REQUIRED COMPONENTS Core Network)

set(AGENT_SOURCES
    src/main.cpp
    src/core/Agent.cpp
    src/watcher/IFileWatcher.cpp
    src/watcher/InotifyWatcher.cpp
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
