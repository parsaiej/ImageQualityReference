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
    None            = 0,
    Window          = 1 << 0,
    SwapChain       = 1 << 1,
    GraphicsRuntime = 1 << 2
};

// State
// -----------------------------

HINSTANCE s_Instance = nullptr;
HWND      s_Window   = nullptr;

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
int                             s_DXGIAdapterIndex;
SwapEffect                      s_DXGISwapEffect;
std::vector<IDXGIOutput*>       s_DXGIOutputs;
std::vector<std::string>        s_DXGIOutputNames;
std::vector<DXGI_MODE_DESC>     s_DXGIDisplayModes;
std::vector<std::string>        s_DXGIDisplayModesStr;
int                             s_DXGIOutputsIndex;

// Adapted from all found DXGI_ADAPTER_DESC1 for easy display in the UI.
std::vector<std::string> s_DXGIAdapterNames;

// Log buffer memory.
std::shared_ptr<std::stringstream> s_LoggerMemory;

// Time since last present.
float s_DeltaTime = 0.0;

// Maintain a 120-frame moving average for the delta time.
MovingAverage s_DeltaTimeMovingAverage(240);

// Ring buffer for frame time (ms) + moving average
ScrollingBuffer s_DeltaTimeBuffer;
ScrollingBuffer s_DeltaTimeMovingAverageBuffer;

// V-Sync Interval requested by user.
int s_SyncInterval;

// Current update flags for the frame.
uint32_t s_UpdateFlags;

// Internal (pre-upscaled) viewport resolution.
DirectX::XMINT2 s_ViewportSizeInternal { 1920, 1080 };

// Output (post-upscaled) viewport resolution.
DirectX::XMINT2 s_ViewportSizeOutput { s_ViewportSizeInternal };

// Cached window rect if going from fullscreen -> window.
RECT s_WindowRect;
UINT s_WindowStyle = WS_OVERLAPPEDWINDOW | WS_SYSMENU;

StopWatch s_StopWatch;

// Utility Prototypes
// -----------------------------

std::string FromWideStr(std::wstring str);

void CreateSwapChain();

void ReleaseSwapChain();

void ResizeSwapChain();

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
    spdlog::set_pattern("%v");

#ifdef _DEBUG
    spdlog::set_level(spdlog::level::debug);
#endif

    EnumerateSupportedAdapters();

    CreateOperatingSystemWindow();

    InitializeGraphicsRuntime();

    ShowWindow(s_Window, nCmdShow);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // Process any messages in the queue.
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    ReleaseGraphicsRuntime();

    for (auto& pOutput : s_DXGIOutputs)
    {
        if (pOutput)
            pOutput->Release();
    }

    // Return this part of the WM_QUIT message to Windows.
    return static_cast<char>(msg.wParam);
}

inline UpdateFlags operator|(UpdateFlags a, UpdateFlags b) { return static_cast<UpdateFlags>(static_cast<int>(a) | static_cast<int>(b)); }

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

