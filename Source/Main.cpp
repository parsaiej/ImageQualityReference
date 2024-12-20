// clang-format off
// Tell Windows to load the Agility SDK DLLs. 
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 715; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
// clang-format on

// Import the imgui style.
#include "OverlayStyle.h"

// Options
// -----------------------------

enum WindowMode
{
    Windowed,
    BorderlessFullscreen,
    ExclusiveFullscreen
};

enum SwapEffect
{
    FlipSequential,
    FlipDiscard
};

enum UpdateFlags : uint32_t
{
    None              = 0,
    Window            = 1 << 0,
    SwapChainRecreate = 1 << 1,
    SwapChainResize   = 1 << 2,
    GraphicsRuntime   = 1 << 3
};

// State
// -----------------------------

HINSTANCE   s_Instance     = nullptr;
GLFWwindow* s_Window       = nullptr;
HWND        s_WindowNative = nullptr;

WindowMode s_WindowMode     = WindowMode::Windowed;
WindowMode s_WindowModePrev = WindowMode::Windowed;

int s_CurrentSwapChainImageIndex;
int s_RTVDescriptorSize;
int s_SRVDescriptorSize;

ComPtr<ID3D12Device>              s_LogicalDevice     = nullptr;
ComPtr<IDSRDevice>                s_DSRDevice         = nullptr;
ComPtr<ID3D12CommandQueue>        s_CommandQueue      = nullptr;
ComPtr<ID3D12DescriptorHeap>      s_RTVDescriptorHeap = nullptr;
ComPtr<ID3D12DescriptorHeap>      s_SRVDescriptorHeap = nullptr;
ComPtr<ID3D12CommandAllocator>    s_CommandAllocator  = nullptr;
ComPtr<ID3D12GraphicsCommandList> s_CommandList       = nullptr;

std::vector<ComPtr<ID3D12Resource>> s_SwapChainImages;
uint32_t                            s_SwapChainImageCount = 2;

int                                    s_DSRVariantIndex;
std::vector<DSR_SUPERRES_VARIANT_DESC> s_DSRVariantDescs;
std::vector<std::string>               s_DSRVariantNames;

ComPtr<ID3D12Fence> s_Fence                     = nullptr;
HANDLE              s_FenceOperatingSystemEvent = nullptr;
UINT64              s_FenceValue                = 0U;

// Process -> Adapter -> Display (DXGI)
ComPtr<IDXGIAdapter1>           s_DXGIAdapter;
ComPtr<IDXGIFactory6>           s_DXGIFactory;
ComPtr<IDXGISwapChain3>         s_DXGISwapChain;
std::vector<DXGI_ADAPTER_DESC1> s_DXGIAdapterInfos;
SwapEffect                      s_DXGISwapEffect;
std::vector<IDXGIOutput*>       s_DXGIOutputs;
std::vector<std::string>        s_DXGIOutputNames;
std::vector<DXGI_MODE_DESC>     s_DXGIDisplayModes;

std::vector<DirectX::XMINT2> s_DXGIDisplayResolutions;
std::vector<std::string>     s_DXGIDisplayResolutionsStr;

int s_DXGIAdapterIndex;
int s_DXGIOutputsIndex;
int s_DXGIDisplayResolutionsIndex;

// Adapted from all found DXGI_ADAPTER_DESC1 for easy display in the UI.
std::vector<std::string> s_DXGIAdapterNames;

// Log buffer memory.
std::shared_ptr<std::stringstream> s_LoggerMemory;

// Time since last present.
float s_DeltaTime = 0.0;

// Maintain a 120-frame moving average for the delta time.
MovingAverage s_DeltaTimeMovingAverage(60);

// Ring buffer for frame time (ms) + moving average
ScrollingBuffer s_DeltaTimeBuffer;
ScrollingBuffer s_DeltaTimeMovingAverageBuffer;

// V-Sync Interval requested by user.
int s_SyncInterval;

// Current update flags for the frame.
uint32_t s_UpdateFlags;

// Output (post-upscaled) viewport resolution.
DirectX::XMINT2 s_BackBufferSize { 1280, 720 };
DirectX::XMINT2 s_BackBufferSizePrev { 1280, 720 };

// Cached window rect if going from fullscreen -> window.
RECT s_WindowRect;
UINT s_WindowStyle = WS_OVERLAPPEDWINDOW | WS_SYSMENU;

StopWatch s_StopWatch;

// Utility Prototypes
// -----------------------------

