#include "imgui_impl_nri.h"
#include <Interface.h>
#include <State.h>

// -------------------------------------

void ImageQualityReference::CreateInterface()
{
    if (ImGui::GetCurrentContext() != nullptr)
        DestroyInterface();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOther(gWindow, true);

    ImGui_ImplNRI_InitInfo interfaceInitInfo = {};
    {
        interfaceInitInfo.Device = gDevice;
    }
    ImGui_ImplNRI_Init(&interfaceInitInfo);
}

void ImageQualityReference::DestroyInterface()
{
    // TODO
    ImGui::DestroyContext();
}
