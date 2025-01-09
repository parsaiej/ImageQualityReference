#include "NRI/Extensions/NRISwapChain.h"
#include "NRI/NRIDescs.h"
#include <State.h>

namespace ImageQualityReference
{
    NRIInterface gNRI = {};

    // The current graphics device.
    nri::Device* gDevice = nullptr;

    // Graphics objects.
    nri::CommandQueue*     gCommandQueue     = nullptr;
    nri::CommandAllocator* gCommandAllocator = nullptr;
    nri::SwapChain*        gSwapChain        = nullptr;

    // List of adapters in the system.
    std::vector<nri::AdapterDesc> gAdapterInfos;

    // List of adapter names for displaty to user.
    std::vector<std::string> gAdapterNames;

    // The currently selected adapter by the user.
    int gAdapterIndex = 0;

    // The list of detected displays / monitors in the system.
    std::vector<GLFWmonitor*> gDisplays;

    // List of human readable names for the displays.
    std::vector<std::string> gDisplayNames;

    // The currently selected display by the user.
    int gDisplayIndex = 0;

    // The list of resolutions for the currently selected display.
    std::vector<Eigen::Vector2i> gResolutions;

    // List of resolutions for display to user.
    std::vector<std::string> gResolutionsStr;

    // The currently selected resolution index.
    int gResolutionIndex = 0;

    // The list of refresh rates for the currently selected display.
    std::vector<int> gRefreshRates;

    // List of refresh rates for display to user.
    std::vector<std::string> gRefreshRatesStr;

    // The currently selected refresh rate.
    int gRefreshRateIndex = 0;

    // Operating system window.
    GLFWwindow* gWindow = nullptr;

    // Need a separate window concept for swap chain creation.
    nri::Window gNRIWindow = {};

    // Block of memory for log text.
    std::shared_ptr<std::stringstream> gLoggerMemory;

    // For dispatching one-off jobs.
    tbb::task_group gAsyncTaskGroup;

} // namespace ImageQualityReference