std::string FromWideStr(std::wstring str);

void CreateSwapChain();

void ReleaseSwapChain();

// Creates an OS window for Microsoft Windows.
void CreateOperatingSystemWindow();

// Enumerate a list of graphics adapters that support our usage of D3D12.
void EnumerateSupportedAdapters();

// Release all D3D12 resources.
void ReleaseGraphicsRuntime();

// Create a DXGI swap-chain for the OS window.
void InitializeGraphicsRuntime();

// Command list recording and queue submission for current swap chain image.
void Render();

// For per-frame imgui window creation.
void RenderInterface();

// Message handle for a Microsoft Windows window.
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// Create or sync DXGI, OS Window, Swapchain, HDR/SDR, V-Sync with current settings.
void ValidatePresentation();

// Crashing utility.
void ThrowIfFailed(HRESULT hr);

// Use fence primitives to pause the thread until the previous swap-chain image has finished being drawn and presented.
void WaitForDevice();

// Syncs runtime context with user settings.
void SyncSettings();

// Entry-point
// -----------------------------

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    s_Instance = hInstance;

    // Flush on critical events since we usually fatally crash the app right after.
    spdlog::flush_on(spdlog::level::critical);

    // Configure logging.
    // --------------------------------------

    s_LoggerMemory  = std::make_shared<std::stringstream>();
    auto loggerSink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*s_LoggerMemory);
    auto logger     = std::make_shared<spdlog::logger>("", loggerSink);

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%l] %v");

#ifdef _DEBUG
    spdlog::set_level(spdlog::level::debug);
#endif

    EnumerateSupportedAdapters();

    // Create operating system window.
    // -----------------------------------------
    {
        glfwInit();

        s_Window = glfwCreateWindow(s_BackBufferSize.x, s_BackBufferSize.y, "ImageClarityReference", nullptr, nullptr);

        if (!s_Window)
            exit(1);

        s_WindowNative = glfwGetWin32Window(s_Window);
    }

    InitializeGraphicsRuntime();

    // Configure initial window size based on display-supplied resolutions.
    // -----------------------------------------
    {
        // Select a resolution size from the list, around the half-largest one.
        s_DXGIDisplayResolutionsIndex = static_cast<int>(s_DXGIDisplayResolutions.size()) / 2;

        s_BackBufferSize = s_DXGIDisplayResolutions[s_DXGIDisplayResolutionsIndex];

        // Queue a resize on the next render call.
        s_UpdateFlags |= UpdateFlags::SwapChainResize;
    }

    while (!glfwWindowShouldClose(s_Window))
    {
        glfwPollEvents();

        Render();
    }

    ReleaseGraphicsRuntime();

    for (auto& pOutput : s_DXGIOutputs)
    {
        if (pOutput)
            pOutput->Release();
    }

    return 0;
}

std::string FromWideStr(std::wstring wstr)
{
    // Determine the size of the resulting string
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);

    if (size == 0)
        throw std::runtime_error("Failed to convert wide string to string.");

    // Allocate a buffer for the resulting string
    std::string str(size - 1, '\0'); // size - 1 because size includes the null terminator
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);

    return str;
}

template <typename T>
bool EnumDropdown(const char* name, int* selectedEnumIndex)
{
    constexpr auto enumNames = magic_enum::enum_names<T>();

    std::vector<const char*> enumNameStrings(enumNames.size());

    for (uint32_t enumIndex = 0U; enumIndex < enumNames.size(); enumIndex++)
        enumNameStrings[enumIndex] = enumNames[enumIndex].data();

    return ImGui::Combo(name, selectedEnumIndex, enumNameStrings.data(), static_cast<int>(enumNameStrings.size()));
}

