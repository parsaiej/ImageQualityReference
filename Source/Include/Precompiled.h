#ifndef PRECOMPILED_H
#define PRECOMPILED_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h> // For imgui.

#ifdef _WIN32
// To resolve unresolved GUID symbols in DSR:
// https://discordapp.com/channels/590611987420020747/590965902564917258/1307840432872624228
#include <initguid.h>

#include <d3dx12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <directsr.h>
#include <DirectXMath.h>
#include <D3D12MemAlloc.h>

#include <wrl.h>
using namespace Microsoft::WRL;

#include <Windows.h>
#endif

#include <curl/curl.h>

// Using the eigen vector math library.
#include <Eigen/Eigen>

// NRI low-level graphics API abstraction.
#include <NRI/NRI.h>
#include <NRI/NRIDescs.h>
#include <NRI/Extensions/NRIDeviceCreation.h>
#include <NRI/Extensions/NRIHelper.h>
#include <NRI/Extensions/NRISwapChain.h>
#include <NRI/Extensions/NRIResourceAllocator.h>
#include <NRI/Extensions/NRIWrapperVK.h>

#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/SPIRV/disassemble.h>
#include <glslang/SPIRV/SPVRemapper.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include <spirv_cross/spirv_hlsl.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#ifdef __APPLE__
#include <imgui_impl_vulkan.h>
#else
#error TODO
#endif

#ifdef _WIN32
#include <imgui_impl_dx12.h>
#endif

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#ifdef __APPLE__
#include "../../External/MetalUtility/MetalUtility.h"
#endif

#include <implot.h>

#include <magic_enum/magic_enum.hpp>

#include <stb_image.h>

#include <tbb/tbb.h>
#include <taskflow/taskflow.hpp>

#include <nlohmann/json.hpp>

#include <format>
#include <set>
#include <fstream>

#include <spirv_to_dxil.h>

#include <vk_enum_string_helper.h>

#endif
