#include "NRI/NRIDescs.h"
#include <State.h>
#include <imgui_impl_glfw.h>

namespace ImageQualityReference
{
    NRIInterface gNRI = {};

    // The current graphics device.
    nri::Device* gDevice = nullptr;

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

    // Operating system window.
    GLFWwindow* gWindow = nullptr;

    // Block of memory for log text.
    std::shared_ptr<std::stringstream> gLoggerMemory;

    // For dispatching one-off jobs.
    tbb::task_group gAsyncTaskGroup;

} // namespace ImageQualityReference