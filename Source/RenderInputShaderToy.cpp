#include <RenderInputShaderToy.h>
#include <Util.h>
#include <State.h>

namespace ICR
{

    RenderInputShaderToy::RenderInputShaderToy() : mURL(256, '\0')
    {
        // Initialize the shadertoy to a known-good one.
        mURL = "https://www.shadertoy.com/api/v1/shaders/MfVfz3?key=BtrjRM";
    }

    RenderInputShaderToy::~RenderInputShaderToy() {}

    void RenderInputShaderToy::Initialize() {}

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
            mainImage(fragColorOut, gl_FragCoord.xy);

            fragColorOut = vec4(gl_FragCoord.xy / vec2(1920.0, 1440.0), 0.0, 1.0);
        }

    )";

    void CompileShaderToyToGraphicsPSO(const std::string& url, ID3D12RootSignature** ppRootSignature, ID3D12PipelineState** ppPSO)
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
    }

    void RenderInputShaderToy::RenderInterface()
    {
        if (ImGui::BeginChild("##ShaderToy", ImVec2(-1, 300), ImGuiChildFlags_Borders))
        {
            ImGui::InputText("URL", mURL.data(), mURL.size());

            ImGui::SameLine();

            if (ImGui::Button("Load"))
                CompileShaderToyToGraphicsPSO(mURL, &mRootSignature, &mPSO);
        }

        ImGui::EndChild();
    }

    void RenderInputShaderToy::Render(const FrameParams& frameParams)
    {
        if (!mPSO)
            return;

        // Hack a viewport
        D3D12_VIEWPORT viewport = {};
        {
            viewport.TopLeftX = 1920 * 0.25f;
            viewport.TopLeftY = 0;
            viewport.Width    = 1920 * 0.75f;
            viewport.Height   = 1440 * 0.75f;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
        }

        D3D12_RECT scissor = {};
        {
            scissor.left   = static_cast<LONG>(viewport.TopLeftX);
            scissor.top    = static_cast<LONG>(viewport.TopLeftY);
            scissor.right  = static_cast<LONG>(1920);
            scissor.bottom = static_cast<LONG>(1440);
        }

        frameParams.pCmd->RSSetScissorRects(1U, &scissor);
        frameParams.pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        frameParams.pCmd->RSSetViewports(1U, &viewport);
        frameParams.pCmd->SetGraphicsRootSignature(mRootSignature.Get());
        frameParams.pCmd->SetPipelineState(mPSO.Get());
        frameParams.pCmd->DrawInstanced(3U, 1U, 0U, 0U);
    }

    void RenderInputShaderToy::Release() {}

} // namespace ICR
