#include <State.h>
#include <ResourceRegistry.h>
#include <Blitter.h>

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
