cmake_minimum_required(VERSION 3.15)
project(SlaveExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Slave
    Slave.cpp
)

# nlohmann_json is header-only, so just add the include directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/nlohmann)

if (WIN32)
    target_link_libraries(Slave ws2_32)
endif()
