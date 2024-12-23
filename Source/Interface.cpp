#include <Interface.h>
#include <State.h>
#include <OverlayStyle.h>

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

auto WriteData(void* contents, size_t size, size_t nmemb, std::string* userp)
{
    size_t totalSize = size * nmemb;
    userp->append(static_cast<char*>(contents), totalSize);
    return totalSize;
};

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

// Function to compile GLSL to SPIR-V using glslang
std::vector<uint32_t> CompileGLSLToSPIRV(const std::string& source, EShLanguage stage)
{
    (void)source;

    glslang::TShader shader(stage);
    const char*      shaderStrings[3] = { kFragmentShaderShaderToyInputs, source.c_str(), kFragmentShaderMainInvocation };

    shader.setStrings(shaderStrings, 3);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_1);

    if (!shader.parse(GetDefaultResources(), 450, true, EShMsgEnhanced))
    {
        spdlog::error("GLSL Compilation Failed:\n\n{}", shader.getInfoLog());
        return {};
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(EShMsgDefault))
    {
        spdlog::error("Program Linking Failed:\n\n{}", program.getInfoLog());
        return {};
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);

    return spirv;
}

void ShaderToyShaderRequest(const std::string& url)
{
    // 1) Download the shader toy shader.
    // -----------------------------------

    CURL*    curl;
    CURLcode result;

    std::string data;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (!curl)
        return;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    result = curl_easy_perform(curl);

    if (result != CURLE_OK)
    {
        spdlog::error("Failed to download shader toy shader: {}", curl_easy_strerror(result));
        return;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();

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

    auto spirv = CompileGLSLToSPIRV(parsedData["Shader"]["renderpass"][0]["code"].get<std::string>(), EShLangFragment);

    if (spirv.empty())
        return;

    // 4) Convert SPIR-V to DXIL.
    // --------------------------

    dxil_spirv_debug_options debug_opts = {};
    {
        debug_opts.dump_nir = false;
    }

    struct dxil_spirv_runtime_conf conf;
    memset(&conf, 0, sizeof(conf));
    conf.runtime_data_cbv.base_shader_register  = 0;
    conf.runtime_data_cbv.register_space        = 0;
    conf.push_constant_cbv.base_shader_register = 0;
    conf.push_constant_cbv.register_space       = 0;
    conf.first_vertex_and_base_instance_mode    = DXIL_SPIRV_SYSVAL_TYPE_ZERO;
    conf.declared_read_only_images_as_srvs      = true;
    conf.shader_model_max                       = SHADER_MODEL_6_0;

    dxil_spirv_logger logger = {};
    logger.log               = [](void*, const char* msg) { spdlog::info("{}", msg); };

    dxil_spirv_object dxil;

    if (!spirv_to_dxil(spirv.data(),
                       spirv.size(),
                       nullptr,
                       0,
                       DXIL_SPIRV_SHADER_FRAGMENT,
                       "main",
                       DXIL_VALIDATOR_1_6,
                       &debug_opts,
                       &conf,
                       &logger,
                       &dxil))
        spdlog::error("Failed to convert SPIR-V to DXIL.");

    spdlog::error("Successfully converted SPIR-V to DXIL.");

    // 5) Load vertex shader DXIL.
    // ---------------------------

    std::string vertexShaderPath = "C:\\Development\\ImageQualityReference\\Assets\\Shaders\\Compiled\\FullscreenTriangle.vert.dxil";

    std::ifstream file(vertexShaderPath, std::ios::binary | std::ios::ate);

    if (!file)
        throw std::runtime_error("Failed to open file: " + vertexShaderPath);

    // Determine the file size
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Create a buffer to hold the file content
    std::vector<char> buffer(fileSize);

    // Read the file into the buffer
    if (!file.read(buffer.data(), fileSize))
        throw std::runtime_error("Failed to read file: " + vertexShaderPath);

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
        shaderToyPSOInfo.PS                                = { dxil.binary.buffer, dxil.binary.size };
        shaderToyPSOInfo.VS                                = { buffer.data(), buffer.size() };
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

    ComPtr<ID3D12PipelineState> shaderToyPSO;
    ThrowIfFailed(gLogicalDevice->CreateGraphicsPipelineState(&shaderToyPSOInfo, IID_PPV_ARGS(&shaderToyPSO)));

    spirv_to_dxil_free(&dxil);
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
