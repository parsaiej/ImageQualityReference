cmake_minimum_required(VERSION 3.10)

# Fail immediately if the Agility SDK is not found.
if (WIN32 AND NOT DEFINED ENV{DIRECTX_AGILITY_SDK_DIR})
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

# Metal Native Window Utility
# --------------------------------

if (APPLE)
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    find_library(QUARTZ_CORE QuartzCore REQUIRED)
    add_library(MetalUtility SHARED "External/MetalUtility/MetalUtility.m")
    target_link_libraries(MetalUtility ${QUARTZ_CORE} ${COCOA_LIBRARY} glfw)
endif ()

# Packages
# --------------------------------

find_package(spdlog                 CONFIG REQUIRED)
find_package(Jolt                   CONFIG REQUIRED)
find_package(imgui                  CONFIG REQUIRED)
find_package(implot                 CONFIG REQUIRED)
find_package(magic_enum             CONFIG REQUIRED)
find_package(glfw3                  CONFIG REQUIRED)
find_package(TBB                    CONFIG REQUIRED) 
find_package(Taskflow               CONFIG REQUIRED) 
find_package(glslang                CONFIG REQUIRED)
find_package(nlohmann_json          CONFIG REQUIRED)
find_package(spirv_cross_core       CONFIG REQUIRED)
find_package(spirv_cross_glsl       CONFIG REQUIRED)
find_package(spirv_cross_hlsl       CONFIG REQUIRED)
find_package(Eigen3                 CONFIG REQUIRED)
find_package(CURL                          REQUIRED)
find_package(Stb                           REQUIRED)

if (WIN32)
    find_package(d3d12-memory-allocator CONFIG REQUIRED)
endif()

# Executable
# --------------------------------

add_executable(ImageQualityReference WIN32
    Source/Main.cpp
    Source/State.cpp
    Source/Precompiled.cpp
)

# Compile Options
# --------------------------------

# target_compile_options(ImageQualityReference PRIVATE /W4 /WX)

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
    External/NRI/Include
    $ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/include/
    $ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/include/d3dx12/
    ${Stb_INCLUDE_DIR}
)

# Link
# --------------------------------

if (WIN32)
    set(PLATFORM         "x64")
    set(PLATFORM_BIN_EXT "dll")
    set(PLATFORM_LIB_EXT "lib")
else()
    set(PLATFORM         "arm64-osx")
    set(PLATFORM_BIN_EXT "dylib")
    set(PLATFORM_LIB_EXT "a")
endif()

target_link_libraries(ImageQualityReference PRIVATE 
    spdlog::spdlog_header_only
    Jolt::Jolt
    glfw
    CURL::libcurl
    TBB::tbb
    Taskflow::Taskflow
    imgui::imgui
    implot::implot
    magic_enum::magic_enum
    glslang::glslang
    glslang::glslang-default-resource-limits
    glslang::SPIRV
    nlohmann_json::nlohmann_json
    # synchronization.lib
    spirv-cross-core
    spirv-cross-glsl
    spirv-cross-hlsl
    Eigen3::Eigen

    # Link NRI low-level graphics abstraction.
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libNRI.${PLATFORM_LIB_EXT}
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libNRI_NONE.${PLATFORM_LIB_EXT}
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libNRI_Shared.${PLATFORM_LIB_EXT}
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libNRI_Validation.${PLATFORM_LIB_EXT}
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libNRI_VK.${PLATFORM_LIB_EXT}
)

if (APPLE)
    target_link_libraries(ImageQualityReference PRIVATE MetalUtility)
endif()

if (WIN32)
    target_link_libraries(ImageQualityReference PRIVATE 
        ${CMAKE_SOURCE_DIR}/External/spirv-to-dxil/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libspirv_to_dxil.${PLATFORM_BIN_EXT}
        ${CMAKE_SOURCE_DIR}/External/spirv-to-dxil/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libspirv_to_dxil.${PLATFORM_BIN_EXT}
        unofficial::D3D12MemoryAllocator
    )
endif()

# Copy Agility SDK files.
# --------------------------------

if (WIN32)
    configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/D3D12Core.dll      D3D12/D3D12Core.dll      COPYONLY)
    configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/DirectSR.dll       D3D12/DirectSR.dll       COPYONLY)
    configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/d3d12SDKLayers.dll D3D12/d3d12SDKLayers.dll COPYONLY)

    configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/D3D12Core.pdb      D3D12/D3D12Core.pdb      COPYONLY)
    configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/DirectSR.pdb       D3D12/DirectSR.pdb       COPYONLY)
    configure_file($ENV{DIRECTX_AGILITY_SDK_DIR}/build/native/bin/x64/d3d12SDKLayers.pdb D3D12/d3d12SDKLayers.pdb COPYONLY)
endif()

# Copy shader bytecodes. 
configure_file(${CMAKE_SOURCE_DIR}/Assets/Shaders/Compiled/FullscreenTriangle.vert.dxil Shaders/FullscreenTriangle.vert.dxil COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/Assets/Shaders/Compiled/Blit.frag.dxil Shaders/Blit.frag.dxil COPYONLY)