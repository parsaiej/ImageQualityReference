// clang-format off
// Tell Windows to load the Agility SDK DLLs. 
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 715; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
// clang-format on

#include <Util.h>
#include <RenderInput.h>
#include <RenderInputShaderToy.h>
#include <Interface.h>
#include <Blitter.h>
#include <ResourceRegistry.h>

using namespace ICR;

// Ref
// -----------------------------
// https://www.intel.com/content/www/us/en/developer/articles/code-sample/sample-application-for-direct3d-12-flip-model-swap-chains.html

// Initialize State
// -----------------------------

namespace ICR
{
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
    int gSMPDescriptorSize;

    ComPtr<ID3D12Device>              gLogicalDevice          = nullptr;
    ComPtr<IDSRDevice>                gDSRDevice              = nullptr;
    ComPtr<ID3D12CommandQueue>        gCommandQueue           = nullptr;
    ComPtr<ID3D12DescriptorHeap>      gImguiDescriptorHeapSRV = nullptr;
    ComPtr<ID3D12CommandAllocator>    gCommandAllocator       = nullptr;
    ComPtr<ID3D12GraphicsCommandList> gCommandList            = nullptr;
    ComPtr<D3D12MA::Allocator>        gMemoryAllocator        = nullptr;

    std::vector<ResourceHandle> gSwapChainImageHandles;
    uint32_t                    gSwapChainImageCount = 2;

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
    D3D12_VIEWPORT  gViewport;

    // Cached window rect if going from fullscreen -> window.
    RECT gWindowRect;
    UINT gWindowStyle = WS_OVERLAPPEDWINDOW | WS_SYSMENU;

    StopWatch gStopWatch;

    std::unique_ptr<RenderInput> gRenderInput;
    RenderInputMode              gRenderInputMode = RenderInputMode::ShaderToy;

    tbb::task_group gTaskGroup;

    std::queue<std::function<void()>> gPreRenderTaskQueue;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> gShaderDXIL;

    std::unique_ptr<Blitter> gBlitter;

    std::unique_ptr<ResourceRegistry> gResourceRegistry;
} // namespace ICR

// Utility Prototypes
// -----------------------------

// Message handle for a Microsoft Windows window.
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void CreateSwapChain();

void ReleaseSwapChain();

// Enumerate a list of graphics adapters that support our usage of D3D12.
void EnumerateSupportedAdapters();

// Create a DXGI swap-chain for the OS window.
void InitializeGraphicsRuntime();

// Command list recording and queue submission for current swap chain image.
void Render();

// Use fence primitives to pause the thread until the previous swap-chain image has finished being drawn and presented.
void WaitForDevice();

// Syncs runtime context with user settings.
void SyncSettings();

