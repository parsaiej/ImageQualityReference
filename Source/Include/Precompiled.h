#ifndef PRECOMPILED_H
#define PRECOMPILED_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h> // For imgui.

// To resolve unresolved GUID symbols in DSR:
// https://discordapp.com/channels/590611987420020747/590965902564917258/1307840432872624228
#include <initguid.h>

#include <d3dx12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <directsr.h>
#include <DirectXMath.h>
#include <wrl.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_dx12.h>

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <implot.h>

#include <magic_enum/magic_enum.hpp>

#include <stb_image.h>

#include <tbb/tbb.h>

#include <Windows.h>
#include <format>
#include <set>

#include <SplashImageBytes.h>
#include <Util.h>

using namespace Microsoft::WRL;

#endif
