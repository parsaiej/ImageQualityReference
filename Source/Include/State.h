#ifndef STATE_H
#define STATE_H

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
    extern std::vector<Eigen::Vector2i>       gResolutions;
    extern std::vector<std::string>           gResolutionsStr;
    extern int                                gResolutionIndex;
    extern std::vector<int>                   gRefreshRates;
    extern std::vector<std::string>           gRefreshRatesStr;
    extern int                                gRefreshRateIndex;
    extern int                                gVideoModeIndex;
    extern std::shared_ptr<std::stringstream> gLoggerMemory;
    extern tbb::task_group                    gAsyncTaskGroup;

}; // namespace ImageQualityReference

#endif