bool StringListDropdown(const char* name, std::vector<std::string>& strings, int& selectedIndex)
{
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

void CreateOperatingSystemWindow()
{
    WNDCLASSEX windowClass = { 0 };
    {
        windowClass.cbSize        = sizeof(WNDCLASSEX);
        windowClass.style         = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc   = WindowProc;
        windowClass.hInstance     = s_Instance;
        windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
        windowClass.lpszClassName = "ImageClarityReference";
    }
    RegisterClassEx(&windowClass);

    RECT windowRect = { 0, 0, s_ViewportSizeOutput.x, s_ViewportSizeOutput.y };
    AdjustWindowRect(&windowRect, s_WindowStyle, FALSE);

    s_Window = CreateWindow(windowClass.lpszClassName,
                            "Image Clarity Reference",
                            s_WindowStyle,
                            CW_USEDEFAULT,
                            CW_USEDEFAULT,
                            windowRect.right - windowRect.left,
                            windowRect.bottom - windowRect.top,
                            nullptr,
                            nullptr,
                            s_Instance,
                            nullptr);
}

void CreateSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount           = s_SwapChainImageCount;
    swapChainDesc.Width                 = static_cast<UINT>(s_ViewportSizeOutput.x);
    swapChainDesc.Height                = static_cast<UINT>(s_ViewportSizeOutput.y);
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
    ThrowIfFailed(s_DXGIFactory->CreateSwapChainForHwnd(s_CommandQueue.Get(), s_Window, &swapChainDesc, nullptr, nullptr, swapChain.GetAddressOf()));

    // Does not support fullscreen transitions.
    ThrowIfFailed(s_DXGIFactory->MakeWindowAssociation(s_Window, DXGI_MWA_NO_ALT_ENTER));

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

void ResizeSwapChain()
{
    for (auto& swapChainImageView : s_SwapChainImages)
        swapChainImageView.Reset();

    s_SwapChainImages.resize(s_SwapChainImageCount);

    DXGI_SWAP_CHAIN_DESC swapChainInfo = {};
    s_DXGISwapChain->GetDesc(&swapChainInfo);

    ThrowIfFailed(s_DXGISwapChain->ResizeBuffers(s_SwapChainImageCount,
                                                 s_ViewportSizeOutput.x,
                                                 s_ViewportSizeOutput.y,
                                                 swapChainInfo.BufferDesc.Format,
                                                 swapChainInfo.Flags));

    s_CurrentSwapChainImageIndex = s_DXGISwapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(s_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT swapChainImageIndex = 0; swapChainImageIndex < s_SwapChainImageCount; swapChainImageIndex++)
    {
        ThrowIfFailed(s_DXGISwapChain->GetBuffer(swapChainImageIndex, IID_PPV_ARGS(s_SwapChainImages[swapChainImageIndex].GetAddressOf())));
        s_LogicalDevice->CreateRenderTargetView(s_SwapChainImages[swapChainImageIndex].Get(), nullptr, rtvHandle);

        rtvHandle.Offset(1, s_RTVDescriptorSize);
    }
}

void ReleaseGraphicsRuntime()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
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
    ThrowIfFailed(s_DXGIOutputs[0]->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &displayModeCount, nullptr));

    s_DXGIDisplayModes.resize(displayModeCount);

    ThrowIfFailed(s_DXGIOutputs[0]->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &displayModeCount, s_DXGIDisplayModes.data()));

    // Extract list of names for overlay.
    std::transform(s_DXGIDisplayModes.begin(),
                   s_DXGIDisplayModes.end(),
                   std::back_inserter(s_DXGIDisplayModesStr),
                   [&](const DXGI_MODE_DESC& mode)
                   {
                       return std::format("{} x {} @ {}Hz -- {}",
                                          mode.Width,
                                          mode.Height,
                                          mode.RefreshRate.Numerator / mode.RefreshRate.Denominator,
                                          magic_enum::enum_name(mode.Scaling));
                   });
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

        ImGui_ImplWin32_Init(s_Window);

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
        std::stringstream runtimeInfoMsg;

        runtimeInfoMsg << std::endl << std::endl;
        runtimeInfoMsg << std::left << std::setw(13) << "Device: " << s_DXGIAdapterNames[s_DXGIAdapterIndex] << std::endl;
        runtimeInfoMsg << std::left << std::setw(13)
                       << "VRAM: " << s_DXGIAdapterInfos[s_DXGIAdapterIndex].DedicatedVideoMemory / static_cast<float>(1024 * 1024 * 1024)
                       << " GB (Dedicated)" << std::endl;
        runtimeInfoMsg << std::left << std::setw(13) << "DirectSR: ";

        if (s_DSRVariantDescs.empty())
            runtimeInfoMsg << "None" << std::endl;
        else
        {
            for (const auto& variantDesc : s_DSRVariantDescs)
            {
                runtimeInfoMsg << variantDesc.VariantName << std::endl;
                runtimeInfoMsg << std::left << std::setw(13) << " ";
            }
        }

        runtimeInfoMsg << std::endl;

        spdlog::info(runtimeInfoMsg.str());
    }
}

