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

if (APPLE)
add_executable(ImageQualityReference)
else()
add_executable(ImageQualityReference WIN32)
endif()

target_sources(ImageQualityReference PRIVATE
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
    set(PLATFORM         "x64-win")
    set(PLATFORM_BIN_EXT "dll")
    set(PLATFORM_LIB_EXT "lib")
    set(NRI_LIB_PREFIX   "")
else()
    set(PLATFORM         "arm64-osx")
    set(PLATFORM_BIN_EXT "dylib")
    set(PLATFORM_LIB_EXT "a")
    set(NRI_LIB_PREFIX   "lib")
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
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/${NRI_LIB_PREFIX}NRI.${PLATFORM_LIB_EXT}
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/${NRI_LIB_PREFIX}NRI_NONE.${PLATFORM_LIB_EXT}
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/${NRI_LIB_PREFIX}NRI_Shared.${PLATFORM_LIB_EXT}
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/${NRI_LIB_PREFIX}NRI_Validation.${PLATFORM_LIB_EXT}
    ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/${NRI_LIB_PREFIX}NRI_VK.${PLATFORM_LIB_EXT}
)

if (WIN32)
    target_link_libraries(ImageQualityReference PRIVATE 
        ${CMAKE_SOURCE_DIR}/External/spirv-to-dxil/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libspirv_to_dxil.${PLATFORM_LIB_EXT}
        ${CMAKE_SOURCE_DIR}/External/spirv-to-dxil/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/libspirv_to_dxil.${PLATFORM_LIB_EXT}

        # Link the Windows-only NRI API support.
        ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/${NRI_LIB_PREFIX}NRI_D3D11.${PLATFORM_LIB_EXT}
        ${CMAKE_SOURCE_DIR}/External/NRI/lib/${PLATFORM}/${CMAKE_BUILD_TYPE}/${NRI_LIB_PREFIX}NRI_D3D12.${PLATFORM_LIB_EXT}

        d3d11 d3d12 dxgi

        C:\\Development\\NRI\\External\\nvapi\\amd64\\nvapi64.lib
    )
endif()

# Copy shader bytecodes. 
configure_file(${CMAKE_SOURCE_DIR}/Assets/Shaders/Compiled/FullscreenTriangle.vert.dxil Shaders/FullscreenTriangle.vert.dxil COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/Assets/Shaders/Compiled/Blit.frag.dxil Shaders/Blit.frag.dxil COPYONLY)