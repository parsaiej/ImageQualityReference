// clang-format off
// Tell Windows to load the Agility SDK DLLs. 
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 715; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
// clang-format on

#define VIEWPORT_W             1280
#define VIEWPORT_H             720
#define SWAP_CHAIN_IMAGE_COUNT 2

// State
// -----------------------------

HWND s_Window = nullptr;

#include <atlbase.h>

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
ComPtr<IDXGISwapChain3>           s_SwapChain         = nullptr;
ComPtr<ID3D12Resource>            s_SwapChainImages[SWAP_CHAIN_IMAGE_COUNT];

ComPtr<ID3D12Fence> s_Fence                     = nullptr;
HANDLE              s_FenceOperatingSystemEvent = nullptr;
UINT64              s_FenceValue                = 0U;

// Utility Prototypes
// -----------------------------

// Creates an OS window for Microsoft Windows.
void CreateOperatingSystemWindow(HINSTANCE hInstance);

// Create a DXGI swap-chain for the OS window.
void InitializeGraphicsRuntime();

// Load PSOs, simulation state, command buffers, etc.
void LoadResources();

// Command list recording and queue submission for current swap chain image.
void Render();

// Message handle for a Microsoft Windows window.
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_ void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter = false);

// Crashing utility.
void ThrowIfFailed(HRESULT hr);

// Use fence primitives to pause the thread until the previous swap-chain image has finished being drawn and presented.
void WaitForPreviousFrame();

// Entry-point
// -----------------------------

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    CreateOperatingSystemWindow(hInstance);

    InitializeGraphicsRuntime();

    LoadResources();

    ShowWindow(s_Window, nCmdShow);

    // Main sample loop.
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

    // Tear down ImGui.
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // Return this part of the WM_QUIT message to Windows.
    return static_cast<char>(msg.wParam);
}

// Utiliy Implementations
// -----------------------------

void CreateOperatingSystemWindow(HINSTANCE hInstance)
{
    WNDCLASSEX windowClass = { 0 };
    {
        windowClass.cbSize        = sizeof(WNDCLASSEX);
        windowClass.style         = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc   = WindowProc;
        windowClass.hInstance     = hInstance;
        windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
        windowClass.lpszClassName = "ImageQualityReference";
    }
    RegisterClassEx(&windowClass);

    // Hardcode for now.
    RECT windowRect = { 0, 0, VIEWPORT_W, VIEWPORT_H };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // Create the window and store a handle to it.
    s_Window = CreateWindow(windowClass.lpszClassName,
                            "ImageQualityReference",
                            WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT,
                            CW_USEDEFAULT,
                            windowRect.right - windowRect.left,
                            windowRect.bottom - windowRect.top,
                            nullptr, // We have no parent window.
                            nullptr, // We aren't using menus.
                            hInstance,
                            nullptr);
}
#include <atlbase.h>
void InitializeGraphicsRuntime()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&s_LogicalDevice)));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type                     = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(s_LogicalDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&s_CommandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount           = SWAP_CHAIN_IMAGE_COUNT;
    swapChainDesc.Width                 = VIEWPORT_W;
    swapChainDesc.Height                = VIEWPORT_H;
    swapChainDesc.Format                = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count      = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(s_CommandQueue.Get(), // Swap chain needs the queue so that it can force a flush on it.
                                                  s_Window,
                                                  &swapChainDesc,
                                                  nullptr,
                                                  nullptr,
                                                  &swapChain));

    // Does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(s_Window, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&s_SwapChain));
    s_CurrentSwapChainImageIndex = s_SwapChain->GetCurrentBackBufferIndex();

    // Descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors             = SWAP_CHAIN_IMAGE_COUNT;
        rtvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(s_LogicalDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&s_RTVDescriptorHeap)));

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors             = 32;
        srvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(s_LogicalDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&s_SRVDescriptorHeap)));

        s_RTVDescriptorSize = s_LogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        s_SRVDescriptorSize = s_LogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(s_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < SWAP_CHAIN_IMAGE_COUNT; n++)
        {
            ThrowIfFailed(s_SwapChain->GetBuffer(n, IID_PPV_ARGS(&s_SwapChainImages[n])));
            s_LogicalDevice->CreateRenderTargetView(s_SwapChainImages[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, s_RTVDescriptorSize);
        }
    }

    ThrowIfFailed(s_LogicalDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_CommandAllocator)));

    // Initialize DirectSR device.
    {
        ComPtr<ID3D12DSRDeviceFactory> pDSRDeviceFactory;
        ThrowIfFailed(D3D12GetInterface(CLSID_D3D12DSRDeviceFactory, IID_PPV_ARGS(&pDSRDeviceFactory)));
        ThrowIfFailed(pDSRDeviceFactory->CreateDSRDevice(s_LogicalDevice.Get(), 1, IID_PPV_ARGS(&s_DSRDevice)));

        UINT numDsrVariants = s_DSRDevice->GetNumSuperResVariants();

        for (UINT index = 0; index < numDsrVariants; index++)
        {
            DSR_SUPERRES_VARIANT_DESC variantDesc;
            ThrowIfFailed(s_DSRDevice->GetSuperResVariantDesc(index, &variantDesc));
        }
    }

    // Initialize ImGui.
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();

        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(s_Window);

        auto srvHeapCPUHandle = s_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        auto srvHeapGPUHandle = s_SRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

        ImGui_ImplDX12_Init(s_LogicalDevice.Get(),
                            SWAP_CHAIN_IMAGE_COUNT,
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            s_SRVDescriptorHeap.Get(),
                            srvHeapCPUHandle,
                            srvHeapGPUHandle);
    }
}

void LoadResources()
{
    // Create the command list.
    ThrowIfFailed(
        s_LogicalDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&s_CommandList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(s_CommandList->Close());

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(s_LogicalDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s_Fence)));
        s_FenceValue = 1;

        // Create an event handle to use for frame synchronization.
        s_FenceOperatingSystemEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (s_FenceOperatingSystemEvent == nullptr)
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

        // Wait for the command list to execute; we are reusing the same command
        // list in our main loop but for now, we just want to wait for setup to
        // complete before continuing.
        WaitForPreviousFrame();
    }
}

void Render()
{
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    ThrowIfFailed(s_CommandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
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

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    s_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    static bool s_ShowWindow = true;
    ImGui::ShowDemoWindow(&s_ShowWindow);
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
    ThrowIfFailed(s_SwapChain->Present(1, 0));

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

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_ void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter)
{
    *ppAdapter = nullptr;

    ComPtr<IDXGIAdapter1> adapter;

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (UINT adapterIndex = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                 adapterIndex,
                 requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                 IID_PPV_ARGS(&adapter)));
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_2, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    if (adapter.Get() == nullptr)
    {
        for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_2, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    *ppAdapter = adapter.Detach();
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
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = s_FenceValue;
    ThrowIfFailed(s_CommandQueue->Signal(s_Fence.Get(), fence));
    s_FenceValue++;

    // Wait until the previous frame is finished.
    if (s_Fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(s_Fence->SetEventOnCompletion(fence, s_FenceOperatingSystemEvent));
        WaitForSingleObject(s_FenceOperatingSystemEvent, INFINITE);
    }

    s_CurrentSwapChainImageIndex = s_SwapChain->GetCurrentBackBufferIndex();
}