void RenderInterface()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)s_ViewportSizeOutput.x * 0.25f, (float)s_ViewportSizeOutput.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);

    if (ImGui::CollapsingHeader("Presentation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        StringListDropdown("Display", s_DXGIOutputNames, s_DXGIOutputsIndex);

        if (StringListDropdown("Adapter", s_DXGIAdapterNames, s_DXGIAdapterIndex))
            s_UpdateFlags |= UpdateFlags::GraphicsRuntime;

        if (EnumDropdown<WindowMode>("Window Mode", reinterpret_cast<int*>(&s_WindowMode)))
            s_UpdateFlags |= UpdateFlags::Window;

        static int test;
        StringListDropdown("Display Modes", s_DXGIDisplayModesStr, test);

        if (EnumDropdown<SwapEffect>("Swap Effect", reinterpret_cast<int*>(&s_DXGISwapEffect)))
            s_UpdateFlags |= UpdateFlags::SwapChain;

        if (!s_DSRVariantDescs.empty())
            StringListDropdown("DirectSR Algorithm", s_DSRVariantNames, s_DSRVariantIndex);

        if (ImGui::SliderInt("Buffering", reinterpret_cast<int*>(&s_SwapChainImageCount), 2, DXGI_MAX_SWAP_CHAIN_BUFFERS - 1))
            s_UpdateFlags |= UpdateFlags::SwapChain;

        ImGui::SliderInt("V-Sync Interval", &s_SyncInterval, 0, 4);

        static int s_FramesInFlightCount = 1;
        ImGui::SliderInt("Frames in Flight", &s_FramesInFlightCount, 1, 16);
    }

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static float elapsedTime = 0;
        elapsedTime += s_DeltaTime;

        float deltaTimeMs = 1000.0f * s_DeltaTime;
        s_DeltaTimeMovingAverage.AddValue(deltaTimeMs);
        s_DeltaTimeBuffer.AddPoint(elapsedTime, deltaTimeMs);
        s_DeltaTimeMovingAverageBuffer.AddPoint(elapsedTime, s_DeltaTimeMovingAverage.GetAverage());

        static float history = 3.0f;

        if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1, 150)))
        {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, 0x0);
            ImPlot::SetupAxisLimits(ImAxis_X1, elapsedTime - history, elapsedTime, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, s_DeltaTimeMovingAverage.GetAverage() * 2.0, ImGuiCond_Always);
            ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.5f);

            ImPlot::PlotLine("Frame Time (ms)",
                             &s_DeltaTimeBuffer.mData[0].x,
                             &s_DeltaTimeBuffer.mData[0].y,
                             s_DeltaTimeBuffer.mData.size(),
                             ImPlotLineFlags_None,
                             s_DeltaTimeBuffer.mOffset,
                             2 * sizeof(float));

            ImPlot::PlotLine("Frame Time (ms) (smooth)",
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
        if (ImGui::BeginChild("LogSubWindow", ImVec2(0, 200), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::TextUnformatted(s_LoggerMemory->str().c_str());

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0F);
        }
        ImGui::EndChild();
    }

    ImGui::PopItemWidth();

    ImGui::End();
}