// Entry-point
// -----------------------------

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
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

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        gWindow = glfwCreateWindow(gBackBufferSize.x, gBackBufferSize.y, "Image Clarity Reference", nullptr, nullptr);

        if (!gWindow)
            exit(1);

        gWindowNative = glfwGetWin32Window(gWindow);
    }

    // Wait for adapter enumeration to complete.
    gTaskGroup.wait();

    // We use glslang in case of shadertoy shader compilation to DXIL.
    glslang::InitializeProcess();

    // Fetch all shader bytecodes.
    LoadShaderByteCodes(gShaderDXIL);

    InitializeGraphicsRuntime();

    // Create blitting instance.
    gBlitter = std::make_unique<Blitter>();

    // Configure initial window size based on display-supplied resolutions.
    // -----------------------------------------
    {
        constexpr DirectX::XMINT2 kTargetInitialResolution { 1280, 720 };

        gDXGIDisplayResolutionsIndex = -1;

        // Select a resolution that matches a target one.
        for (int resolutionIndex = 0; resolutionIndex < static_cast<int>(gDXGIDisplayResolutions.size()); resolutionIndex++)
        {
            if (gDXGIDisplayResolutions[resolutionIndex].x == kTargetInitialResolution.x &&
                gDXGIDisplayResolutions[resolutionIndex].y == kTargetInitialResolution.y)
            {
                gDXGIDisplayResolutionsIndex = resolutionIndex;
                break;
            }
        }

        if (gDXGIDisplayResolutionsIndex < 0)
        {
            // Select a somewhat large initial resolution size from the list.
            gDXGIDisplayResolutionsIndex = std::max(0, static_cast<int>(gDXGIDisplayResolutions.size()) - 4);
        }

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

    // Before swapchain is released we need to close the fullscreen state.
    if (gWindowMode == WindowMode::ExclusiveFullscreen)
        gDXGISwapChain->SetFullscreenState(false, nullptr);

    Interface::Release();

    if (gRenderInput)
        gRenderInput->Release();

    glslang::FinalizeProcess();

    glfwDestroyWindow(gWindow);
    glfwTerminate();

    return 0;
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

    ThrowIfFailed(gDXGIFactory->MakeWindowAssociation(gWindowNative, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&gDXGISwapChain));
    gCurrentSwapChainImageIndex = gDXGISwapChain->GetCurrentBackBufferIndex();

    gSwapChainImageHandles.resize(gSwapChainImageCount);

    for (UINT swapChainImageIndex = 0; swapChainImageIndex < gSwapChainImageCount; swapChainImageIndex++)
    {
        ComPtr<ID3D12Resource> pSwapChainBuffer;
        ThrowIfFailed(gDXGISwapChain->GetBuffer(swapChainImageIndex, IID_PPV_ARGS(&pSwapChainBuffer)));

        // Wrap a handle around the swap chain buffer and create a RTV/SRV.
        gSwapChainImageHandles[swapChainImageIndex] =
            gResourceRegistry->Create(pSwapChainBuffer.Detach(), DescriptorHeap::Type::RenderTarget | DescriptorHeap::Type::Texture2D);
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

    for (auto& swapChainImageHandle : gSwapChainImageHandles)
        gResourceRegistry->Release(swapChainImageHandle);

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

            DXGI_OUTPUT_DESC outputInfo;
            gDXGIOutputs[gDXGIOutputsIndex]->GetDesc(&outputInfo);

            glfwSetWindowAttrib(gWindow, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(gWindow,
                                 nullptr,
                                 outputInfo.DesktopCoordinates.left,
                                 outputInfo.DesktopCoordinates.top,
                                 gBackBufferSize.x,
                                 gBackBufferSize.y,
                                 0);
            break;
        }

        case WindowMode::ExclusiveFullscreen:
        {
            if (FAILED(gDXGISwapChain->SetFullscreenState(true, gDXGIOutputs[gDXGIOutputsIndex].Get())))
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

    gViewport = {};
    {
        gViewport.TopLeftX = gBackBufferSize.x * 0.25f;
        gViewport.TopLeftY = 0;
        gViewport.Width    = gBackBufferSize.x * 0.75f;
        gViewport.Height   = gBackBufferSize.y * 0.75f;
        gViewport.MinDepth = 0.0f;
        gViewport.MaxDepth = 1.0f;
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

    // Need to release the swap chain image views before resizing the swap chain.
    for (auto& swapChainImageHandle : gSwapChainImageHandles)
        gResourceRegistry->Release(swapChainImageHandle);

    DXGI_SWAP_CHAIN_DESC swapChainInfo = {};
    gDXGISwapChain->GetDesc(&swapChainInfo);

    ThrowIfFailed(gDXGISwapChain->ResizeBuffers(gSwapChainImageCount,
                                                gBackBufferSize.x,
                                                gBackBufferSize.y,
                                                swapChainInfo.BufferDesc.Format,
                                                swapChainInfo.Flags));

    gCurrentSwapChainImageIndex = gDXGISwapChain->GetCurrentBackBufferIndex();

    gSwapChainImageHandles.resize(gSwapChainImageCount);

    for (UINT swapChainImageIndex = 0; swapChainImageIndex < gSwapChainImageCount; swapChainImageIndex++)
    {
        ComPtr<ID3D12Resource> pSwapChainBuffer;
        ThrowIfFailed(gDXGISwapChain->GetBuffer(swapChainImageIndex, IID_PPV_ARGS(&pSwapChainBuffer)));

        gSwapChainImageHandles[swapChainImageIndex] =
            gResourceRegistry->Create(pSwapChainBuffer.Detach(), DescriptorHeap::Type::RenderTarget | DescriptorHeap::Type::Texture2D);
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
    if (gRenderInput)
        gRenderInput->Release();

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

    if (FAILED(D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, NULL, NULL)))
    {
        spdlog::warn(
            "Failed to enable experimental shader models. This may happen if Windows Developer Mode is disabled. Shader Toy shaders will not "
            "compile.");
    }

    ThrowIfFailed(D3D12CreateDevice(gDXGIAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&gLogicalDevice)));

    D3D12MA::ALLOCATOR_DESC memoryAllocatorDesc = {};
    {
        memoryAllocatorDesc.pDevice  = gLogicalDevice.Get();
        memoryAllocatorDesc.pAdapter = gDXGIAdapter.Get();
        memoryAllocatorDesc.Flags    = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED | D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
    }
    ThrowIfFailed(D3D12MA::CreateAllocator(&memoryAllocatorDesc, &gMemoryAllocator));

    gResourceRegistry = std::make_unique<ResourceRegistry>();

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type                     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(gLogicalDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gCommandQueue)));

    // Determine the size of descriptor type stride.
    gRTVDescriptorSize = gLogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    gSRVDescriptorSize = gLogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    gSMPDescriptorSize = gLogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    // Descriptor heaps.
    // ------------------------------------------

    {
        // Dedicated descriptor heap for imgui shader resource.
        D3D12_DESCRIPTOR_HEAP_DESC imguiDescriptorHeapDescSRV = {};
        imguiDescriptorHeapDescSRV.NumDescriptors             = 1;
        imguiDescriptorHeapDescSRV.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        imguiDescriptorHeapDescSRV.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(gLogicalDevice->CreateDescriptorHeap(&imguiDescriptorHeapDescSRV, IID_PPV_ARGS(&gImguiDescriptorHeapSRV)));
    }

    // ------------------------------------------

    if (gDXGISwapChain)
        ReleaseSwapChain();

    CreateSwapChain();

    // ------------------------------------------

    ThrowIfFailed(gLogicalDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gCommandAllocator)));

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

    Interface::Create();

    // Log a message containing information about the adapter.
    // ------------------------------------------

    {
        auto vram = gDXGIAdapterInfos[gDXGIAdapterIndex].DedicatedVideoMemory / static_cast<float>(1024 * 1024 * 1024);
        spdlog::info("Device: {} ({:.2f} GB)", gDXGIAdapterNames[gDXGIAdapterIndex], vram);
    }

    if (gRenderInput)
        gRenderInput->Initialize();
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

    if ((gUpdateFlags & UpdateFlags::RenderInputChanged) != 0)
    {
        if (gRenderInput)
            gRenderInput->Release();

        switch (gRenderInputMode)
        {
            case RenderInputMode::ShaderToy:
            {
                gRenderInput = std::make_unique<RenderInputShaderToy>();
                break;
            }

            case RenderInputMode::OpenUSD:
            {
                gRenderInput = nullptr;
                break;
            }
        }

        if (gRenderInput)
            gRenderInput->Initialize();
    }

    // Clear update flags.
    gUpdateFlags = 0u;
}

