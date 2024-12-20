// clang-format off
// Tell Windows to load the Agility SDK DLLs. 
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 715; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
// clang-format on

// Import the imgui style.
#include "OverlayStyle.h"

// Ref
// -----------------------------
// https://www.intel.com/content/www/us/en/developer/articles/code-sample/sample-application-for-direct3d-12-flip-model-swap-chains.html

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

// Report unreleased objects when this goes out of scope (after ComPtr).
D3DMemoryLeakReport gLeakReport;

HINSTANCE   gInstance     = nullptr;
GLFWwindow* gWindow       = nullptr;
HWND        gWindowNative = nullptr;

WindowMode gWindowMode     = WindowMode::Windowed;
WindowMode gWindowModePrev = WindowMode::Windowed;

int gCurrentSwapChainImageIndex;
int gRTVDescriptorSize;
int gSRVDescriptorSize;

ComPtr<ID3D12Device>              gLogicalDevice     = nullptr;
ComPtr<IDSRDevice>                gDSRDevice         = nullptr;
ComPtr<ID3D12CommandQueue>        gCommandQueue      = nullptr;
ComPtr<ID3D12DescriptorHeap>      gRTVDescriptorHeap = nullptr;
ComPtr<ID3D12DescriptorHeap>      gSRVDescriptorHeap = nullptr;
ComPtr<ID3D12CommandAllocator>    gCommandAllocator  = nullptr;
ComPtr<ID3D12GraphicsCommandList> gCommandList       = nullptr;

std::vector<ComPtr<ID3D12Resource>> gSwapChainImages;
uint32_t                            gSwapChainImageCount = 2;

int                                    gDSRVariantIndex;
std::vector<DSR_SUPERRES_VARIANT_DESC> gDSRVariantDescs;
std::vector<std::string>               gDSRVariantNames;

ComPtr<ID3D12Fence> gFence                     = nullptr;
HANDLE              gFenceOperatingSystemEvent = nullptr;
UINT64              gFenceValue                = 0U;

// Process -> Adapter -> Display (DXGI)
ComPtr<IDXGIAdapter1>            gDXGIAdapter;
ComPtr<IDXGIFactory6>            gDXGIFactory;
ComPtr<IDXGISwapChain3>          gDXGISwapChain;
std::vector<DXGI_ADAPTER_DESC1>  gDXGIAdapterInfos;
SwapEffect                       gDXGISwapEffect;
std::vector<ComPtr<IDXGIOutput>> gDXGIOutputs;
std::vector<std::string>         gDXGIOutputNames;
std::vector<DXGI_MODE_DESC>      gDXGIDisplayModes;

std::vector<DirectX::XMINT2> gDXGIDisplayResolutions;
std::vector<std::string>     gDXGIDisplayResolutionsStr;

std::vector<DXGI_RATIONAL> gDXGIDisplayRefreshRates;
std::vector<std::string>   gDXGIDisplayRefreshRatesStr;

int gDXGIAdapterIndex;
int gDXGIOutputsIndex;
int gDXGIDisplayResolutionsIndex;
int gDXGIDisplayRefreshRatesIndex;

// Adapted from all found DXGI_ADAPTER_DESC1 for easy display in the UI.
std::vector<std::string> gDXGIAdapterNames;

// Log buffer memory.
std::shared_ptr<std::stringstream> gLoggerMemory;

// Time since last present.
float gDeltaTime = 0.0;

// Maintain a 120-frame moving average for the delta time.
MovingAverage gDeltaTimeMovingAverage(60);

// Ring buffer for frame time (ms) + moving average
ScrollingBuffer gDeltaTimeBuffer;
ScrollingBuffer gDeltaTimeMovingAverageBuffer;

// V-Sync Interval requested by user.
int gSyncInterval;

// Current update flags for the frame.
uint32_t gUpdateFlags;

// Output (post-upscaled) viewport resolution.
DirectX::XMINT2 gBackBufferSize { 1280, 720 };
DirectX::XMINT2 gBackBufferSizePrev { 1280, 720 };

// Cached window rect if going from fullscreen -> window.
RECT gWindowRect;
UINT gWindowStyle = WS_OVERLAPPEDWINDOW | WS_SYSMENU;

StopWatch gStopWatch;