bool StringListDropdown(const char* name, const std::vector<std::string>& strings, int& selectedIndex)
{
    if (strings.empty())
        return false;

    bool modified = false;

    if (ImGui::BeginCombo(name, strings[selectedIndex].c_str()))
    {
        for (int i = 0; i < strings.size(); i++)
        {
            if (ImGui::Selectable(strings[i].c_str(), selectedIndex == i))
            {
                selectedIndex = i;
                modified      = true;
            }

            if (selectedIndex == i)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    return modified;
}

bool StringListDropdown(const char* name, const char* const* cstrings, size_t size, int& selectedIndex)
{
    std::vector<std::string> strings;

    for (uint32_t i = 0; i < size; i++)
        strings.push_back(cstrings[i]);

    return StringListDropdown(name, strings, selectedIndex);
}

void EnumerateSupportedAdapters()
{
    ComPtr<IDXGIFactory6> pDXGIFactory;
    ThrowIfFailed(CreateDXGIFactory2(0u, IID_PPV_ARGS(pDXGIFactory.GetAddressOf())));

    ComPtr<IDXGIAdapter1> pAdapter;

    s_DXGIAdapterInfos.clear();
    s_DXGIAdapterNames.clear();

    for (UINT adapterIndex = 0;
         SUCCEEDED(pDXGIFactory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter)));
         ++adapterIndex)
    {
        if (!SUCCEEDED(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
            continue;

        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);

        s_DXGIAdapterInfos.push_back(desc);
    }

    // Extract a list of supported adapter names for presentation to the user.
    std::transform(s_DXGIAdapterInfos.begin(),
                   s_DXGIAdapterInfos.end(),
                   std::back_inserter(s_DXGIAdapterNames),
                   [](const DXGI_ADAPTER_DESC1& adapter) { return FromWideStr(adapter.Description); });

    if (!s_DXGIAdapterInfos.empty())
        return;

    MessageBox(nullptr,
               "No D3D12 Adapters found that support D3D_FEATURE_LEVEL_12_0. The app will now exit.",
               "Image Clarity Reference",
               MB_ICONERROR | MB_OK);

    // Nothing to do if no devices are found.
    exit(1);
}

void CreateSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount           = s_SwapChainImageCount;
    swapChainDesc.Width                 = static_cast<UINT>(s_BackBufferSize.x);
    swapChainDesc.Height                = static_cast<UINT>(s_BackBufferSize.y);
    swapChainDesc.Format                = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SampleDesc.Count      = 1;

    switch (s_DXGISwapEffect)
    {
        // D3D12 only support DXGI_EFFECT_FLIP_* so we need to adapt for it.
        case SwapEffect::FlipDiscard   : swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; break;
        case SwapEffect::FlipSequential: swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; break;
    }

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(
        s_DXGIFactory->CreateSwapChainForHwnd(s_CommandQueue.Get(), s_WindowNative, &swapChainDesc, nullptr, nullptr, swapChain.GetAddressOf()));

    // Does not support fullscreen transitions.
    ThrowIfFailed(s_DXGIFactory->MakeWindowAssociation(s_WindowNative, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&s_DXGISwapChain));
    s_CurrentSwapChainImageIndex = s_DXGISwapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(s_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    s_SwapChainImages.resize(s_SwapChainImageCount);

    for (UINT swapChainImageIndex = 0; swapChainImageIndex < s_SwapChainImageCount; swapChainImageIndex++)
    {
        ThrowIfFailed(s_DXGISwapChain->GetBuffer(swapChainImageIndex, IID_PPV_ARGS(&s_SwapChainImages[swapChainImageIndex])));
        s_LogicalDevice->CreateRenderTargetView(s_SwapChainImages[swapChainImageIndex].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, s_RTVDescriptorSize);
    }
}

void ReleaseSwapChain()
{
    // Need to force disable fullscreen in order to destroy swap chain.
    ThrowIfFailed(s_DXGISwapChain->SetFullscreenState(false, nullptr));

    for (auto& swapChainImageView : s_SwapChainImages)
    {
        if (swapChainImageView)
            swapChainImageView.Reset();
    }

    s_DXGISwapChain.Reset();
}

DirectX::XMINT2 s_PreviousWindowedPos;
DirectX::XMINT2 s_PreviousWindowedSize;
int             s_DXGIDisplayResolutionsIndexPrev;

void UpdateWindowAndUpdateSwapChain()
{
    switch (s_WindowMode)
    {
        case WindowMode::BorderlessFullscreen:
        case WindowMode::ExclusiveFullscreen:
        {
            if (s_WindowModePrev != WindowMode::Windowed)
                break;

            // Cache the windowed window if making fullscreen transition.
            s_DXGIDisplayResolutionsIndexPrev = s_DXGIDisplayResolutionsIndex;
            glfwGetWindowPos(s_Window, &s_PreviousWindowedPos.x, &s_PreviousWindowedPos.y);
            glfwGetWindowSize(s_Window, &s_PreviousWindowedSize.x, &s_PreviousWindowedSize.y);
        }

        default: break;
    }

    // Handle DXGI / GLFW changes.
    switch (s_WindowMode)
    {
        case WindowMode::Windowed:
        {
            ThrowIfFailed(s_DXGISwapChain->SetFullscreenState(false, nullptr));
            glfwSetWindowSize(s_Window, s_BackBufferSize.x, s_BackBufferSize.y);
            glfwSetWindowAttrib(s_Window, GLFW_DECORATED, GLFW_TRUE);
            break;
        }

        case WindowMode::BorderlessFullscreen:
        {
            ThrowIfFailed(s_DXGISwapChain->SetFullscreenState(false, nullptr));
            glfwSetWindowAttrib(s_Window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(s_Window, nullptr, 0, 0, s_BackBufferSize.x, s_BackBufferSize.y, 0);
            break;
        }

        case WindowMode::ExclusiveFullscreen:
        {
            if (FAILED(s_DXGISwapChain->SetFullscreenState(true, nullptr)))
            {
                spdlog::info("Failed to go fullscreen. The adapter may not own the output display. Reverting to borderless.");
                s_WindowMode = WindowMode::BorderlessFullscreen;
                UpdateWindowAndUpdateSwapChain();
                return;
            }

            break;
        };
    }

    // Handle backbuffer sizes.
    switch (s_WindowMode)
    {
        case WindowMode::Windowed:
        {
            if (s_WindowModePrev != WindowMode::Windowed)
            {
                s_DXGIDisplayResolutionsIndex = s_DXGIDisplayResolutionsIndexPrev;

                // Restore cached window if making fullscreen transition.
                glfwSetWindowMonitor(s_Window,
                                     nullptr,
                                     s_PreviousWindowedPos.x,
                                     s_PreviousWindowedPos.y,
                                     s_PreviousWindowedSize.x,
                                     s_PreviousWindowedSize.y,
                                     0);
            }

            s_BackBufferSize = s_DXGIDisplayResolutions[s_DXGIDisplayResolutionsIndex];
            break;
        }

        case WindowMode::BorderlessFullscreen:
        case WindowMode::ExclusiveFullscreen:
        {
            s_DXGIDisplayResolutionsIndex = static_cast<int>(s_DXGIDisplayResolutions.size()) - 1;
            s_BackBufferSize              = s_DXGIDisplayResolutions[s_DXGIDisplayResolutionsIndex];
            break;
        }
    }

    spdlog::info("Resizing Swap Chain ({}x{} --> {}x{})", s_BackBufferSizePrev.x, s_BackBufferSizePrev.y, s_BackBufferSize.x, s_BackBufferSize.y);

    DXGI_MODE_DESC targetDisplayMode = s_DXGIDisplayModes[s_DXGIDisplayModes.size() - 1];
    {
        targetDisplayMode.Width  = s_BackBufferSize.x;
        targetDisplayMode.Height = s_BackBufferSize.y;
    }

    // Reconstruct the closest-matching display mode based on the user selections of resolution, refresh rate, etc.
    DXGI_MODE_DESC closestDisplayMode;
    ThrowIfFailed(s_DXGIOutputs[s_DXGIOutputsIndex]->FindClosestMatchingMode(&targetDisplayMode, &closestDisplayMode, nullptr));

    ThrowIfFailed(s_DXGISwapChain->ResizeTarget(&closestDisplayMode));

    for (auto& swapChainImageView : s_SwapChainImages)
        swapChainImageView.Reset();

    DXGI_SWAP_CHAIN_DESC swapChainInfo = {};
    s_DXGISwapChain->GetDesc(&swapChainInfo);

    ThrowIfFailed(s_DXGISwapChain->ResizeBuffers(s_SwapChainImageCount,
                                                 s_BackBufferSize.x,
                                                 s_BackBufferSize.y,
                                                 swapChainInfo.BufferDesc.Format,
                                                 swapChainInfo.Flags));

    s_CurrentSwapChainImageIndex = s_DXGISwapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(s_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    s_SwapChainImages.resize(s_SwapChainImageCount);

    for (UINT swapChainImageIndex = 0; swapChainImageIndex < s_SwapChainImageCount; swapChainImageIndex++)
    {
        ThrowIfFailed(s_DXGISwapChain->GetBuffer(swapChainImageIndex, IID_PPV_ARGS(s_SwapChainImages[swapChainImageIndex].GetAddressOf())));
        s_LogicalDevice->CreateRenderTargetView(s_SwapChainImages[swapChainImageIndex].Get(), nullptr, rtvHandle);

        rtvHandle.Offset(1, s_RTVDescriptorSize);
    }

    s_BackBufferSizePrev = s_BackBufferSize;
    s_WindowModePrev     = s_WindowMode;
}

void ReleaseGraphicsRuntime()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    s_DSRDevice.Reset();
    s_CommandAllocator->Reset();
    s_CommandList.Reset();
    s_CommandQueue.Reset();
    s_SRVDescriptorHeap.Reset();
    s_RTVDescriptorHeap.Reset();
    s_Fence.Reset();

    ReleaseSwapChain();

    s_DXGIAdapter.Reset();
    s_DXGIFactory.Reset();
    s_LogicalDevice.Reset();
}

void EnumerateOutputDisplays()
{
    // NOTE: Some systems (i.e. laptops with dedicate GPU) fail to enumrate any outputs
    //       due to ownership of the output being with the integrated GPU (i.e. Intel).
    //       Thus, we enumerate all adapters first and check each output that adapter owns.

    for (auto& pOutput : s_DXGIOutputs)
    {
        if (pOutput)
            pOutput->Release();
    }

    s_DXGIOutputs.clear();
    s_DXGIOutputNames.clear();

    IDXGIAdapter* pAdapter;
    UINT          adapterIndex = 0u;

    while (s_DXGIFactory->EnumAdapters(adapterIndex++, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        IDXGIOutput* pOutput;
        UINT         outputIndex = 0u;

        while (pAdapter->EnumOutputs(outputIndex++, &pOutput) != DXGI_ERROR_NOT_FOUND)
            s_DXGIOutputs.push_back(pOutput);

        pAdapter->Release();
    }

    // Extract list of names for overlay.
    std::transform(s_DXGIOutputs.begin(),
                   s_DXGIOutputs.end(),
                   std::back_inserter(s_DXGIOutputNames),
                   [](IDXGIOutput* pOutput)
                   {
                       DXGI_OUTPUT_DESC outputInfo;
                       pOutput->GetDesc(&outputInfo);

                       return FromWideStr(outputInfo.DeviceName);
                   });
}

void EnumerateDisplayModes()
{
    UINT displayModeCount;
    ThrowIfFailed(s_DXGIOutputs[s_DXGIOutputsIndex]->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &displayModeCount, nullptr));

    s_DXGIDisplayModes.resize(displayModeCount);

    ThrowIfFailed(s_DXGIOutputs[s_DXGIOutputsIndex]->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &displayModeCount, s_DXGIDisplayModes.data()));

    // Extract list of unique resolutions. (There may be multiples of the same resolution due
    // to various refresh rates, dxgi formats, etc.).
    // ----------------------------------------------------------
    {
        std::set<DirectX::XMINT2, XMINT2Cmp> uniqueDisplayModeResolutions;

        std::transform(s_DXGIDisplayModes.begin(),
                       s_DXGIDisplayModes.end(),
                       std::inserter(uniqueDisplayModeResolutions, uniqueDisplayModeResolutions.begin()),
                       [&](const DXGI_MODE_DESC& mode) { return DirectX::XMINT2(mode.Width, mode.Height); });

        s_DXGIDisplayResolutions.resize(uniqueDisplayModeResolutions.size());
        std::copy(uniqueDisplayModeResolutions.begin(), uniqueDisplayModeResolutions.end(), s_DXGIDisplayResolutions.begin());

        std::transform(s_DXGIDisplayResolutions.begin(),
                       s_DXGIDisplayResolutions.end(),
                       std::back_inserter(s_DXGIDisplayResolutionsStr),
                       [&](const DirectX::XMINT2& r) { return std::format("{} x {}", r.x, r.y); });
    }
}

void InitializeGraphicsRuntime()
{
    UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    // DXGI Adapter Selection
    // ------------------------------------------

    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(s_DXGIFactory.GetAddressOf())));

    ComPtr<IDXGIAdapter1> pAdapter;
    for (UINT adapterIndex = 0; SUCCEEDED(s_DXGIFactory->EnumAdapters1(adapterIndex, &pAdapter)); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);

        if (s_DXGIAdapterInfos[s_DXGIAdapterIndex].DeviceId == desc.DeviceId)
            break;
    }

    s_DXGIAdapter = pAdapter.Detach();

    EnumerateOutputDisplays();

    EnumerateDisplayModes();

