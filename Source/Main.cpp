#include "NRI/NRIDescs.h"
#include <State.h>
#include <Common.h>
#include <ResourceManager.h>

using namespace ImageQualityReference;

// Enumerate a list of graphics adapters that support our usage of D3D12.
void EnumerateAdapters();

// Enumerate a list of displays and user-friendly info about them.
void EnumerateDisplays();

// Enumerate a list of the supported video modes for the current display.
void EnumerateVideoModes();

//
void RecreateSwapChain();

// Creates device instance based on user selection etc.
void InitializeGraphicsRuntime();

// Frees all graphics resources.
void ReleaseGraphicsRuntime();

// Main rendering routine.
void Render();

#ifdef _WIN32
_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
#else
int main(int argc, char** argv)
#endif
{
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
    gAsyncTaskGroup.run([] { EnumerateAdapters(); });

    // Create operating system window.
    // -----------------------------------------
    {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        // Initialize window to 800x600 for now, later on in initialization it will be resized to something more reasonable.
        gWindow = glfwCreateWindow(800, 600, "Image Clarity Reference", nullptr, nullptr);

        if (!gWindow)
            exit(1);

        // Retrieve native handle to the operating system window.
#if _WIN32
        gNRIWindow.windows.hwnd = glfwGetWin32Window(gWindow);
#elif __linux__
        gNRIWindow.x11.dpy    = glfwGetX11Display();
        gNRIWindow.x11.window = glfwGetX11Window(gWindow);
#elif __APPLE__
        gNRIWindow.metal.caMetalLayer = GetMetalLayer(gWindow);
#endif
    }

    gAsyncTaskGroup.wait();

    // We use glslang in case of shadertoy shader compilation to DXIL.
    glslang::InitializeProcess();

    // Select graphics device + initialize API, swapchain, etc.
    InitializeGraphicsRuntime();

    // Main application loop.
    // -----------------------------------------

    while (!glfwWindowShouldClose(gWindow))
    {
        glfwPollEvents();

        Render();
    }

    ReleaseGraphicsRuntime();

    glslang::FinalizeProcess();

    // Release operating system window.
    glfwDestroyWindow(gWindow);
    glfwTerminate();

    return 0;
}

void EnumerateDisplays()
{
    int           monitorCount;
    GLFWmonitor** ppMonitors = glfwGetMonitors(&monitorCount);

    gDisplays.clear();
    gDisplayNames.clear();
    gDisplayIndex = 0;

    for (int monitorIndex = 0; monitorIndex < monitorCount; ++monitorIndex)
    {
        gDisplays.push_back(ppMonitors[monitorIndex]);
        gDisplayNames.push_back(glfwGetMonitorName(ppMonitors[monitorIndex]));
    }
}

void EnumerateVideoModes()
{
    std::vector<GLFWvidmode> videoModes;
    {
        int                modeCount;
        const GLFWvidmode* ppModes = glfwGetVideoModes(gDisplays[gDisplayIndex], &modeCount);

        for (int modeIndex = 0; modeIndex < modeCount; modeIndex++)
            videoModes.push_back(ppModes[modeIndex]);
    }

    gResolutions.clear();
    gResolutionsStr.clear();
    gResolutionIndex = 0;

    // Extract list of unique resolutions. (There may be multiples of the same resolution due
    // to various refresh rates, dxgi formats, etc.).
    // ----------------------------------------------------------
    {
        std::set<Eigen::Vector2i, Vector2iCmp> uniqueResolutions;

        std::transform(videoModes.begin(),
                       videoModes.end(),
                       std::inserter(uniqueResolutions, uniqueResolutions.begin()),
                       [&](const GLFWvidmode& mode) { return Eigen::Vector2i(mode.width, mode.height); });

        gResolutions.resize(uniqueResolutions.size());
        std::copy(uniqueResolutions.begin(), uniqueResolutions.end(), gResolutions.begin());

        std::transform(gResolutions.begin(),
                       gResolutions.end(),
                       std::back_inserter(gResolutionsStr),
                       [&](const Eigen::Vector2i& r) { return fmt::format("{} x {}", r.x(), r.y()); });
    }

    gRefreshRates.clear();
    gRefreshRatesStr.clear();
    gRefreshRateIndex = 0;

    // Extract list of refresh rates.
    // ----------------------------------------------------------
    {
        std::set<int> uniqueRefreshRates;

        std::transform(videoModes.begin(),
                       videoModes.end(),
                       std::inserter(uniqueRefreshRates, uniqueRefreshRates.begin()),
                       [&](const GLFWvidmode& mode) { return mode.refreshRate; });

        gRefreshRates.resize(uniqueRefreshRates.size());
        std::copy(uniqueRefreshRates.begin(), uniqueRefreshRates.end(), gRefreshRates.begin());

        std::transform(gRefreshRates.begin(),
                       gRefreshRates.end(),
                       std::back_inserter(gRefreshRatesStr),
                       [&](const int& r) { return fmt::format("{} Hz", r); });
    }
}

