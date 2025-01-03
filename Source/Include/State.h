#ifndef STATE_H
#define STATE_H

#include <Util.h>
#include <RenderInput.h>

namespace ICR
{
    class Blitter;
    class ResourceRegistry;

    struct ResourceHandle;

    // Main.cpp for documentation.
    extern D3DMemoryLeakReport                               gLeakReport;
    extern HINSTANCE                                         gInstance;
    extern GLFWwindow*                                       gWindow;
    extern HWND                                              gWindowNative;
    extern WindowMode                                        gWindowMode;
    extern WindowMode                                        gWindowModePrev;
    extern int                                               gCurrentSwapChainImageIndex;
    extern int                                               gRTVDescriptorSize;
    extern int                                               gSRVDescriptorSize;
    extern int                                               gSMPDescriptorSize;
    extern ComPtr<ID3D12Device>                              gLogicalDevice;
    extern ComPtr<IDSRDevice>                                gDSRDevice;
    extern ComPtr<ID3D12CommandQueue>                        gCommandQueue;
    extern ComPtr<ID3D12PipelineState>                       gGraphicsPipelineState;
    extern ComPtr<ID3D12DescriptorHeap>                      gSwapChainDescriptorHeapRTV;
    extern ComPtr<ID3D12DescriptorHeap>                      gImguiDescriptorHeapSRV;
    extern ComPtr<ID3D12CommandAllocator>                    gCommandAllocator;
    extern ComPtr<ID3D12GraphicsCommandList>                 gCommandList;
    extern std::vector<ResourceHandle>                       gSwapChainImageHandles;
    extern uint32_t                                          gSwapChainImageCount;
    extern int                                               gDSRVariantIndex;
    extern std::vector<DSR_SUPERRES_VARIANT_DESC>            gDSRVariantDescs;
    extern std::vector<std::string>                          gDSRVariantNames;
    extern ComPtr<ID3D12Fence>                               gFence;
    extern HANDLE                                            gFenceOperatingSystemEvent;
    extern UINT64                                            gFenceValue;
    extern ComPtr<IDXGIAdapter1>                             gDXGIAdapter;
    extern ComPtr<IDXGIFactory6>                             gDXGIFactory;
    extern ComPtr<IDXGISwapChain3>                           gDXGISwapChain;
    extern std::vector<DXGI_ADAPTER_DESC1>                   gDXGIAdapterInfos;
    extern SwapEffect                                        gDXGISwapEffect;
    extern std::vector<ComPtr<IDXGIOutput>>                  gDXGIOutputs;
    extern std::vector<std::string>                          gDXGIOutputNames;
    extern std::vector<DXGI_MODE_DESC>                       gDXGIDisplayModes;
    extern std::vector<DirectX::XMINT2>                      gDXGIDisplayResolutions;
    extern std::vector<std::string>                          gDXGIDisplayResolutionsStr;
    extern std::vector<DXGI_RATIONAL>                        gDXGIDisplayRefreshRates;
    extern std::vector<std::string>                          gDXGIDisplayRefreshRatesStr;
    extern int                                               gDXGIAdapterIndex;
    extern int                                               gDXGIOutputsIndex;
    extern int                                               gDXGIDisplayResolutionsIndex;
    extern int                                               gDXGIDisplayRefreshRatesIndex;
    extern std::vector<std::string>                          gDXGIAdapterNames;
    extern std::shared_ptr<std::stringstream>                gLoggerMemory;
    extern float                                             gDeltaTime;
    extern MovingAverage                                     gDeltaTimeMovingAverage;
    extern ScrollingBuffer                                   gDeltaTimeBuffer;
    extern ScrollingBuffer                                   gDeltaTimeMovingAverageBuffer;
    extern int                                               gSyncInterval;
    extern uint32_t                                          gUpdateFlags;
    extern DirectX::XMINT2                                   gBackBufferSize;
    extern DirectX::XMINT2                                   gBackBufferSizePrev;
    extern D3D12_VIEWPORT                                    gViewport;
    extern RECT                                              gWindowRect;
    extern UINT                                              gWindowStyle;
    extern StopWatch                                         gStopWatch;
    extern tbb::task_group                                   gTaskGroup;
    extern RenderInputMode                                   gRenderInputMode;
    extern std::unique_ptr<RenderInput>                      gRenderInput;
    extern std::queue<std::function<void()>>                 gPreRenderTaskQueue;
    extern std::unordered_map<std::string, ComPtr<ID3DBlob>> gShaderDXIL;
    extern std::unique_ptr<Blitter>                          gBlitter;
    extern std::unique_ptr<ResourceRegistry>                 gResourceRegistry;

} // namespace ICR

#endif
