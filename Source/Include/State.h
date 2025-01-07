#ifndef STATE_H
#define STATE_H

#include "NRI/NRIDescs.h"
#include <imgui_impl_glfw.h>
namespace ImageQualityReference
{
    // clang-format off
struct NRIInterface
    : public nri::CoreInterface,
      public nri::HelperInterface,
      public nri::SwapChainInterface {};
    // clang-format on

    extern GLFWwindow*                        gWindow;
    extern NRIInterface                       gNRI;
    extern nri::Device*                       gDevice;
    extern std::vector<nri::AdapterDesc>      gAdapterInfos;
    extern std::vector<std::string>           gAdapterNames;
    extern int                                gAdapterIndex;
    extern std::vector<GLFWmonitor*>          gDisplays;
    extern std::vector<std::string>           gDisplayNames;
    extern int                                gDisplayIndex;
    extern std::shared_ptr<std::stringstream> gLoggerMemory;
    extern tbb::task_group                    gAsyncTaskGroup;

}; // namespace ImageQualityReference

#endif
