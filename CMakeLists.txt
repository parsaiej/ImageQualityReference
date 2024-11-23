cmake_minimum_required(VERSION 3.10)

# Build System
# --------------------------------

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/External/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "")

set(PROJECT_NAME ImageQualityReference)

project(${PROJECT_NAME} VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Packages
# --------------------------------

find_package(spdlog CONFIG REQUIRED)
find_package(Jolt   CONFIG REQUIRED)

# Executable
# --------------------------------

add_executable(ImageQualityReference WIN32
    Source/Main.cpp
)

# Defines
# --------------------------------

# Link
# --------------------------------

target_link_libraries(ImageQualityReference PRIVATE 
    spdlog::spdlog_header_only
    Jolt::Jolt
    dxgi
    d3d12
)