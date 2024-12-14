// clang-format off
// Tell Windows to load the Agility SDK DLLs. 
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 715; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
// clang-format on

// Import the imgui style.
#include "OverlayStyle.h"

#define VIEWPORT_W     1920
#define VIEWPORT_H     1080
#define NUM_BACKBUFFER 2

// Options
// -----------------------------

enum WindowMode
{
    Windowed,
    BorderlessFullscreen,
    ExclusiveFullscreen
};

// State
// -----------------------------

HINSTANCE s_Instance = nullptr;
HWND      s_Window   = nullptr;

WindowMode s_WindowMode = WindowMode::Windowed;

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
ComPtr<ID3D12Resource>            s_SwapChainImages[NUM_BACKBUFFER];

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
std::vector<DXGI_OUTPUT_DESC>   s_DXGIOutputInfos;
DXGI_SWAP_EFFECT                s_DXGISwapEffect;

// Adapted from all found DXGI_ADAPTER_DESC1 for easy display in the UI.
std::vector<std::string> s_DXGIAdapterNames;

// Log buffer memory.
std::shared_ptr<std::stringstream> s_LoggerMemory;

// Ring buffer for frame time (ms)
int                k_FrameTimeHistorySize = 100;
int                s_FrameTimeIndex;
int                s_FrameTimeCount;
std::vector<float> s_FrameTimesMs;

// Various flags to keep the state of the process in sync with user selection.
bool s_RecreateDevice    = false;
bool s_RecreateSwapChain = false;

// Utility Prototypes
// -----------------------------

std::string FromWideStr(std::wstring str);

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
void WaitForPreviousFrame();

// Entry-point
// -----------------------------

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Declare this process to be high DPI aware, and prevent automatic scaling
    HINSTANCE hUser32 = LoadLibrary("user32.dll");

    if (hUser32)
    {
        typedef BOOL(WINAPI * LPSetProcessDPIAware)(void);

        LPSetProcessDPIAware pSetProcessDPIAware = (LPSetProcessDPIAware)GetProcAddress(hUser32, "SetProcessDPIAware");

        if (pSetProcessDPIAware)
            pSetProcessDPIAware();

        FreeLibrary(hUser32);
    }

    s_Instance = hInstance;

    // Flush on critical events since we usually fatally crash the app right after.
    spdlog::flush_on(spdlog::level::critical);

    // Frame tine ring buffer holds 2k frames of history.
    s_FrameTimesMs.resize(k_FrameTimeHistorySize);

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

    // Return this part of the WM_QUIT message to Windows.
    return static_cast<char>(msg.wParam);
}

// Utiliy Implementations
// -----------------------------

struct ScrollingBuffer
{
    ScrollingBuffer(int maxSize = 8000)
    {
        this->maxSize = maxSize;
        offset        = 0;
        data.reserve(maxSize);
    }

    void AddPoint(float x, float y)
    {
        if (data.size() < maxSize)
            data.push_back(ImVec2(x, y));
        else
        {
            data[offset] = ImVec2(x, y);

            offset = (offset + 1) % maxSize;
        }
    }

    void Erase()
    {
        if (data.size() > 0)
        {
            data.shrink(0);
            offset = 0;
        }
    }

    int              maxSize;
    int              offset;
    ImVector<ImVec2> data;
};

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
               "No D3D12 Adapters found that support D3D_FEATURE_LEVEL_12_2. The app will now exit.",
               "Image Quality Reference",
               MB_ICONERROR | MB_OK);

    // Nothing to do if no devices are found.
    exit(1);
}

void CreateOperatingSystemWindow()
{
    if (s_Window != nullptr)
        DestroyWindow(s_Window);

    WNDCLASSEX windowClass = { 0 };
    {
        windowClass.cbSize        = sizeof(WNDCLASSEX);
        windowClass.style         = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc   = WindowProc;
        windowClass.hInstance     = s_Instance;
        windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
        windowClass.lpszClassName = "ImageQualityReference";
    }
    RegisterClassEx(&windowClass);

    // Hardcode for now.
    RECT windowRect = { 0, 0, VIEWPORT_W, VIEWPORT_H };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // Create the window and store a handle to it.
    s_Window = CreateWindow(windowClass.lpszClassName,
                            "Image Quality Reference",
                            WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT,
                            CW_USEDEFAULT,
                            windowRect.right - windowRect.left,
                            windowRect.bottom - windowRect.top,
                            nullptr, // We have no parent window.
                            nullptr, // We aren't using menus.
                            s_Instance,
                            nullptr);
}

