#include <RenderInputShaderToy.h>
#include <Util.h>
#include <State.h>

namespace ICR
{

    RenderInputShaderToy::RenderInputShaderToy() : mShaderID(256, '\0')
    {
        // Initialize the shadertoy to a known-good one.
        // "Raymarching - Primitives" https://www.shadertoy.com/view/Xds3zN
        memcpy(mShaderID.data(), "Xds3zN", 6);

        // Initial async compile state.
        mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Idle);
    }

    RenderInputShaderToy::~RenderInputShaderToy() {}

    bool CompileShaderToyToGraphicsPSO(const std::string& shaderID, ID3D12RootSignature** ppRootSignature, ID3D12PipelineState** ppPSO);

    void RenderInputShaderToy::Initialize()
    {
        if (mInitialized)
            return;

        //  Resources
        // ---------------------------

        D3D12_RESOURCE_DESC shaderToyUBO = {};
        {
            shaderToyUBO.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
            shaderToyUBO.Alignment          = 0;
            shaderToyUBO.Width              = sizeof(Constants);
            shaderToyUBO.Height             = 1;
            shaderToyUBO.DepthOrArraySize   = 1;
            shaderToyUBO.MipLevels          = 1;
            shaderToyUBO.Format             = DXGI_FORMAT_UNKNOWN;
            shaderToyUBO.SampleDesc.Count   = 1;
            shaderToyUBO.SampleDesc.Quality = 0;
            shaderToyUBO.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            shaderToyUBO.Flags              = D3D12_RESOURCE_FLAG_NONE;
        }

        D3D12MA::ALLOCATION_DESC shaderToyUBOAllocDesc = {};
        {
            shaderToyUBOAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
        }

        ThrowIfFailed(gMemoryAllocator->CreateResource(&shaderToyUBOAllocDesc,
                                                       &shaderToyUBO,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ,
                                                       nullptr,
                                                       mUBOAllocation.GetAddressOf(),
                                                       IID_PPV_ARGS(mUBO.GetAddressOf())));

        ThrowIfFailed(mUBO->Map(0, nullptr, &mpUBOData));

        // Descriptor heaps.
        // ---------------------------

        D3D12_DESCRIPTOR_HEAP_DESC constantBufferViewHeapDesc = {};
        {
            constantBufferViewHeapDesc.NumDescriptors = 1;
            constantBufferViewHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            constantBufferViewHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        }
        ThrowIfFailed(gLogicalDevice->CreateDescriptorHeap(&constantBufferViewHeapDesc, IID_PPV_ARGS(&mUBOHeap)));

        // Resource views
        // ---------------------------

        D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc = {};
        {
            constantBufferViewDesc.BufferLocation = mUBO->GetGPUVirtualAddress();
            constantBufferViewDesc.SizeInBytes    = sizeof(Constants);
        }
        gLogicalDevice->CreateConstantBufferView(&constantBufferViewDesc, mUBOHeap->GetCPUDescriptorHandleForHeapStart());

        // Re-load PSO
        if (!mShaderID.empty())
        {
            mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Compiling);

            gTaskGroup.run(
                [&]()
                {
                    if (CompileShaderToyToGraphicsPSO(mShaderID, &mRootSignature, &mPSO))
                        mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Compiled);
                    else
                        mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Failed);
                });
        }

        mInitialized = true;
    }

    constexpr const char* kFragmentShaderShaderToyInputs = R"(

        layout (set = 0, binding = 0) uniform UBO
        {
            // Add some application-specific inputs.
            vec4 iAppViewport;

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

            // Pad-up to 256 bytes.
            float padding[28];
        };

    )";

    constexpr const char* kFragmentShaderMainInvocation = R"(

        layout (location = 0) out vec4 fragColorOut;

        void main()
        {
            vec2 fragCoordViewport = gl_FragCoord.xy;

            fragCoordViewport.x -= iAppViewport.x;
            fragCoordViewport.y  = iResolution.y - fragCoordViewport.y;

            // Invoke the ShaderToy shader.
            mainImage(fragColorOut, fragCoordViewport);
        }

    )";

    void BuildRenderGraph(const nlohmann::json& parsedShaderToy)
    {
        tf::Taskflow renderGraph;

        std::unordered_map<int, tf::Task> renderPassTasks;

        auto ForEachRenderPass = [&](const std::function<void(const nlohmann::json&, const int&)>& func)
        {
            // Scan 1): Create a task for each render pass.
            for (const auto& renderPass : parsedShaderToy["Shader"]["renderpass"])
            {
                int renderPassId;

                if (renderPass["outputs"].empty())
                {
                    // Handle special case for the "Common" file.
                    return; // renderPassId = -1;
                }
                else
                {
                    // Otherwise ShaderToy renderpasses only have one output.
                    renderPassId = renderPass["outputs"][0]["id"].get<int>();
                }

                func(renderPass, renderPassId);
            }
        };

        // Scan 1): Create a task for each render pass.
        ForEachRenderPass([&](const nlohmann::json&, const int& id) { renderPassTasks[id] = renderGraph.emplace([id]() { spdlog::info(id); }); });

        // Scan 2): Create dependencies between render passes.
        ForEachRenderPass(
            [&](const nlohmann::json& renderPass, const int& id)
            {
                for (const auto& input : renderPass["inputs"])
                {
                    renderPassTasks[input["id"]].precede(renderPassTasks[id]);
                }
            });

        tf::Executor executor;

        executor.run(renderGraph).wait();
    }

    bool CompileShaderToyToGraphicsPSO(const std::string& shaderID, ID3D12RootSignature** ppRootSignature, ID3D12PipelineState** ppPSO)
    {
        // 1) Download the shader toy shader.
        // -----------------------------------
        auto data = QueryURL("https://www.shadertoy.com/api/v1/shaders/" + shaderID.substr(0, 6) + "?key=BtrjRM");

        // 2) Parse result into JSON.
        // --------------------------

        nlohmann::json parsedData = nlohmann::json::parse(data);

        spdlog::debug(parsedData.dump(4));

        // 3) Compile GLSL to SPIR-V.
        // ---------------------------

        if (parsedData.contains("Error"))
        {
            spdlog::info("Requested shader is not publically available via API.");
            return false;
        }

        // Build task-graph.
        // BuildRenderGraph(parsedData);

        // For now, grab the first render pass.
        auto shaderToySource = parsedData["Shader"]["renderpass"][0]["code"].get<std::string>();

        // Compose a GLSL shader that makes the ShaderToy shader Vulkan-conformant.
        const char* shaderStrings[3] = { kFragmentShaderShaderToyInputs, shaderToySource.c_str(), kFragmentShaderMainInvocation };

        auto shaderToyFragmentShaderSPIRV = CompileGLSLToSPIRV(shaderStrings, 3, EShLangFragment);

        if (shaderToyFragmentShaderSPIRV.empty())
            return false;

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
            return false;

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

            return false;
        }

        ThrowIfFailed(gLogicalDevice->CreateRootSignature(0,
                                                          pBlobSignature->GetBufferPointer(),
                                                          pBlobSignature->GetBufferSize(),
                                                          IID_PPV_ARGS(ppRootSignature)));

        D3D12_BLEND_DESC blendDesc               = {};
        blendDesc.AlphaToCoverageEnable          = FALSE;
        blendDesc.IndependentBlendEnable         = FALSE;
        blendDesc.RenderTarget[0].BlendEnable    = FALSE;
        blendDesc.RenderTarget[0].LogicOpEnable  = FALSE;
        blendDesc.RenderTarget[0].SrcBlend       = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend      = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].LogicOp        = D3D12_LOGIC_OP_NOOP;
        blendDesc.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE | D3D12_COLOR_WRITE_ENABLE_ALPHA;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC shaderToyPSOInfo = {};
        {
            shaderToyPSOInfo.PS                       = { shaderToyFragmentShaderDXIL.data(), shaderToyFragmentShaderDXIL.size() };
            shaderToyPSOInfo.VS                       = { fullscreenTriangleVertexShaderDXIL.data(), fullscreenTriangleVertexShaderDXIL.size() };
            shaderToyPSOInfo.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            shaderToyPSOInfo.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            shaderToyPSOInfo.RasterizerState.MultisampleEnable = FALSE;
            shaderToyPSOInfo.BlendState                        = blendDesc;
            shaderToyPSOInfo.PrimitiveTopologyType             = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            shaderToyPSOInfo.SampleMask                        = UINT_MAX;
            shaderToyPSOInfo.NumRenderTargets                  = 1;
            shaderToyPSOInfo.RTVFormats[0]                     = DXGI_FORMAT_R8G8B8A8_UNORM;
            shaderToyPSOInfo.SampleDesc.Count                  = 1;
            shaderToyPSOInfo.pRootSignature                    = *ppRootSignature;
        }

        // Compile the PSO in the driver.
        ThrowIfFailed(gLogicalDevice->CreateGraphicsPipelineState(&shaderToyPSOInfo, IID_PPV_ARGS(ppPSO)));

        return true;
    }

    void ProcessShaderToyRequestAsync() {}

    void RenderInputShaderToy::RenderInterface()
    {
        if (mAsyncCompileStatus.load() == AsyncCompileShaderToyStatus::Compiling)
        {
            ImGui::SetNextWindowPos(ImVec2(gViewport.TopLeftX + (0.5f * gViewport.Width), gViewport.Height * 0.5f),
                                    ImGuiCond_Always,
                                    ImVec2(0.5f, 0.5f));

            ImGui::Begin("##ShaderToyProgress", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
            ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(0, 0), "Loading");
            ImGui::End();
        }

        if (ImGui::BeginChild("##ShaderToy", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders))
        {
            ImGui::InputText("Shader ID", mShaderID.data(), mShaderID.size());

            if (ImGui::Button("Load", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
            {
                gPreRenderTaskQueue.push(
                    [&]()
                    {
                        Release();
                        Initialize();
                    });
            }

            do
            {
                if (mAsyncCompileStatus.load() != AsyncCompileShaderToyStatus::Compiled)
                    break;

                if (ImGui::Button("Modify", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {}
            }
            while (false);
        }

        ImGui::EndChild();
    }

    void RenderInputShaderToy::Render(const FrameParams& frameParams)
    {
        if (!mInitialized)
            return;

        // Check PSO compile status.
        switch (mAsyncCompileStatus.load())
        {
            case AsyncCompileShaderToyStatus::Compiling:
            case AsyncCompileShaderToyStatus::Failed:
            case AsyncCompileShaderToyStatus::Idle     : return;
            default                                    : break;
        };

        D3D12_RECT scissor = {};
        {
            scissor.left   = static_cast<LONG>(0);
            scissor.top    = static_cast<LONG>(0);
            scissor.right  = static_cast<LONG>(gBackBufferSize.x);
            scissor.bottom = static_cast<LONG>(gBackBufferSize.y);
        }

        static float elapsedSeconds = 0;
        static int   elapsedFrames  = 0;

        Constants constants = {};
        {
            constants.iResolution.x = gViewport.Width;
            constants.iResolution.y = gViewport.Height;
            constants.iTime         = elapsedSeconds;
            constants.iFrame        = elapsedFrames;
            constants.iTimeDelta    = gDeltaTime;
            constants.iFrameRate    = 1.0f / gDeltaTime;
            constants.iAppViewport  = { gViewport.TopLeftX, gViewport.TopLeftY, gViewport.Width, gViewport.Height };
        }
        memcpy(mpUBOData, &constants, sizeof(Constants));

        elapsedSeconds += gDeltaTime;
        elapsedFrames++;

        frameParams.pCmd->SetGraphicsRootSignature(mRootSignature.Get());
        frameParams.pCmd->SetDescriptorHeaps(1U, mUBOHeap.GetAddressOf());
        frameParams.pCmd->SetGraphicsRootConstantBufferView(0u, mUBO->GetGPUVirtualAddress());
        frameParams.pCmd->RSSetScissorRects(1U, &scissor);
        frameParams.pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        frameParams.pCmd->RSSetViewports(1U, &gViewport);
        frameParams.pCmd->SetPipelineState(mPSO.Get());
        frameParams.pCmd->DrawInstanced(3U, 1U, 0U, 0U);
    }

    void RenderInputShaderToy::Release()
    {
        if (!mInitialized)
            return;

        mUBO->Unmap(0, nullptr);
        mUBOAllocation.Reset();
        mUBO.Reset();
        mUBOHeap.Reset();
        mRootSignature.Reset();
        mPSO.Reset();

        mInitialized = false;
    }

} // namespace ICR