tbb::task_group gTaskGroup;

// Utility Prototypes
// -----------------------------

std::string FromWideStr(std::wstring str);

void CreateSwapChain();

void ReleaseSwapChain();

// Creates an OS window for Microsoft Windows.
void CreateOperatingSystemWindow();

// Enumerate a list of graphics adapters that support our usage of D3D12.
void EnumerateSupportedAdapters();

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
    gInstance = hInstance;

    // Flush on critical events since we usually fatally crash the app right after.
    spdlog::flush_on(spdlog::level::critical);

    // Configure logging.
    // --------------------------------------

    gLoggerMemory   = std::make_shared<std::stringstream>();
    auto loggerSink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*gLoggerMemory);
    auto logger     = std::make_shared<spdlog::logger>("", loggerSink);

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%l] %v");

#ifdef _DEBUG
    spdlog::set_level(spdlog::level::debug);
#endif

    // Overlap adapter enumeration with window creation.
    gTaskGroup.run([] { EnumerateSupportedAdapters(); });

    // Create operating system window.
    // -----------------------------------------
    {
        glfwInit();

        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        gWindow = glfwCreateWindow(gBackBufferSize.x, gBackBufferSize.y, "Image Clarity Reference", nullptr, nullptr);

        if (!gWindow)
            exit(1);

        gWindowNative = glfwGetWin32Window(gWindow);
    }

    // Wait for adapter enumeration to complete.
    gTaskGroup.wait();

    InitializeGraphicsRuntime();

    // Configure initial window size based on display-supplied resolutions.
    // -----------------------------------------
    {
        // Select a somewhat large initial resolution size from the list.
        gDXGIDisplayResolutionsIndex = std::max(0, static_cast<int>(gDXGIDisplayResolutions.size()) - 4);

        gBackBufferSize = gDXGIDisplayResolutions[gDXGIDisplayResolutionsIndex];

        // Select the largest refresh rate.
        gDXGIDisplayRefreshRatesIndex = static_cast<int>(gDXGIDisplayRefreshRates.size()) - 1;

        // Queue a resize on the next render call.
        gUpdateFlags |= UpdateFlags::SwapChainResize;
    }

    while (!glfwWindowShouldClose(gWindow))
    {
        glfwPollEvents();

        Render();
    }

    // Shut down imgui.
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
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

    gDXGIAdapterInfos.clear();
    gDXGIAdapterNames.clear();

    for (UINT adapterIndex = 0;
         SUCCEEDED(pDXGIFactory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter)));
         ++adapterIndex)
    {
        if (!SUCCEEDED(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
            continue;

        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);

        gDXGIAdapterInfos.push_back(desc);
    }

    SetDebugName(pDXGIFactory.Get(), L"Temporary Enumeration Adapter");

    // Extract a list of supported adapter names for presentation to the user.
    std::transform(gDXGIAdapterInfos.begin(),
                   gDXGIAdapterInfos.end(),
                   std::back_inserter(gDXGIAdapterNames),
                   [](const DXGI_ADAPTER_DESC1& adapter) { return FromWideStr(adapter.Description); });

    if (!gDXGIAdapterInfos.empty())
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
    swapChainDesc.BufferCount           = gSwapChainImageCount;
    swapChainDesc.Width                 = static_cast<UINT>(gBackBufferSize.x);
    swapChainDesc.Height                = static_cast<UINT>(gBackBufferSize.y);
    swapChainDesc.Format                = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SampleDesc.Count      = 1;

    switch (gDXGISwapEffect)
    {
        // D3D12 only support DXGI_EFFECT_FLIP_* so we need to adapt for it.
        case SwapEffect::FlipDiscard   : swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; break;
        case SwapEffect::FlipSequential: swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; break;
    }

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(gDXGIFactory->CreateSwapChainForHwnd(gCommandQueue.Get(), gWindowNative, &swapChainDesc, nullptr, nullptr, &swapChain));

    // Does not support fullscreen transitions.
    ThrowIfFailed(gDXGIFactory->MakeWindowAssociation(gWindowNative, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&gDXGISwapChain));
    gCurrentSwapChainImageIndex = gDXGISwapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    gSwapChainImages.resize(gSwapChainImageCount);

    for (UINT swapChainImageIndex = 0; swapChainImageIndex < gSwapChainImageCount; swapChainImageIndex++)
    {
        ThrowIfFailed(gDXGISwapChain->GetBuffer(swapChainImageIndex, IID_PPV_ARGS(&gSwapChainImages[swapChainImageIndex])));
        gLogicalDevice->CreateRenderTargetView(gSwapChainImages[swapChainImageIndex].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, gRTVDescriptorSize);
    }
}