#ifdef _DEBUG
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            debugController->EnableDebugLayer();
    }
#endif

    ThrowIfFailed(D3D12CreateDevice(s_DXGIAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(s_LogicalDevice.GetAddressOf())));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type                     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(s_LogicalDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(s_CommandQueue.GetAddressOf())));

    // Descriptor heaps.
    // ------------------------------------------

    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors             = 32;
        rtvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(s_LogicalDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(s_RTVDescriptorHeap.GetAddressOf())));

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors             = 32;
        srvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(s_LogicalDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(s_SRVDescriptorHeap.GetAddressOf())));

        s_RTVDescriptorSize = s_LogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        s_SRVDescriptorSize = s_LogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // ------------------------------------------

    CreateSwapChain();

    // ------------------------------------------

    ThrowIfFailed(s_LogicalDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(s_CommandAllocator.GetAddressOf())));

    // Initialize DirectSR device.
    // ------------------------------------------

    do
    {
        ComPtr<ID3D12DSRDeviceFactory> pDSRDeviceFactory;
        if (FAILED(D3D12GetInterface(CLSID_D3D12DSRDeviceFactory, IID_PPV_ARGS(&pDSRDeviceFactory))))
            break;

        if (FAILED(pDSRDeviceFactory->CreateDSRDevice(s_LogicalDevice.Get(), 1, IID_PPV_ARGS(s_DSRDevice.GetAddressOf()))))
            break;

        s_DSRVariantDescs.resize(s_DSRDevice->GetNumSuperResVariants());

        for (UINT variantIndex = 0; variantIndex < s_DSRVariantDescs.size(); variantIndex++)
        {
            DSR_SUPERRES_VARIANT_DESC variantDesc;
            ThrowIfFailed(s_DSRDevice->GetSuperResVariantDesc(variantIndex, &variantDesc));

            s_DSRVariantDescs[variantIndex] = variantDesc;
        }

        s_DSRVariantNames.clear();
        s_DSRVariantIndex = 0;

        // Enumerate variants names.
        std::transform(s_DSRVariantDescs.begin(),
                       s_DSRVariantDescs.end(),
                       std::back_inserter(s_DSRVariantNames),
                       [](const DSR_SUPERRES_VARIANT_DESC& variantDesc) { return variantDesc.VariantName; });
    }
    while (false);

    ThrowIfFailed(s_LogicalDevice->CreateCommandList(0,
                                                     D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     s_CommandAllocator.Get(),
                                                     nullptr,
                                                     IID_PPV_ARGS(s_CommandList.GetAddressOf())));

    ThrowIfFailed(s_CommandList->Close());

    // Frames-in-flight synchronization objects.
    // ------------------------------------------

    {
        ThrowIfFailed(s_LogicalDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(s_Fence.GetAddressOf())));
        s_FenceValue = 1;

        // Create an event handle to use for frame synchronization.
        if (!s_FenceOperatingSystemEvent)
            s_FenceOperatingSystemEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (s_FenceOperatingSystemEvent == nullptr)
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

        // Wait for the command list to execute; we are reusing the same command
        // list in our main loop but for now, we just want to wait for setup to
        // complete before continuing.
        WaitForDevice();
    }

    // Initialize ImGui.
    // ------------------------------------------

    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();

        ImGuiIO& io = ImGui::GetIO();

        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

        // Auto-install message callbacks.
        ImGui_ImplGlfw_InitForOther(s_Window, true);

        auto srvHeapCPUHandle = s_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        auto srvHeapGPUHandle = s_SRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

        ImGui_ImplDX12_Init(s_LogicalDevice.Get(),
                            DXGI_MAX_SWAP_CHAIN_BUFFERS,
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            s_SRVDescriptorHeap.Get(),
                            srvHeapCPUHandle,
                            srvHeapGPUHandle);

        SetStyle();
    }

    // Log a message containing information about the adapter.
    // ------------------------------------------

    {
        spdlog::info("Device: {}", s_DXGIAdapterNames[s_DXGIAdapterIndex]);
        spdlog::info("VRAM:   {} GB", s_DXGIAdapterInfos[s_DXGIAdapterIndex].DedicatedVideoMemory / static_cast<float>(1024 * 1024 * 1024));
    }
}