void EnumerateAdapters()
{
    uint32_t adapterCount;
    NRI_ABORT_ON_FAILURE(nri::nriEnumerateAdapters(nullptr, adapterCount));

    gAdapterInfos.resize(adapterCount);
    NRI_ABORT_ON_FAILURE(nri::nriEnumerateAdapters(gAdapterInfos.data(), adapterCount));

    // Extract a list of supported adapter names for presentation to the user.
    std::transform(gAdapterInfos.begin(),
                   gAdapterInfos.end(),
                   std::back_inserter(gAdapterNames),
                   [](const nri::AdapterDesc& adapter) { return adapter.name; });

    if (!gAdapterInfos.empty())
        return;

    spdlog::critical("There were no graphics adapters found on the system");

    exit(1);
}

void RecreateSwapChain()
{
    if (gSwapChain != nullptr)
    {
        gNRI.DestroySwapChain(*gSwapChain);
    }

    nri::SwapChainDesc swapChainInfo = {};
    {
        swapChainInfo.window               = gNRIWindow;
        swapChainInfo.commandQueue         = gCommandQueue;
        swapChainInfo.format               = nri::SwapChainFormat::BT709_G22_8BIT;
        swapChainInfo.verticalSyncInterval = 1;
        swapChainInfo.width                = 800;
        swapChainInfo.height               = 600;
        swapChainInfo.textureNum           = 2;
    }
    NRI_ABORT_ON_FAILURE(gNRI.CreateSwapChain(*gDevice, swapChainInfo, gSwapChain));

    uint32_t swapChainImageCount;
    auto     ppSwapChainTextures = gNRI.GetSwapChainTextures(*gSwapChain, swapChainImageCount);

    gSwapChainImageInfo = gNRI.GetTextureDesc(*ppSwapChainTextures[0]);

    for (uint32_t swapChainImageIndex = 0; swapChainImageIndex < swapChainImageCount; swapChainImageIndex++)
    {
        gSwapChainImageHandles.push_back(gResourceManager->Create(ppSwapChainTextures[swapChainImageIndex], ResourceView::RenderTarget));
    }
}

static nri::CommandBuffer* pCommandBuffer;

