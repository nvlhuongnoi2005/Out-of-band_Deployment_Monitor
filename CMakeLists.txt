cmake_minimum_required(VERSION 3.16)
project(OutOfBandDeploymentMonitor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_subdirectory(shared)
add_subdirectory(agent)
add_subdirectory(central-service)