void RenderInterface()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)s_BackBufferSize.x * 0.25f, (float)s_BackBufferSize.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);

    if (ImGui::CollapsingHeader("Presentation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        StringListDropdown("Display", s_DXGIOutputNames, s_DXGIOutputsIndex);

        ImGui::BeginDisabled(s_WindowMode != WindowMode::Windowed);

        if (StringListDropdown("Resolution", s_DXGIDisplayResolutionsStr, s_DXGIDisplayResolutionsIndex))
            s_UpdateFlags |= UpdateFlags::SwapChainResize;

        ImGui::EndDisabled();

        if (StringListDropdown("Adapter", s_DXGIAdapterNames, s_DXGIAdapterIndex))
            s_UpdateFlags |= UpdateFlags::GraphicsRuntime;

        if (EnumDropdown<WindowMode>("Window Mode", reinterpret_cast<int*>(&s_WindowMode)))
            s_UpdateFlags |= UpdateFlags::SwapChainResize;

        if (EnumDropdown<SwapEffect>("Swap Effect", reinterpret_cast<int*>(&s_DXGISwapEffect)))
            s_UpdateFlags |= UpdateFlags::SwapChainRecreate;

        // TODO: Might be able to resize here?
        if (ImGui::SliderInt("Buffering", reinterpret_cast<int*>(&s_SwapChainImageCount), 2, DXGI_MAX_SWAP_CHAIN_BUFFERS - 1))
            s_UpdateFlags |= UpdateFlags::SwapChainRecreate;

        ImGui::SliderInt("V-Sync Interval", &s_SyncInterval, 0, 4);

        static int s_FramesInFlightCount = 1;
        ImGui::SliderInt("Frames in Flight", &s_FramesInFlightCount, 1, 16);
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!s_DSRVariantDescs.empty())
            StringListDropdown("DirectSR Algorithm", s_DSRVariantNames, s_DSRVariantIndex);
        else
            StringListDropdown("DirectSR Algorithm", { "None" }, s_DSRVariantIndex);
    }

    if (ImGui::CollapsingHeader("Analysis", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("TODO");
    }

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static float elapsedTime = 0;
        elapsedTime += s_DeltaTime;

        constexpr std::array<const char*, 2> graphModes = { "Frame Time (Milliseconds)", "Frames-per-Second" };

        static int selectedPerformanceGraphMode;
        StringListDropdown("Graph Mode", graphModes.data(), graphModes.size(), selectedPerformanceGraphMode);

        switch (selectedPerformanceGraphMode)
        {
            case 0:
            {
                float deltaTimeMs = 1000.0f * s_DeltaTime;
                s_DeltaTimeMovingAverage.AddValue(deltaTimeMs);
                s_DeltaTimeBuffer.AddPoint(elapsedTime, deltaTimeMs);
                s_DeltaTimeMovingAverageBuffer.AddPoint(elapsedTime, s_DeltaTimeMovingAverage.GetAverage());
                break;
            }

            case 1:
            {
                float framesPerSecond = 1.0f / s_DeltaTime;
                s_DeltaTimeMovingAverage.AddValue(framesPerSecond);
                s_DeltaTimeBuffer.AddPoint(elapsedTime, framesPerSecond);
                s_DeltaTimeMovingAverageBuffer.AddPoint(elapsedTime, s_DeltaTimeMovingAverage.GetAverage());
                break;
            }
        }

        static float history = 3.0f;

        if (ImPlot::BeginPlot("##PerformanceChild", ImVec2(-1, 150)))
        {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, 0x0);
            ImPlot::SetupAxisLimits(ImAxis_X1, elapsedTime - history, elapsedTime, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, s_DeltaTimeMovingAverage.GetAverage() * 2.0, ImGuiCond_Always);

            ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 1.0);

            ImPlot::PlotLine("Exact",
                             &s_DeltaTimeBuffer.mData[0].x,
                             &s_DeltaTimeBuffer.mData[0].y,
                             s_DeltaTimeBuffer.mData.size(),
                             ImPlotLineFlags_None,
                             s_DeltaTimeBuffer.mOffset,
                             2 * sizeof(float));

            ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 2.0);

            ImPlot::PlotLine("Smoothed",
                             &s_DeltaTimeMovingAverageBuffer.mData[0].x,
                             &s_DeltaTimeMovingAverageBuffer.mData[0].y,
                             s_DeltaTimeMovingAverageBuffer.mData.size(),
                             ImPlotLineFlags_None,
                             s_DeltaTimeMovingAverageBuffer.mOffset,
                             2 * sizeof(float));

            ImPlot::EndPlot();
        }
    }

    if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));

        if (ImGui::BeginChild("##LogChild", ImVec2(0, 200), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::TextUnformatted(s_LoggerMemory->str().c_str());

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0F);
        }
        ImGui::EndChild();

        ImGui::PopStyleColor(2);
    }

    ImGui::PopItemWidth();

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2((float)s_BackBufferSize.x * 0.25f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2((float)s_BackBufferSize.x * 0.75f, (float)s_BackBufferSize.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::End();
}

void SyncSettings()
{
    if ((s_UpdateFlags & UpdateFlags::GraphicsRuntime) != 0)
    {
        if (s_WindowMode == WindowMode::ExclusiveFullscreen)
        {
            spdlog::info(
                "Detected adapter change while in exclusive fullscreen mode.\nForcing the app into borderless fullscreen mode in case the new "
                "adapter does not have ownership of the display.");

            s_WindowMode = WindowMode::BorderlessFullscreen;

            UpdateWindowAndUpdateSwapChain();
        }

        WaitForDevice();

        spdlog::info("Updating Graphics Adapter");

        ReleaseGraphicsRuntime();

        InitializeGraphicsRuntime();
    }

    if ((s_UpdateFlags & UpdateFlags::SwapChainRecreate) != 0)
    {
        WaitForDevice();

        spdlog::info("Re-creating Swap Chain");

        ReleaseSwapChain();

        CreateSwapChain();
    }

    if ((s_UpdateFlags & UpdateFlags::SwapChainResize) != 0)
    {
        WaitForDevice();

        UpdateWindowAndUpdateSwapChain();
    }

    // Clear update flags.
    s_UpdateFlags = 0u;
}

void Render()
{
    SyncSettings();

    s_StopWatch.Read(s_DeltaTime);

    ThrowIfFailed(s_CommandAllocator->Reset());

    ThrowIfFailed(s_CommandList->Reset(s_CommandAllocator.Get(), nullptr));

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    auto presentToRenderBarrier = CD3DX12_RESOURCE_BARRIER::Transition(s_SwapChainImages[s_CurrentSwapChainImageIndex].Get(),
                                                                       D3D12_RESOURCE_STATE_PRESENT,
                                                                       D3D12_RESOURCE_STATE_RENDER_TARGET);
    s_CommandList->ResourceBarrier(1, &presentToRenderBarrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(s_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                            s_CurrentSwapChainImageIndex,
                                            s_RTVDescriptorSize);
    s_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Clear the overlay ui region.
    {
        const float clearColor[]   = { 1.0f, 1.0f, 1.0f, 1.0f };
        D3D12_RECT  clearColorRect = { 0, 0, static_cast<LONG>(0.25L * s_BackBufferSize.x), s_BackBufferSize.y };
        s_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &clearColorRect);
    }

    // CLear the normal rendering viewport region.
    {
        const float clearColor[]   = { 0.2f, 0.2f, 0.75f, 1.0f };
        D3D12_RECT  clearColorRect = { static_cast<LONG>(0.25L * s_BackBufferSize.x), 0, s_BackBufferSize.x, s_BackBufferSize.y };
        s_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &clearColorRect);
    }

    RenderInterface();
    ImGui::Render();

    s_CommandList->SetDescriptorHeaps(1, s_SRVDescriptorHeap.GetAddressOf());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), s_CommandList.Get());

    auto renderToPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(s_SwapChainImages[s_CurrentSwapChainImageIndex].Get(),
                                                                       D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                       D3D12_RESOURCE_STATE_PRESENT);
    s_CommandList->ResourceBarrier(1, &renderToPresentBarrier);

    ThrowIfFailed(s_CommandList->Close());

    ID3D12CommandList* ppCommandLists[] = { s_CommandList.Get() };
    s_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(s_DXGISwapChain->Present(s_SyncInterval, 0));

    WaitForDevice();
}

inline std::string HrToString(HRESULT hr)
{
    char s_str[64] = {};
    sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
    return std::string(s_str);
}

class HrException : public std::runtime_error
{
public:

    HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
    HRESULT Error() const { return m_hr; }

private:

    const HRESULT m_hr;
};

void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw HrException(hr);
    }
}

void WaitForDevice()
{
    ThrowIfFailed(s_CommandQueue->Signal(s_Fence.Get(), s_FenceValue));

    // Wait until the previous frame is finished.
    ThrowIfFailed(s_Fence->SetEventOnCompletion(s_FenceValue, s_FenceOperatingSystemEvent));
    WaitForSingleObject(s_FenceOperatingSystemEvent, INFINITE);

    s_FenceValue++;

    s_CurrentSwapChainImageIndex = s_DXGISwapChain->GetCurrentBackBufferIndex();
}