void UpdateWindow()
{
    switch (s_WindowMode)
    {
        case WindowMode::Windowed:
        {
            ThrowIfFailed(s_DXGISwapChain->SetFullscreenState(false, nullptr));

            // Restore the window's attributes and size.
            SetWindowLong(s_Window, GWL_STYLE, s_WindowStyle);

            SetWindowPos(s_Window,
                         HWND_NOTOPMOST,
                         s_WindowRect.left,
                         s_WindowRect.top,
                         s_WindowRect.right - s_WindowRect.left,
                         s_WindowRect.bottom - s_WindowRect.top,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE);

            ShowWindow(s_Window, SW_NORMAL);

            break;
        }

        case WindowMode::BorderlessFullscreen:
        {
            ThrowIfFailed(s_DXGISwapChain->SetFullscreenState(false, nullptr));

            // Save the old window rect so we can restore it when exiting fullscreen mode.
            if (s_WindowModePrev == WindowMode::Windowed)
                GetWindowRect(s_Window, &s_WindowRect);

            // Make the window borderless so that the client area can fill the screen.
            SetWindowLong(s_Window, GWL_STYLE, s_WindowStyle & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME));

            RECT fullscreenWindowRect;

            try
            {
                if (s_DXGISwapChain)
                {
                    // Get the settings of the display on which the app's window is currently displayed
                    ComPtr<IDXGIOutput> pOutput;
                    ThrowIfFailed(s_DXGISwapChain->GetContainingOutput(&pOutput));

                    DXGI_OUTPUT_DESC Desc;
                    ThrowIfFailed(pOutput->GetDesc(&Desc));

                    fullscreenWindowRect = Desc.DesktopCoordinates;
                }
                else
                {
                    // Fallback to EnumDisplaySettings implementation
                    throw std::exception();
                }
            }
            catch (std::exception& e)
            {
                UNREFERENCED_PARAMETER(e);

                // Get the settings of the primary display
                DEVMODE devMode = {};
                devMode.dmSize  = sizeof(DEVMODE);
                EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &devMode);

                fullscreenWindowRect = { devMode.dmPosition.x,
                                         devMode.dmPosition.y,
                                         devMode.dmPosition.x + static_cast<LONG>(devMode.dmPelsWidth),
                                         devMode.dmPosition.y + static_cast<LONG>(devMode.dmPelsHeight) };
            }

            SetWindowPos(s_Window,
                         HWND_TOPMOST,
                         fullscreenWindowRect.left,
                         fullscreenWindowRect.top,
                         fullscreenWindowRect.right,
                         fullscreenWindowRect.bottom,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE);

            ShowWindow(s_Window, SW_MAXIMIZE);

            break;
        }

        case WindowMode::ExclusiveFullscreen:
        {
            // Save the old window rect so we can restore it when exiting fullscreen mode.
            if (s_WindowModePrev == WindowMode::Windowed)
                GetWindowRect(s_Window, &s_WindowRect);

            if (SUCCEEDED(s_DXGISwapChain->SetFullscreenState(true, nullptr)))
            {
                // If swapping from borderless mode, we need to manually resize the swapchain here instead of
                // the WM_SIZE windows message proc since the borderless fullscreen + exclusive fullscreen are the same size.
                if (s_WindowModePrev == WindowMode::BorderlessFullscreen)
                    ResizeSwapChain();

                break;
            }

            spdlog::warn("Failed to go fullscreen. Check that the currently selected adapter has ownership of the display.");

            // Return to windowed state.
            s_WindowMode = s_WindowModePrev;
        }
    };

    s_WindowModePrev = s_WindowMode;
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

            UpdateWindow();
        }

        WaitForDevice();

        spdlog::info("Updating Graphics Adapter");

        ReleaseGraphicsRuntime();

        InitializeGraphicsRuntime();
    }

    if ((s_UpdateFlags & UpdateFlags::SwapChain) != 0)
    {
        WaitForDevice();

        spdlog::info("Updating Swap Chain");

        ReleaseSwapChain();

        CreateSwapChain();
    }

    if ((s_UpdateFlags & UpdateFlags::Window) != 0)
    {
        spdlog::info("Updating Window");

        UpdateWindow();
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

    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Indicate that the back buffer will be used as a render target.
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
        D3D12_RECT  clearColorRect = { 0, 0, static_cast<LONG>(0.25L * s_ViewportSizeOutput.x), s_ViewportSizeOutput.y };
        s_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &clearColorRect);
    }

    // CLear the normal rendering viewport region.
    {
        const float clearColor[]   = { 0.2f, 0.2f, 0.75f, 1.0f };
        D3D12_RECT  clearColorRect = { static_cast<LONG>(0.25L * s_ViewportSizeOutput.x), 0, s_ViewportSizeOutput.x, s_ViewportSizeOutput.y };
        s_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &clearColorRect);
    }

    RenderInterface();
    ImGui::Render();

    s_CommandList->SetDescriptorHeaps(1, s_SRVDescriptorHeap.GetAddressOf());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), s_CommandList.Get());

    // Indicate that the back buffer will now be used to present.
    auto renderToPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(s_SwapChainImages[s_CurrentSwapChainImageIndex].Get(),
                                                                       D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                       D3D12_RESOURCE_STATE_PRESENT);
    s_CommandList->ResourceBarrier(1, &renderToPresentBarrier);

    ThrowIfFailed(s_CommandList->Close());

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { s_CommandList.Get() };
    s_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(s_DXGISwapChain->Present(s_SyncInterval, 0));

    WaitForDevice();
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    switch (message)
    {
        case WM_PAINT  : Render(); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;

        case WM_SIZE:
        {
            RECT clientRect = {};

            GetClientRect(hWnd, &clientRect);

            s_ViewportSizeOutput.x = clientRect.right - clientRect.left;
            s_ViewportSizeOutput.y = clientRect.bottom - clientRect.top;

            ResizeSwapChain();

            return 0;
        }
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
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
