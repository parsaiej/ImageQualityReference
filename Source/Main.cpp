#include <spdlog/spdlog.h>
#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using namespace Microsoft::WRL;

#define VIEWPORT_W             1280
#define VIEWPORT_H             720
#define SWAP_CHAIN_IMAGE_COUNT 2

// State
// -----------------------------

HWND s_Window = nullptr;

int s_CurrentSwapChainImageIndex;
int s_RTVDescriptorSize;

ComPtr<ID3D12Device>         s_LogicalDevice     = nullptr;
ComPtr<ID3D12CommandQueue>   s_CommandQueue      = nullptr;
ComPtr<ID3D12DescriptorHeap> s_RTVDescriptorHeap = nullptr;
ComPtr<IDXGISwapChain3>      s_SwapChain         = nullptr;
ComPtr<ID3D12Resource>       s_SwapChainImages[SWAP_CHAIN_IMAGE_COUNT];

// Utility Prototypes
// -----------------------------

// Creates an OS window for Microsoft Windows.
void CreateOperatingSystemWindow(HINSTANCE hInstance);

// Create a DXGI swap-chain for the OS window.
void CreateSwapChain();

// Message handle for a Microsoft Windows window.
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_ void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter = false);

// Crashing utility.
void ThrowIfFailed(HRESULT hr);

// Entry-point
// -----------------------------

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    CreateOperatingSystemWindow(hInstance);

    CreateSwapChain();

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

void CreateSwapChain()
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

    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&s_LogicalDevice)));

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

    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors             = SWAP_CHAIN_IMAGE_COUNT;
    rtvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(s_LogicalDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&s_RTVDescriptorHeap)));

    s_RTVDescriptorSize = s_LogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

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
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
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
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
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
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
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
