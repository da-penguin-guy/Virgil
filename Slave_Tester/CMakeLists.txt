cmake_minimum_required(VERSION 3.15)

project(VirgilSlaveTester)

set(CMAKE_CXX_STANDARD 17)

find_package(nlohmann_json CONFIG REQUIRED)

add_executable(Slave
    Slave.cpp
)

# Add local nlohmann include directory
target_include_directories(Slave PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Slave
    nlohmann_json::nlohmann_json
    ws2_32
)