void ReleaseSwapChain()
{
    if (gWindowMode == WindowMode::ExclusiveFullscreen)
    {
        // Need to force disable fullscreen in order to destroy swap chain.
        ThrowIfFailed(gDXGISwapChain->SetFullscreenState(false, nullptr));

        // Resize back to exclusize fullscreen on the next update.
        gUpdateFlags |= UpdateFlags::SwapChainResize;
    }

    for (auto& swapChainImageView : gSwapChainImages)
    {
        if (swapChainImageView)
            swapChainImageView.Reset();
    }

    gDXGISwapChain.Reset();
}

DirectX::XMINT2 gPreviousWindowedPos;
DirectX::XMINT2 gPreviousWindowedSize;
int             gDXGIDisplayResolutionsIndexPrev;

void UpdateWindowAndSwapChain()
{
    switch (gWindowMode)
    {
        case WindowMode::BorderlessFullscreen:
        case WindowMode::ExclusiveFullscreen:
        {
            if (gWindowModePrev != WindowMode::Windowed)
                break;

            // Cache the windowed window if making fullscreen transition.
            gDXGIDisplayResolutionsIndexPrev = gDXGIDisplayResolutionsIndex;
            glfwGetWindowPos(gWindow, &gPreviousWindowedPos.x, &gPreviousWindowedPos.y);
            glfwGetWindowSize(gWindow, &gPreviousWindowedSize.x, &gPreviousWindowedSize.y);
        }

        default: break;
    }

    // Handle DXGI / GLFW changes.
    switch (gWindowMode)
    {
        case WindowMode::Windowed:
        {
            ThrowIfFailed(gDXGISwapChain->SetFullscreenState(false, nullptr));
            glfwSetWindowSize(gWindow, gBackBufferSize.x, gBackBufferSize.y);
            glfwSetWindowAttrib(gWindow, GLFW_DECORATED, GLFW_TRUE);
            break;
        }

        case WindowMode::BorderlessFullscreen:
        {
            ThrowIfFailed(gDXGISwapChain->SetFullscreenState(false, nullptr));
            glfwSetWindowAttrib(gWindow, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(gWindow, nullptr, 0, 0, gBackBufferSize.x, gBackBufferSize.y, 0);
            break;
        }

        case WindowMode::ExclusiveFullscreen:
        {
            if (FAILED(gDXGISwapChain->SetFullscreenState(true, nullptr)))
            {
                spdlog::info("Failed to go fullscreen. The adapter may not own the output display. Reverting to borderless.");
                gWindowMode = WindowMode::BorderlessFullscreen;
                UpdateWindowAndSwapChain();
                return;
            }

            break;
        };
    }

    // Handle backbuffer sizes.
    switch (gWindowMode)
    {
        case WindowMode::Windowed:
        {
            if (gWindowModePrev != WindowMode::Windowed)
            {
                gDXGIDisplayResolutionsIndex = gDXGIDisplayResolutionsIndexPrev;

                // Restore cached window if making fullscreen transition.
                glfwSetWindowMonitor(gWindow,
                                     nullptr,
                                     gPreviousWindowedPos.x,
                                     gPreviousWindowedPos.y,
                                     gPreviousWindowedSize.x,
                                     gPreviousWindowedSize.y,
                                     0);
            }

            gBackBufferSize = gDXGIDisplayResolutions[gDXGIDisplayResolutionsIndex];
            break;
        }

        case WindowMode::BorderlessFullscreen:
        case WindowMode::ExclusiveFullscreen:
        {
            gDXGIDisplayResolutionsIndex = static_cast<int>(gDXGIDisplayResolutions.size()) - 1;
            gBackBufferSize              = gDXGIDisplayResolutions[gDXGIDisplayResolutionsIndex];
            break;
        }
    }

    spdlog::info("Modify Swap Chain ({}x{} --> {}x{} @ {} Hz)",
                 gBackBufferSizePrev.x,
                 gBackBufferSizePrev.y,
                 gBackBufferSize.x,
                 gBackBufferSize.y,
                 gDXGIDisplayRefreshRates[gDXGIDisplayRefreshRatesIndex].Numerator /
                     gDXGIDisplayRefreshRates[gDXGIDisplayRefreshRatesIndex].Denominator);

    DXGI_MODE_DESC targetDisplayMode = gDXGIDisplayModes[gDXGIDisplayModes.size() - 1];
    {
        targetDisplayMode.Width       = gBackBufferSize.x;
        targetDisplayMode.Height      = gBackBufferSize.y;
        targetDisplayMode.RefreshRate = gDXGIDisplayRefreshRates[gDXGIDisplayRefreshRatesIndex];
    }

    // Reconstruct the closest-matching display mode based on the user selections of resolution, refresh rate, etc.
    DXGI_MODE_DESC closestDisplayMode;
    ThrowIfFailed(gDXGIOutputs[gDXGIOutputsIndex]->FindClosestMatchingMode(&targetDisplayMode, &closestDisplayMode, nullptr));

    ThrowIfFailed(gDXGISwapChain->ResizeTarget(&closestDisplayMode));

    for (auto& swapChainImageView : gSwapChainImages)
        swapChainImageView.Reset();

    DXGI_SWAP_CHAIN_DESC swapChainInfo = {};
    gDXGISwapChain->GetDesc(&swapChainInfo);

    ThrowIfFailed(gDXGISwapChain->ResizeBuffers(gSwapChainImageCount,
                                                gBackBufferSize.x,
                                                gBackBufferSize.y,
                                                swapChainInfo.BufferDesc.Format,
                                                swapChainInfo.Flags));

    gCurrentSwapChainImageIndex = gDXGISwapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    gSwapChainImages.resize(gSwapChainImageCount);

    for (UINT swapChainImageIndex = 0; swapChainImageIndex < gSwapChainImageCount; swapChainImageIndex++)
    {
        ThrowIfFailed(gDXGISwapChain->GetBuffer(swapChainImageIndex, IID_PPV_ARGS(gSwapChainImages[swapChainImageIndex].GetAddressOf())));
        gLogicalDevice->CreateRenderTargetView(gSwapChainImages[swapChainImageIndex].Get(), nullptr, rtvHandle);

        rtvHandle.Offset(1, gRTVDescriptorSize);
    }

    gBackBufferSizePrev = gBackBufferSize;
    gWindowModePrev     = gWindowMode;
}

void EnumerateOutputDisplays()
{
    // NOTE: Some systems (i.e. laptops with dedicate GPU) fail to enumrate any outputs
    //       due to ownership of the output being with the integrated GPU (i.e. Intel).
    //       Thus, we enumerate all adapters first and check each output that adapter owns.

    for (auto& pOutput : gDXGIOutputs)
        pOutput.Reset();

    gDXGIOutputs.clear();
    gDXGIOutputNames.clear();

    IDXGIAdapter* pAdapter;
    UINT          adapterIndex = 0u;

    while (gDXGIFactory->EnumAdapters(adapterIndex++, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        IDXGIOutput* pOutput;
        UINT         outputIndex = 0u;

        while (pAdapter->EnumOutputs(outputIndex++, &pOutput) != DXGI_ERROR_NOT_FOUND)
        {
            gDXGIOutputs.push_back(pOutput);

            pOutput->Release();
        }

        pAdapter->Release();
    }

    // Extract list of names for overlay.
    std::transform(gDXGIOutputs.begin(),
                   gDXGIOutputs.end(),
                   std::back_inserter(gDXGIOutputNames),
                   [](const ComPtr<IDXGIOutput>& pOutput)
                   {
                       DXGI_OUTPUT_DESC outputInfo;
                       pOutput->GetDesc(&outputInfo);

                       return FromWideStr(outputInfo.DeviceName);
                   });
}

void EnumerateDisplayModes()
{
    UINT displayModeCount;
    ThrowIfFailed(gDXGIOutputs[gDXGIOutputsIndex]->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &displayModeCount, nullptr));

    gDXGIDisplayModes.resize(displayModeCount);

    ThrowIfFailed(gDXGIOutputs[gDXGIOutputsIndex]->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &displayModeCount, gDXGIDisplayModes.data()));

    // Extract list of unique resolutions. (There may be multiples of the same resolution due
    // to various refresh rates, dxgi formats, etc.).
    // ----------------------------------------------------------
    {
        std::set<DirectX::XMINT2, XMINT2Cmp> uniqueDisplayModeResolutions;

        std::transform(gDXGIDisplayModes.begin(),
                       gDXGIDisplayModes.end(),
                       std::inserter(uniqueDisplayModeResolutions, uniqueDisplayModeResolutions.begin()),
                       [&](const DXGI_MODE_DESC& mode) { return DirectX::XMINT2(mode.Width, mode.Height); });

        gDXGIDisplayResolutions.resize(uniqueDisplayModeResolutions.size());
        std::copy(uniqueDisplayModeResolutions.begin(), uniqueDisplayModeResolutions.end(), gDXGIDisplayResolutions.begin());

        gDXGIDisplayResolutionsStr.clear();
        std::transform(gDXGIDisplayResolutions.begin(),
                       gDXGIDisplayResolutions.end(),
                       std::back_inserter(gDXGIDisplayResolutionsStr),
                       [&](const DirectX::XMINT2& r) { return std::format("{} x {}", r.x, r.y); });
    }

    // Extract list of refresh rates.
    // ----------------------------------------------------------
    {
        std::set<DXGI_RATIONAL, RefreshRateCmp> uniqueRefreshRates;

        std::transform(gDXGIDisplayModes.begin(),
                       gDXGIDisplayModes.end(),
                       std::inserter(uniqueRefreshRates, uniqueRefreshRates.begin()),
                       [&](const DXGI_MODE_DESC& mode) { return mode.RefreshRate; });

        gDXGIDisplayRefreshRates.resize(uniqueRefreshRates.size());
        std::copy(uniqueRefreshRates.begin(), uniqueRefreshRates.end(), gDXGIDisplayRefreshRates.begin());

        gDXGIDisplayRefreshRatesStr.clear();
        std::transform(gDXGIDisplayRefreshRates.begin(),
                       gDXGIDisplayRefreshRates.end(),
                       std::back_inserter(gDXGIDisplayRefreshRatesStr),
                       [&](const DXGI_RATIONAL& r) { return std::format("{} Hz", r.Numerator / r.Denominator); });
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

    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&gDXGIFactory)));

    ComPtr<IDXGIAdapter1> pAdapter;
    for (UINT adapterIndex = 0; SUCCEEDED(gDXGIFactory->EnumAdapters1(adapterIndex, &pAdapter)); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);

        if (gDXGIAdapterInfos[gDXGIAdapterIndex].DeviceId == desc.DeviceId)
            break;
    }

    pAdapter.As(&gDXGIAdapter);

    SetDebugName(
        gDXGIAdapter.Get(),
        std::format(L"Adapter {}", std::wstring(gDXGIAdapterNames[gDXGIAdapterIndex].begin(), gDXGIAdapterNames[gDXGIAdapterIndex].end())).c_str());

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

    ThrowIfFailed(D3D12CreateDevice(gDXGIAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&gLogicalDevice)));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type                     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(gLogicalDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gCommandQueue)));

    // Descriptor heaps.
    // ------------------------------------------

    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors             = 32;
        rtvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(gLogicalDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&gRTVDescriptorHeap)));

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors             = 32;
        srvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(gLogicalDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&gSRVDescriptorHeap)));

        gRTVDescriptorSize = gLogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        gSRVDescriptorSize = gLogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // ------------------------------------------

    if (gDXGISwapChain)
        ReleaseSwapChain();

    CreateSwapChain();

    // ------------------------------------------

    ThrowIfFailed(gLogicalDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gCommandAllocator)));

    SetDebugName(gCommandAllocator.Get(), L"Command Allocator");

    // Initialize DirectSR device.
    // ------------------------------------------

    do
    {
        ComPtr<ID3D12DSRDeviceFactory> pDSRDeviceFactory;
        if (FAILED(D3D12GetInterface(CLSID_D3D12DSRDeviceFactory, IID_PPV_ARGS(&pDSRDeviceFactory))))
            break;

        if (FAILED(pDSRDeviceFactory->CreateDSRDevice(gLogicalDevice.Get(), 1, IID_PPV_ARGS(&gDSRDevice))))
            break;

        gDSRVariantDescs.resize(gDSRDevice->GetNumSuperResVariants());

        for (UINT variantIndex = 0; variantIndex < gDSRVariantDescs.size(); variantIndex++)
        {
            DSR_SUPERRES_VARIANT_DESC variantDesc;
            ThrowIfFailed(gDSRDevice->GetSuperResVariantDesc(variantIndex, &variantDesc));

            gDSRVariantDescs[variantIndex] = variantDesc;
        }

        gDSRVariantNames.clear();
        gDSRVariantIndex = 0;

        // Enumerate variants names.
        std::transform(gDSRVariantDescs.begin(),
                       gDSRVariantDescs.end(),
                       std::back_inserter(gDSRVariantNames),
                       [](const DSR_SUPERRES_VARIANT_DESC& variantDesc) { return variantDesc.VariantName; });
    }
    while (false);

    ThrowIfFailed(
        gLogicalDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&gCommandList)));

    ThrowIfFailed(gCommandList->Close());

    // Frames-in-flight synchronization objects.
    // ------------------------------------------

    {
        ThrowIfFailed(gLogicalDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence)));
        gFenceValue = 1;

        // Create an event handle to use for frame synchronization.
        if (!gFenceOperatingSystemEvent)
            gFenceOperatingSystemEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (gFenceOperatingSystemEvent == nullptr)
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

        // Wait for the command list to execute; we are reusing the same command
        // list in our main loop but for now, we just want to wait for setup to
        // complete before continuing.
        WaitForDevice();
    }

    // Initialize ImGui.
    // ------------------------------------------

    {
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();

        ImGuiIO& io = ImGui::GetIO();

        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // NOTE: Enabling this causes a 200+ ms hitch on the first NewFrame() call.
        // Disable it for now to remove that hitch.
        // io.ConfigFlags |= ImGuiConfigFlaggNavEnableGamepad;

        // Auto-install message callbacks.
        ImGui_ImplGlfw_InitForOther(gWindow, true);

        auto srvHeapCPUHandle = gSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        auto srvHeapGPUHandle = gSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

        ImGui_ImplDX12_Init(gLogicalDevice.Get(),
                            DXGI_MAX_SWAP_CHAIN_BUFFERS,
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            gSRVDescriptorHeap.Get(),
                            srvHeapCPUHandle,
                            srvHeapGPUHandle);

        SetStyle();
    }

    // Log a message containing information about the adapter.
    // ------------------------------------------

    {
        spdlog::info("Device: {}", gDXGIAdapterNames[gDXGIAdapterIndex]);
        spdlog::info("VRAM:   {} GB", gDXGIAdapterInfos[gDXGIAdapterIndex].DedicatedVideoMemory / static_cast<float>(1024 * 1024 * 1024));
    }
}