void Render()
{
    SyncSettings();

    // Process pre-render tasks
    while (!gPreRenderTaskQueue.empty())
    {
        gPreRenderTaskQueue.front()();
        gPreRenderTaskQueue.pop();
    }

    gStopWatch.Read(gDeltaTime);

    ThrowIfFailed(gCommandAllocator->Reset());

    ThrowIfFailed(gCommandList->Reset(gCommandAllocator.Get(), nullptr));

    auto* pCurrentSwapChainImage = gResourceRegistry->Get(gSwapChainImageHandles[gCurrentSwapChainImageIndex]);

    auto presentToRenderBarrier =
        CD3DX12_RESOURCE_BARRIER::Transition(pCurrentSwapChainImage, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    gCommandList->ResourceBarrier(1, &presentToRenderBarrier);

    // Obtain the render target descriptor heap.
    auto renderTargetDescriptorHeap = gResourceRegistry->GetDescriptorHeap(DescriptorHeap::Type::RenderTarget);

    // Obtain the render target view for the current swap chain image.
    auto currentSwapChainBufferRTV =
        renderTargetDescriptorHeap->GetAddressCPU(gSwapChainImageHandles[gCurrentSwapChainImageIndex].indexDescriptorRenderTarget);

    gCommandList->OMSetRenderTargets(1, &currentSwapChainBufferRTV, FALSE, nullptr);

    // Clear the overlay ui region.
    {
        const float clearColor[]   = { 0.0f, 0.0f, 0.0f, 0.0f };
        D3D12_RECT  clearColorRect = { 0, 0, gBackBufferSize.x, gBackBufferSize.y };
        gCommandList->ClearRenderTargetView(currentSwapChainBufferRTV, clearColor, 1, &clearColorRect);
    }

    if (gRenderInput)
        gRenderInput->Render({ gCommandList.Get(), currentSwapChainBufferRTV });

    // Reset the backbuffer as render target.
    gCommandList->OMSetRenderTargets(1, &currentSwapChainBufferRTV, FALSE, nullptr);

    Interface::Draw();

    gCommandList->SetDescriptorHeaps(1, gImguiDescriptorHeapSRV.GetAddressOf());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList.Get());

    auto renderToPresentBarrier =
        CD3DX12_RESOURCE_BARRIER::Transition(pCurrentSwapChainImage, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    gCommandList->ResourceBarrier(1, &renderToPresentBarrier);

    ThrowIfFailed(gCommandList->Close());

    ID3D12CommandList* ppCommandLists[] = { gCommandList.Get() };
    gCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(gDXGISwapChain->Present(gSyncInterval, 0));

    WaitForDevice();
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
