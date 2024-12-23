#include <Interface.h>
#include <State.h>
#include <OverlayStyle.h>
#include <Util.h>

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

constexpr const char* kVertexShaderInputs = R"(

    // TODO    

)";

constexpr const char* kFragmentShaderShaderToyInputs = R"(

layout (set = 0, binding = 0) uniform UBO
{
    // Constant buffer adapted from ShaderToy inputs.

    vec3      iResolution;           // viewport resolution (in pixels)
    float     iTime;                 // shader playback time (in seconds)
    float     iTimeDelta;            // render time (in seconds)
    float     iFrameRate;            // shader frame rate
    int       iFrame;                // shader playback frame
    float     iChannelTime[4];       // channel playback time (in seconds)
    vec3      iChannelResolution[4]; // channel resolution (in pixels)
    vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click
    vec4      iDate;                 // (year, month, day, time in seconds)
    float     iSampleRate;           // sound sample rate (i.e., 44100)

};

)";

constexpr const char* kFragmentShaderMainInvocation = R"(

    layout (location = 0) out vec4 fragColorOut;

    void main()
    {
        // Invoke the ShaderToy shader.
        mainImage(fragColorOut, vec2(0, 0));
    }

)";

void ShaderToyShaderRequest(const std::string& url)
{
    // 1) Download the shader toy shader.
    // -----------------------------------

    auto data = QueryURL(url);

    // 2) Parse result into JSON.
    // --------------------------

    nlohmann::json parsedData = nlohmann::json::parse(data);

    // 3) Compile GLSL to SPIR-V.
    // ---------------------------

    if (parsedData.contains("Error"))
    {
        spdlog::info("Requested shader is not publically available via API.");
        return;
    }

    // For now, grab the first render pass.
    auto shaderToySource = parsedData["Shader"]["renderpass"][0]["code"].get<std::string>();

    // Compose a GLSL shader that makes the ShaderToy shader Vulkan-conformant.
    const char* shaderStrings[3] = { kFragmentShaderShaderToyInputs, shaderToySource.c_str(), kFragmentShaderMainInvocation };

    auto shaderToyFragmentShaderSPIRV = CompileGLSLToSPIRV(shaderStrings, 3, EShLangFragment);

    if (shaderToyFragmentShaderSPIRV.empty())
        return;

    // 4) Convert SPIR-V to DXIL.
    // --------------------------

    std::vector<uint8_t> shaderToyFragmentShaderDXIL;
    if (!CrossCompileSPIRVToDXIL("main", shaderToyFragmentShaderSPIRV, shaderToyFragmentShaderDXIL))
        spdlog::error("Failed to convert SPIR-V to DXIL.");

    // 5) Load our matching fullscreen triangle vertex shader DXIL.
    // ---------------------------

    std::string vertexShaderPath = "C:\\Development\\ImageQualityReference\\Assets\\Shaders\\Compiled\\FullscreenTriangle.vert.dxil";

    std::vector<uint8_t> fullscreenTriangleVertexShaderDXIL;
    if (!ReadFileBytes(vertexShaderPath, fullscreenTriangleVertexShaderDXIL))
        return;

    // 5) Create Graphics PSO.
    // --------------------------

    std::vector<D3D12_ROOT_PARAMETER> rootParameters;
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.Descriptor.ShaderRegister = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

        rootParameters.push_back(rootParameter);
    }

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};

    rootSignatureDesc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_0;
    rootSignatureDesc.Desc_1_0.NumParameters = static_cast<UINT>(rootParameters.size());
    rootSignatureDesc.Desc_1_0.pParameters   = rootParameters.data();

    ComPtr<ID3DBlob> pBlobSignature;
    ComPtr<ID3DBlob> pBlobError;
    if (FAILED(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &pBlobSignature, &pBlobError)))
    {
        if (pBlobError)
            spdlog::error((static_cast<char*>(pBlobError->GetBufferPointer())));

        return;
    }

    ComPtr<ID3D12RootSignature> pShaderToyPSORootSignature;
    ThrowIfFailed(gLogicalDevice->CreateRootSignature(0,
                                                      pBlobSignature->GetBufferPointer(),
                                                      pBlobSignature->GetBufferSize(),
                                                      IID_PPV_ARGS(&pShaderToyPSORootSignature)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shaderToyPSOInfo = {};
    {
        shaderToyPSOInfo.PS                                = { shaderToyFragmentShaderDXIL.data(), shaderToyFragmentShaderDXIL.size() };
        shaderToyPSOInfo.VS                                = { fullscreenTriangleVertexShaderDXIL.data(), fullscreenTriangleVertexShaderDXIL.size() };
        shaderToyPSOInfo.RasterizerState.FillMode          = D3D12_FILL_MODE_SOLID;
        shaderToyPSOInfo.RasterizerState.CullMode          = D3D12_CULL_MODE_NONE;
        shaderToyPSOInfo.RasterizerState.MultisampleEnable = FALSE;
        shaderToyPSOInfo.PrimitiveTopologyType             = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        shaderToyPSOInfo.SampleMask                        = UINT_MAX;
        shaderToyPSOInfo.NumRenderTargets                  = 1;
        shaderToyPSOInfo.RTVFormats[0]                     = DXGI_FORMAT_R8G8B8A8_UNORM;
        shaderToyPSOInfo.SampleDesc.Count                  = 1;
        shaderToyPSOInfo.pRootSignature                    = pShaderToyPSORootSignature.Get();
    }

    // Compile the PSO in the driver.
    ComPtr<ID3D12PipelineState> shaderToyPSO;
    ThrowIfFailed(gLogicalDevice->CreateGraphicsPipelineState(&shaderToyPSOInfo, IID_PPV_ARGS(&shaderToyPSO)));
}

void Interface::Draw()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)gBackBufferSize.x * 0.25f, (float)gBackBufferSize.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);

    if (ImGui::CollapsingHeader("Presentation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        StringListDropdown("Display", gDXGIOutputNames, gDXGIOutputsIndex);

        ImGui::BeginDisabled(gWindowMode != WindowMode::Windowed);

        if (StringListDropdown("Resolution", gDXGIDisplayResolutionsStr, gDXGIDisplayResolutionsIndex))
            gUpdateFlags |= UpdateFlags::SwapChainResize;

        ImGui::EndDisabled();

        ImGui::BeginDisabled(gWindowMode != WindowMode::ExclusiveFullscreen);

        if (StringListDropdown("Refresh Rate", gDXGIDisplayRefreshRatesStr, gDXGIDisplayRefreshRatesIndex))
            gUpdateFlags |= UpdateFlags::SwapChainResize;

        ImGui::EndDisabled();

        if (ImGui::Button("Download Shader"))
            ShaderToyShaderRequest("https://www.shadertoy.com/api/v1/shaders/MfVfz3?key=BtrjRM");

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

    if (ImGui::CollapsingHeader("Analysis", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("TODO");
    }

    static int selectedPerformanceGraphMode;

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        constexpr std::array<const char*, 2> graphModes = { "Frame Time (Milliseconds)", "Frames-per-Second" };

        StringListDropdown("Graph Mode", graphModes.data(), graphModes.size(), selectedPerformanceGraphMode);
    }

    if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));

        if (ImGui::BeginChild("##LogChild", ImVec2(0, 200), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            // ImGui::TextUnformatted(gLoggerMemory->str().c_str());

            ImGui::InputTextMultiline("##LogChild",
                                      const_cast<char*>(gLoggerMemory->str().c_str()),
                                      gLoggerMemory->str().size() + 1,
                                      ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16),
                                      ImGuiInputTextFlags_ReadOnly);

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0F);
        }
        ImGui::EndChild();

        ImGui::PopStyleColor(2);
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
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, gDeltaTimeMovingAverage.GetAverage() * 2.0, ImGuiCond_Always);

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

    ImGui::End();

    ImGui::Render();
}