void RenderInterface()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)gBackBufferSize.x * 0.25f, (float)gBackBufferSize.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);

    if (ImGui::CollapsingHeader("Presentation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        StringListDropdown("Display", gDXGIOutputNames, gDXGIOutputsIndex);

        ImGui::BeginDisabled(gWindowMode != WindowMode::Windowed);

        if (StringListDropdown("Resolution", gDXGIDisplayResolutionsStr, gDXGIDisplayResolutionsIndex))
            gUpdateFlags |= UpdateFlags::SwapChainResize;

        ImGui::EndDisabled();

        ImGui::BeginDisabled(gWindowMode != WindowMode::ExclusiveFullscreen);

        if (StringListDropdown("Refresh Rate", gDXGIDisplayRefreshRatesStr, gDXGIDisplayRefreshRatesIndex))
            gUpdateFlags |= UpdateFlags::SwapChainResize;

        ImGui::EndDisabled();

        if (StringListDropdown("Adapter", gDXGIAdapterNames, gDXGIAdapterIndex))
            gUpdateFlags |= UpdateFlags::GraphicsRuntime;

        if (EnumDropdown<WindowMode>("Window Mode", reinterpret_cast<int*>(&gWindowMode)))
            gUpdateFlags |= UpdateFlags::SwapChainResize;

        if (EnumDropdown<SwapEffect>("Swap Effect", reinterpret_cast<int*>(&gDXGISwapEffect)))
            gUpdateFlags |= UpdateFlags::SwapChainRecreate;

        ImGui::SliderInt("V-Sync Interval", &gSyncInterval, 0, 4);

#if 0
        if (ImGui::SliderInt("Buffering", reinterpret_cast<int*>(&gSwapChainImageCount), 2, DXGI_MAX_SWAP_CHAIN_BUFFERS - 1))
            gUpdateFlags |= UpdateFlags::SwapChainRecreate;

        static int gFramesInFlightCount = 1;
        ImGui::SliderInt("Frames in Flight", &gFramesInFlightCount, 1, 16);
#endif
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!gDSRVariantDescs.empty())
            StringListDropdown("DirectSR Algorithm", gDSRVariantNames, gDSRVariantIndex);
        else
            StringListDropdown("DirectSR Algorithm", { "None" }, gDSRVariantIndex);
    }

    if (ImGui::CollapsingHeader("Analysis", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("TODO");
    }

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static float elapsedTime = 0;
        elapsedTime += gDeltaTime;

        constexpr std::array<const char*, 2> graphModes = { "Frame Time (Milliseconds)", "Frames-per-Second" };

        static int selectedPerformanceGraphMode;
        StringListDropdown("Graph Mode", graphModes.data(), graphModes.size(), selectedPerformanceGraphMode);

        switch (selectedPerformanceGraphMode)
        {
            case 0:
            {
                float deltaTimeMs = 1000.0f * gDeltaTime;
                gDeltaTimeMovingAverage.AddValue(deltaTimeMs);
                gDeltaTimeBuffer.AddPoint(elapsedTime, deltaTimeMs);
                gDeltaTimeMovingAverageBuffer.AddPoint(elapsedTime, gDeltaTimeMovingAverage.GetAverage());
                break;
            }

            case 1:
            {
                float framesPerSecond = 1.0f / gDeltaTime;
                gDeltaTimeMovingAverage.AddValue(framesPerSecond);
                gDeltaTimeBuffer.AddPoint(elapsedTime, framesPerSecond);
                gDeltaTimeMovingAverageBuffer.AddPoint(elapsedTime, gDeltaTimeMovingAverage.GetAverage());
                break;
            }
        }

        static float history = 3.0f;

        if (ImPlot::BeginPlot("##PerformanceChild", ImVec2(-1, 150)))
        {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, 0x0);
            ImPlot::SetupAxisLimits(ImAxis_X1, elapsedTime - history, elapsedTime, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, gDeltaTimeMovingAverage.GetAverage() * 2.0, ImGuiCond_Always);

            ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 1.0);

            // Custom tick label showing the average ms/fps only.
            {
                double middleTick = gDeltaTimeMovingAverage.GetAverage();

                // Define the label for the middle tick
                auto        averageStr      = std::format("{:.2f}", gDeltaTimeMovingAverage.GetAverage());
                const char* middleTickLabel = averageStr.c_str();

                // Set the custom ticks on the y-axis
                ImPlot::SetupAxisTicks(ImAxis_Y1, &middleTick, 1, &middleTickLabel);
            }

            ImPlot::PlotLine("Exact",
                             &gDeltaTimeBuffer.mData[0].x,
                             &gDeltaTimeBuffer.mData[0].y,
                             gDeltaTimeBuffer.mData.size(),
                             ImPlotLineFlags_None,
                             gDeltaTimeBuffer.mOffset,
                             2 * sizeof(float));

            ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 2.0);

            ImPlot::PlotLine("Smoothed",
                             &gDeltaTimeMovingAverageBuffer.mData[0].x,
                             &gDeltaTimeMovingAverageBuffer.mData[0].y,
                             gDeltaTimeMovingAverageBuffer.mData.size(),
                             ImPlotLineFlags_None,
                             gDeltaTimeMovingAverageBuffer.mOffset,
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
            ImGui::TextUnformatted(gLoggerMemory->str().c_str());

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0F);
        }
        ImGui::EndChild();

        ImGui::PopStyleColor(2);
    }

    ImGui::PopItemWidth();

    ImGui::End();

