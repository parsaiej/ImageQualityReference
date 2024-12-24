cmake_minimum_required(VERSION 3.10)

# Fail immediately if the Agility SDK is not found.
if (NOT DEFINED ENV{DIRECTX_AGILITY_SDK_DIR})
    message(FATAL_ERROR "Environment variable DIRECTX_AGILITY_SDK_DIR not found. Please install the latest DirectX Agility SDK and create an environment variable pointing to the root of it.")
endif()

message(STATUS "Agility SDK Found: " $ENV{DIRECTX_AGILITY_SDK_DIR})

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

find_package(spdlog                 CONFIG REQUIRED)
find_package(Jolt                   CONFIG REQUIRED)
find_package(imgui                  CONFIG REQUIRED)
find_package(implot                 CONFIG REQUIRED)
find_package(magic_enum             CONFIG REQUIRED)
find_package(glfw3                  CONFIG REQUIRED)
find_package(TBB                    CONFIG REQUIRED)  
find_package(d3d12-memory-allocator CONFIG REQUIRED)
find_package(glslang                CONFIG REQUIRED)
find_package(nlohmann_json          CONFIG REQUIRED)
find_package(CURL                          REQUIRED)
find_package(Stb                           REQUIRED)

# Executable
# --------------------------------

add_executable(ImageQualityReference WIN32
    Source/Main.cpp
    Source/Util.cpp
    Source/Interface.cpp
    Source/Precompiled.cpp
    Source/RenderInputShaderToy.cpp
)

# Compile Options
# --------------------------------

target_compile_options(ImageQualityReference PRIVATE /W4 /WX)

# PCH
# --------------------------------

target_precompile_headers(ImageQualityReference PRIVATE Source/Include/Precompiled.h)

# Defines
# --------------------------------

add_definitions(-DHAVE_STRUCT_TIMESPEC)

# Include
# --------------------------------

target_include_directories(ImageQualityReference BEFORE PRIVATE 
    Source/Include/
    External/spirv-to-dxil/include
    $ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/include/
    $ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/include/d3dx12/
    ${Stb_INCLUDE_DIR}
)

# Link
# --------------------------------

target_link_libraries(ImageQualityReference PRIVATE 
    spdlog::spdlog_header_only
    Jolt::Jolt
    dxgi
    d3d12
    glfw
    CURL::libcurl
    TBB::tbb
    imgui::imgui
    implot::implot
    unofficial::D3D12MemoryAllocator
    magic_enum::magic_enum
    glslang::glslang
    glslang::glslang-default-resource-limits
    glslang::SPIRV
    nlohmann_json::nlohmann_json
    synchronization.lib
    ${CMAKE_SOURCE_DIR}/External/spirv-to-dxil/lib/${CMAKE_BUILD_TYPE}/libspirv_to_dxil.lib
    ${CMAKE_SOURCE_DIR}/External/spirv-to-dxil/lib/${CMAKE_BUILD_TYPE}/libdxil_compiler.lib
)

configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/D3D12Core.dll      D3D12/D3D12Core.dll      COPYONLY)
configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/DirectSR.dll       D3D12/DirectSR.dll       COPYONLY)
configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/d3d12SDKLayers.dll D3D12/d3d12SDKLayers.dll COPYONLY)

configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/D3D12Core.pdb      D3D12/D3D12Core.pdb      COPYONLY)
configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/DirectSR.pdb       D3D12/DirectSR.pdb       COPYONLY)
configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/d3d12SDKLayers.pdb D3D12/d3d12SDKLayers.pdb COPYONLY)