void ReleaseGraphicsRuntime()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    s_DSRDevice->Release();
    s_CommandAllocator->Release();
    s_CommandList->Release();
    s_CommandQueue->Release();
    s_SRVDescriptorHeap->Release();
    s_RTVDescriptorHeap->Release();
    s_Fence->Release();

    for (auto& swapChainImageView : s_SwapChainImages)
        swapChainImageView->Release();

    // Important to reset, not release here.
    s_DXGISwapChain.Reset();

    s_DXGIAdapter->Release();
    s_DXGIFactory->Release();
    s_LogicalDevice->Release();
}

void InitializeGraphicsRuntime()
{
    UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
    // Enable additional debug layers.
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(s_DXGIFactory.GetAddressOf())));

    ComPtr<IDXGIAdapter1> pAdapter;
    for (UINT adapterIndex = 0; SUCCEEDED(s_DXGIFactory->EnumAdapters1(adapterIndex, &pAdapter)); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);

        if (s_DXGIAdapterInfos[s_DXGIAdapterIndex].DeviceId == desc.DeviceId)
            break;
    }

    // Assign the new adapter.
    s_DXGIAdapter = pAdapter.Detach();

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

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount           = NUM_BACKBUFFER;
    swapChainDesc.Width                 = VIEWPORT_W;
    swapChainDesc.Height                = VIEWPORT_H;
    swapChainDesc.Format                = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count      = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(s_DXGIFactory->CreateSwapChainForHwnd(s_CommandQueue.Get(), s_Window, &swapChainDesc, nullptr, nullptr, swapChain.GetAddressOf()));

    // Does not support fullscreen transitions.
    ThrowIfFailed(s_DXGIFactory->MakeWindowAssociation(s_Window, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&s_DXGISwapChain));
    s_CurrentSwapChainImageIndex = s_DXGISwapChain->GetCurrentBackBufferIndex();

    // Descriptor heaps.
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors             = NUM_BACKBUFFER;
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

    // Swapchain Image Views.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(s_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        for (UINT swapChainImageIndex = 0; swapChainImageIndex < NUM_BACKBUFFER; swapChainImageIndex++)
        {
            ThrowIfFailed(s_DXGISwapChain->GetBuffer(swapChainImageIndex, IID_PPV_ARGS(s_SwapChainImages[swapChainImageIndex].GetAddressOf())));
            s_LogicalDevice->CreateRenderTargetView(s_SwapChainImages[swapChainImageIndex].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, s_RTVDescriptorSize);
        }
    }

    ThrowIfFailed(s_LogicalDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(s_CommandAllocator.GetAddressOf())));

    // Initialize DirectSR device.
    {
        ComPtr<ID3D12DSRDeviceFactory> pDSRDeviceFactory;
        ThrowIfFailed(D3D12GetInterface(CLSID_D3D12DSRDeviceFactory, IID_PPV_ARGS(&pDSRDeviceFactory)));
        ThrowIfFailed(pDSRDeviceFactory->CreateDSRDevice(s_LogicalDevice.Get(), 1, IID_PPV_ARGS(s_DSRDevice.GetAddressOf())));

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

    // Create the command list.
    ThrowIfFailed(s_LogicalDevice->CreateCommandList(0,
                                                     D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     s_CommandAllocator.Get(),
                                                     nullptr,
                                                     IID_PPV_ARGS(s_CommandList.GetAddressOf())));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(s_CommandList->Close());

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
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
        WaitForPreviousFrame();
    }

    // Initialize ImGui.
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
                            NUM_BACKBUFFER,
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            s_SRVDescriptorHeap.Get(),
                            srvHeapCPUHandle,
                            srvHeapGPUHandle);

        SetStyle();
    }

    // Log a message containing information about the adapter.
    {
        spdlog::info("--------- Initialized D3D12 Runtime ----------");

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
    ImGui::SetNextWindowSize(ImVec2(VIEWPORT_W * 0.25, VIEWPORT_H));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::SeparatorText("Presentation");
    {
        // Windowed / Borderless / Fullscreen
        EnumDropdown<WindowMode>("Window Mode", reinterpret_cast<int*>(&s_WindowMode));

        s_RecreateDevice |= StringListDropdown("Adapters", s_DXGIAdapterNames, s_DXGIAdapterIndex);

        s_RecreateSwapChain |= EnumDropdown<DXGI_SWAP_EFFECT>("DXGI Swap Effect", reinterpret_cast<int*>(&s_DXGISwapEffect));

        if (!s_DSRVariantDescs.empty())
            StringListDropdown("DirectSR Algorithm", s_DSRVariantNames, s_DSRVariantIndex);

        // Backbuffer Format

        // Buffering
        static int s_BufferingCount = 1;
        s_RecreateSwapChain |= ImGui::SliderInt("Buffering", &s_BufferingCount, 1, 3);

        // V-Sync

        // Frames in Flight
        static int s_FramesInFlightCount = 1;
        ImGui::SliderInt("Frames in Flight", &s_FramesInFlightCount, 1, 16);
    }

    ImGui::SeparatorText("Content");
    {
        ImGui::Text("TODO");
    }

    ImPlot::ShowDemoWindow();

    ImGui::SeparatorText("Performance");
    {
        static ScrollingBuffer frameTimeBuffer;

        static float elapsedTime = 0;
        elapsedTime += ImGui::GetIO().DeltaTime;

        frameTimeBuffer.AddPoint(elapsedTime, ImGui::GetIO().DeltaTime);

        static float history = 10.0f;

        static ImPlotAxisFlags flags = ImPlotAxisFlags_NoTickLabels;

        if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1, 150)))
        {
            ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
            ImPlot::SetupAxisLimits(ImAxis_X1, elapsedTime - history, elapsedTime, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0f / 60.0f);
            ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.5f);

            ImPlot::PlotLine("Frame Time (ms)",
                             &frameTimeBuffer.data[0].x,
                             &frameTimeBuffer.data[0].y,
                             frameTimeBuffer.data.size(),
                             0,
                             frameTimeBuffer.offset,
                             2 * sizeof(float));

            ImPlot::EndPlot();
        }
    }

    ImGui::SeparatorText("Log");

    if (ImGui::BeginChild("LogSubWindow", ImVec2(0, 0), 1, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::TextUnformatted(s_LoggerMemory->str().c_str());

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0F);
    }
    ImGui::EndChild();

    ImGui::End();
}

