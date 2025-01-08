#include <State.h>
#include <Common.h>

using namespace ImageQualityReference;

// Enumerate a list of graphics adapters that support our usage of D3D12.
void EnumerateAdapters();

// Enumerate a list of displays and user-friendly info about them.
void EnumerateDisplays();

// Enumerate a list of the supported video modes for the current display.
void EnumerateVideoModes();

// Creates device instance based on user selection etc.
void InitializeGraphicsRuntime();

int main(int argc, char** argv)
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
    }

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

void InitializeGraphicsRuntime()
{
    nri::DeviceCreationDesc deviceCreationDesc = {};
    {
#ifdef _DEBUG
        bool enableValidation = false;
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

    nri::nriDestroyDevice(*gDevice);
}
