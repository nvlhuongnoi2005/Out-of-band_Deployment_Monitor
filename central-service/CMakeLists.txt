find_package(Qt6 REQUIRED COMPONENTS Core Network)

set(CENTRAL_SOURCES
    src/main.cpp
    src/core/CentralService.cpp
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
)