void Render()
{
    if (s_RecreateDevice)
    {
        WaitForPreviousFrame();

        ReleaseGraphicsRuntime();

        InitializeGraphicsRuntime();

        s_RecreateDevice = !s_RecreateDevice;
    }

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
        D3D12_RECT  clearColorRect = { 0, 0, static_cast<LONG>(0.25L * VIEWPORT_W), VIEWPORT_H };
        s_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &clearColorRect);
    }

    // CLear the normal rendering viewport region.
    {
        const float clearColor[]   = { 0.2f, 0.2f, 0.75f, 1.0f };
        D3D12_RECT  clearColorRect = { static_cast<LONG>(0.25L * VIEWPORT_W), 0, VIEWPORT_W, VIEWPORT_H };
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
    ThrowIfFailed(s_DXGISwapChain->Present(1, 0));

    WaitForPreviousFrame();
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

void WaitForPreviousFrame()
{
    const UINT64 fence = s_FenceValue++;
    ThrowIfFailed(s_CommandQueue->Signal(s_Fence.Get(), fence));

    // Wait until the previous frame is finished.
    if (s_Fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(s_Fence->SetEventOnCompletion(fence, s_FenceOperatingSystemEvent));
        WaitForSingleObject(s_FenceOperatingSystemEvent, INFINITE);
    }

    s_CurrentSwapChainImageIndex = s_DXGISwapChain->GetCurrentBackBufferIndex();
}
