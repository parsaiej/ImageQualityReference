#include <Interface.h>
#include <State.h>
#include <OverlayStyle.h>
#include <Util.h>
#include <RenderInputShaderToy.h>

using namespace ICR;

void Interface::Create()
{
    if (ImGui::GetCurrentContext() != nullptr)
        Interface::Release();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // NOTE: Enabling this causes a 200+ ms hitch on the first NewFrame() call.
    // Disable it for now to remove that hitch.
    // io.ConfigFlags |= ImGuiConfigFlaggNavEnableGamepad;

    // Auto-install message callbacks.
    ImGui_ImplGlfw_InitForOther(gWindow, true);

    auto srvHeapCPUHandle = gImguiDescriptorHeapSRV->GetCPUDescriptorHandleForHeapStart();
    auto srvHeapGPUHandle = gImguiDescriptorHeapSRV->GetGPUDescriptorHandleForHeapStart();

    ImGui_ImplDX12_Init(gLogicalDevice.Get(),
                        DXGI_MAX_SWAP_CHAIN_BUFFERS,
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        gImguiDescriptorHeapSRV.Get(),
                        srvHeapCPUHandle,
                        srvHeapGPUHandle);

    SetStyle();
}

void Interface::Release()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

void Interface::Draw()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)gBackBufferSize.x * 0.25f, (float)gBackBufferSize.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);

    if (ImGui::CollapsingHeader("Presentation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(gWindowMode == WindowMode::Windowed);
        {
            if (StringListDropdown("Display", gDXGIOutputNames, gDXGIOutputsIndex))
                gUpdateFlags |= UpdateFlags::Display;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(gWindowMode != WindowMode::Windowed);

        if (StringListDropdown("Resolution", gDXGIDisplayResolutionsStr, gDXGIDisplayResolutionsIndex))
            gUpdateFlags |= UpdateFlags::SwapChainResize;

        ImGui::EndDisabled();

        ImGui::BeginDisabled(gWindowMode != WindowMode::ExclusiveFullscreen);

        if (StringListDropdown("Refresh Rate", gDXGIDisplayRefreshRatesStr, gDXGIDisplayRefreshRatesIndex))
            gUpdateFlags |= UpdateFlags::SwapChainResize;

        ImGui::EndDisabled();

        if (StringListDropdown("Adapter", gDXGIAdapterNames, gDXGIAdapterIndex))
            gUpdateFlags |= UpdateFlags::GraphicsRuntime;

        if (EnumDropdown<WindowMode>("Window Mode", reinterpret_cast<int*>(&gWindowMode)))
            gUpdateFlags |= UpdateFlags::SwapChainResize;

        if (EnumDropdown<SwapEffect>("Swap Effect", reinterpret_cast<int*>(&gDXGISwapEffect)))
            gUpdateFlags |= UpdateFlags::SwapChainRecreate;

        ImGui::SliderInt("V-Sync Interval", &gSyncInterval, 0, 4);

#if 0
        if (ImGui::SliderInt("Buffering", reinterpret_cast<int*>(&gSwapChainImageCount), 2, DXGI_MAX_SWAP_CHAIN_BUFFERS - 1))
            gUpdateFlags |= UpdateFlags::SwapChainRecreate;

        static int gFramesInFlightCount = 1;
        ImGui::SliderInt("Frames in Flight", &gFramesInFlightCount, 1, 16);
#endif
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!gDSRVariantDescs.empty())
            StringListDropdown("DirectSR Algorithm", gDSRVariantNames, gDSRVariantIndex);
        else
            StringListDropdown("DirectSR Algorithm", { "None" }, gDSRVariantIndex);
    }

    if (ImGui::CollapsingHeader("Input", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (EnumDropdown<RenderInputMode>("Input Mode", reinterpret_cast<int*>(&gRenderInputMode)) || gRenderInput == nullptr)
            gUpdateFlags |= UpdateFlags::RenderInputChanged;

        if (gRenderInput)
            gRenderInput->RenderInterface();
    }

    static int selectedPerformanceGraphMode;

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        constexpr std::array<const char*, 2> graphModes = { "Frame Time (Milliseconds)", "Frames-per-Second" };

        StringListDropdown("Graph Mode", graphModes.data(), graphModes.size(), selectedPerformanceGraphMode);
    }

    if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen))
    {

        if (ImGui::BeginChild("##LogChild", ImVec2(0, 200), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
#if 1
            ImGui::TextUnformatted(const_cast<char*>(gLoggerMemory->str().c_str()));
#else
            ImGui::InputTextMultiline("##LogChild",
                                      const_cast<char*>(gLoggerMemory->str().c_str()),
                                      gLoggerMemory->str().size() + 1,
                                      ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16),
                                      ImGuiInputTextFlags_ReadOnly);
#endif

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0F);
        }
        ImGui::EndChild();

        if (ImGui::Button("Copy to Clipboard", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
        {
            ImGui::LogToClipboard();
            ImGui::LogText("%s", gLoggerMemory->str().c_str());
            ImGui::LogFinish();
        }

        if (ImGui::Button("Clear", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
        {
            gLoggerMemory->str("");
            gLoggerMemory->clear();
        }
    }

    ImGui::PopItemWidth();

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2((float)gBackBufferSize.x * 0.25f, (float)gBackBufferSize.y * 0.75f));
    ImGui::SetNextWindowSize(ImVec2((float)gBackBufferSize.x * 0.75f, (float)gBackBufferSize.y * 0.25f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("##PerformanceGraphs", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

    static float elapsedTime = 0;
    elapsedTime += gDeltaTime;

    switch (selectedPerformanceGraphMode)
    {
        case 0:
        {
            float deltaTimeMs = 1000.0f * gDeltaTime;
            gDeltaTimeMovingAverage.AddValue(deltaTimeMs);
            gDeltaTimeBuffer.AddPoint(elapsedTime, deltaTimeMs);
            gDeltaTimeMovingAverageBuffer.AddPoint(elapsedTime, gDeltaTimeMovingAverage.GetAverage());
            break;
        }

        case 1:
        {
            float framesPerSecond = 1.0f / gDeltaTime;
            gDeltaTimeMovingAverage.AddValue(framesPerSecond);
            gDeltaTimeBuffer.AddPoint(elapsedTime, framesPerSecond);
            gDeltaTimeMovingAverageBuffer.AddPoint(elapsedTime, gDeltaTimeMovingAverage.GetAverage());
            break;
        }
    }

    static float history = 3.0f;

    if (ImPlot::BeginPlot("##PerformanceChild", ImVec2(-1, -1)))
    {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, 0x0);
        ImPlot::SetupAxisLimits(ImAxis_X1, elapsedTime - history, elapsedTime, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, gDeltaTimeMovingAverage.GetAverage() * 0.5, gDeltaTimeMovingAverage.GetAverage() * 1.5, ImGuiCond_Always);

        ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 1.0);

        // Custom tick label showing the average ms/fps only.
        {
            double middleTick = gDeltaTimeMovingAverage.GetAverage();

            // Define the label for the middle tick
            auto        averageStr      = std::format("{:.2f}", gDeltaTimeMovingAverage.GetAverage());
            const char* middleTickLabel = averageStr.c_str();

            // Set the custom ticks on the y-axis
            ImPlot::SetupAxisTicks(ImAxis_Y1, &middleTick, 1, &middleTickLabel);
        }

        ImPlot::PlotLine("Exact",
                         &gDeltaTimeBuffer.mData[0].x,
                         &gDeltaTimeBuffer.mData[0].y,
                         gDeltaTimeBuffer.mData.size(),
                         ImPlotLineFlags_None,
                         gDeltaTimeBuffer.mOffset,
                         2 * sizeof(float));

        ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 2.0);

        ImPlot::PlotLine("Smoothed",
                         &gDeltaTimeMovingAverageBuffer.mData[0].x,
                         &gDeltaTimeMovingAverageBuffer.mData[0].y,
                         gDeltaTimeMovingAverageBuffer.mData.size(),
                         ImPlotLineFlags_None,
                         gDeltaTimeMovingAverageBuffer.mOffset,
                         2 * sizeof(float));

        ImPlot::EndPlot();
    }

    ImGui::PopStyleColor(2);

    ImGui::End();

    ImGui::Render();
}
