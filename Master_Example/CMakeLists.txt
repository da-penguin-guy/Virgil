cmake_minimum_required(VERSION 3.15)

project(VirgilMasterExample)



set(CMAKE_CXX_STANDARD 17)



find_package(nlohmann_json CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

# Find and link GLEW (required for Nuklear OpenGL3 backend)
find_package(GLEW REQUIRED)




add_executable(Master
    Master.cpp
)

# Add local nuklear include directory
target_include_directories(Master PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/nuklear)

target_link_libraries(Master
    nlohmann_json::nlohmann_json
    glfw
    opengl32
    GLEW::GLEW
)


