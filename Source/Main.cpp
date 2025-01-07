#include "NRI/Extensions/NRIDeviceCreation.h"
#include "NRI/NRIDescs.h"
#include <GLFW/glfw3.h>
#include <State.h>
#include <Common.h>
#include <spdlog/spdlog.h>

using namespace ImageQualityReference;

// Enumerate a list of graphics adapters that support our usage of D3D12.
void EnumerateAdapters();

// Enumerate a list of displays and user-friendly info about them.
void EnumerateDisplays();

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
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

    gDisplays.resize(monitorCount);
    memcpy(gDisplays.data(), monitors, monitorCount);

    std::cout << "Monitors available: " << monitorCount << "\n";
    for (int i = 0; i < monitorCount; ++i)
    {
        GLFWmonitor* monitor     = monitors[i];
        const char*  monitorName = glfwGetMonitorName(monitor);

        // Get all supported video modes
        int                modeCount;
        const GLFWvidmode* modes = glfwGetVideoModes(monitor, &modeCount);

        std::cout << "Monitor " << i + 1 << ": " << monitorName << "\n";
        std::cout << "Supported video modes:\n";

        for (int j = 0; j < modeCount; ++j)
        {
            const GLFWvidmode& mode = modes[j];
            std::cout << "Resolution: " << mode.width << "x" << mode.height << ", Refresh Rate: " << mode.refreshRate << " Hz\n";
        }
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

    nri::nriDestroyDevice(*gDevice);
}