#if 0
    ImGui::SetNextWindowPos(ImVec2((float)gBackBufferSize.x * 0.25f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2((float)gBackBufferSize.x * 0.75f, (float)gBackBufferSize.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlaggNoMove | ImGuiWindowFlaggNoCollapse);

    ImGui::End();
#endif
}

void SyncSettings()
{
    if ((gUpdateFlags & UpdateFlags::GraphicsRuntime) != 0)
    {
        if (gWindowMode == WindowMode::ExclusiveFullscreen)
        {
            spdlog::info(
                "Detected adapter change while in exclusive fullscreen mode.\nForcing the app into borderless fullscreen mode in case the new "
                "adapter does not have ownership of the display.");

            gWindowMode = WindowMode::BorderlessFullscreen;

            UpdateWindowAndSwapChain();
        }

        WaitForDevice();

        spdlog::info("Updating Graphics Adapter");

        InitializeGraphicsRuntime();
    }

    if ((gUpdateFlags & UpdateFlags::SwapChainRecreate) != 0)
    {
        WaitForDevice();

        spdlog::info("Re-creating Swap Chain");

        ReleaseSwapChain();

        CreateSwapChain();
    }

    if ((gUpdateFlags & UpdateFlags::SwapChainResize) != 0)
    {
        WaitForDevice();

        UpdateWindowAndSwapChain();
    }

    // Clear update flags.
    gUpdateFlags = 0u;
}

void Render()
{
    SyncSettings();

    gStopWatch.Read(gDeltaTime);

    ThrowIfFailed(gCommandAllocator->Reset());

    ThrowIfFailed(gCommandList->Reset(gCommandAllocator.Get(), nullptr));

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    auto presentToRenderBarrier = CD3DX12_RESOURCE_BARRIER::Transition(gSwapChainImages[gCurrentSwapChainImageIndex].Get(),
                                                                       D3D12_RESOURCE_STATE_PRESENT,
                                                                       D3D12_RESOURCE_STATE_RENDER_TARGET);
    gCommandList->ResourceBarrier(1, &presentToRenderBarrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                            gCurrentSwapChainImageIndex,
                                            gRTVDescriptorSize);
    gCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Clear the overlay ui region.
    {
        const float clearColor[]   = { 1.0f, 1.0f, 1.0f, 1.0f };
        D3D12_RECT  clearColorRect = { 0, 0, static_cast<LONG>(0.25L * gBackBufferSize.x), gBackBufferSize.y };
        gCommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &clearColorRect);
    }

    // CLear the normal rendering viewport region.
    {
        const float clearColor[]   = { 0.2f, 0.2f, 0.75f, 1.0f };
        D3D12_RECT  clearColorRect = { static_cast<LONG>(0.25L * gBackBufferSize.x), 0, gBackBufferSize.x, gBackBufferSize.y };
        gCommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &clearColorRect);
    }

    RenderInterface();
    ImGui::Render();

    gCommandList->SetDescriptorHeaps(1, gSRVDescriptorHeap.GetAddressOf());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList.Get());

    auto renderToPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(gSwapChainImages[gCurrentSwapChainImageIndex].Get(),
                                                                       D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                       D3D12_RESOURCE_STATE_PRESENT);
    gCommandList->ResourceBarrier(1, &renderToPresentBarrier);

    ThrowIfFailed(gCommandList->Close());

    ID3D12CommandList* ppCommandLists[] = { gCommandList.Get() };
    gCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(gDXGISwapChain->Present(gSyncInterval, 0));

    WaitForDevice();
}

inline std::string HrToString(HRESULT hr)
{
    char gstr[64] = {};
    sprintf_s(gstr, "HRESULT of 0x%08X", static_cast<UINT>(hr));
    return std::string(gstr);
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
    ThrowIfFailed(gCommandQueue->Signal(gFence.Get(), gFenceValue));

    // Wait until the previous frame is finished.
    ThrowIfFailed(gFence->SetEventOnCompletion(gFenceValue, gFenceOperatingSystemEvent));
    WaitForSingleObject(gFenceOperatingSystemEvent, INFINITE);

    gFenceValue++;

    gCurrentSwapChainImageIndex = gDXGISwapChain->GetCurrentBackBufferIndex();
}