void InitializeGraphicsRuntime()
{
    nri::DeviceCreationDesc deviceCreationDesc = {};
    {
#ifdef _DEBUG
        bool enableValidation = true;
#else
        bool enableValidation = false;
#endif

#ifdef _WIN32
        deviceCreationDesc.graphicsAPI = nri::GraphicsAPI::D3D12;
#else
        deviceCreationDesc.graphicsAPI = nri::GraphicsAPI::VK;
#endif

        deviceCreationDesc.enableGraphicsAPIValidation = enableValidation;
        deviceCreationDesc.enableNRIValidation         = enableValidation;
        deviceCreationDesc.adapterDesc                 = &gAdapterInfos[gAdapterIndex];
    }
    NRI_ABORT_ON_FAILURE(nri::nriCreateDevice(deviceCreationDesc, gDevice));

    EnumerateDisplays();

    EnumerateVideoModes();

    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*gDevice, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&gNRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*gDevice, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&gNRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*gDevice, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&gNRI));

    NRI_ABORT_ON_FAILURE(gNRI.GetCommandQueue(*gDevice, nri::CommandQueueType::GRAPHICS, gCommandQueue));

    NRI_ABORT_ON_FAILURE(gNRI.CreateCommandAllocator(*gCommandQueue, gCommandAllocator));

    // Create temporary single-frame command buffer.
    NRI_ABORT_ON_FAILURE(gNRI.CreateCommandBuffer(*gCommandAllocator, pCommandBuffer));

    gResourceManager = std::make_unique<ResourceManager>();

    RecreateSwapChain();
}

void ReleaseGraphicsRuntime()
{
    gNRI.WaitForIdle(*gCommandQueue);
    gNRI.DestroySwapChain(*gSwapChain);
    nri::nriDestroyDevice(*gDevice);
}

void Render()
{
    // For now just halt the thread until GPU is done before moving to next frame.
    gNRI.WaitForIdle(*gCommandQueue);

    gNRI.ResetCommandAllocator(*gCommandAllocator);

    gNRI.BeginCommandBuffer(*pCommandBuffer, nullptr);

    const uint32_t currentSwapChainImageIndex = gNRI.AcquireNextSwapChainTexture(*gSwapChain);

    auto* pCurrentSwapChainImageResource = gResourceManager->Get(gSwapChainImageHandles[currentSwapChainImageIndex]);

    nri::TextureBarrierDesc textureBarrierDescs = {};
    textureBarrierDescs.texture                 = pCurrentSwapChainImageResource->pTexture;
    textureBarrierDescs.after                   = { nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT };
    textureBarrierDescs.layerNum                = 1;
    textureBarrierDescs.mipNum                  = 1;

    nri::BarrierGroupDesc barrierGroupDesc = {};
    barrierGroupDesc.textureNum            = 1;
    barrierGroupDesc.textures              = &textureBarrierDescs;

    gNRI.CmdBarrier(*pCommandBuffer, barrierGroupDesc);

    nri::AttachmentsDesc attachmentInfo = {};
    {
        attachmentInfo.colorNum = 1u;
        attachmentInfo.colors   = &pCurrentSwapChainImageResource->views[ResourceView::RenderTarget];
    }
    gNRI.CmdBeginRendering(*pCommandBuffer, attachmentInfo);

    nri::ClearDesc clearDescs[2] = {};
    clearDescs[0].planes         = nri::PlaneBits::COLOR;
    clearDescs[0].value.color.f  = { 0.0f, 0.63f, 1.0f };

    gNRI.CmdClearAttachments(*pCommandBuffer, clearDescs, 1, nullptr, 0);

    gNRI.CmdEndRendering(*pCommandBuffer);

    textureBarrierDescs.before = textureBarrierDescs.after;
    textureBarrierDescs.after  = { nri::AccessBits::UNKNOWN, nri::Layout::PRESENT };

    gNRI.CmdBarrier(*pCommandBuffer, barrierGroupDesc);

    gNRI.EndCommandBuffer(*pCommandBuffer);

    nri::QueueSubmitDesc submitInfo = {};
    {
        submitInfo.commandBufferNum = 1u;
        submitInfo.commandBuffers   = &pCommandBuffer;
    }
    gNRI.QueueSubmit(*gCommandQueue, submitInfo);

    (gNRI.QueuePresent(*gSwapChain));

    // NRI_ABORT_ON_FAILURE(gNRI.WaitForPresent(*gSwapChain));
}
