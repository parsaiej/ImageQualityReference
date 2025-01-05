#include <RenderInputShaderToy.h>
#include <Util.h>
#include <Blitter.h>
#include <ResourceRegistry.h>
#include <State.h>

namespace ICR
{
    // For managing of history buffers, we keep an internal counter here and use it to flip current + history buffers.
    uint32_t gInternalFrameIndex = 0;

    static float elapsedSeconds = 0;
    static int   elapsedFrames  = 0;

    // Shorthand for getting current + history resources.
    int GetCurrentFrameIndex() { return (gInternalFrameIndex + 0) % 2; }
    int GetHistoryFrameIndex() { return (gInternalFrameIndex + 1) % 2; }

    constexpr const char* kFragmentShaderShaderToyInputs = R"(

        layout (set = 0, binding = 0, std140) uniform UBO
        {
            // Add some application-specific inputs.
            vec4 iAppParams0;

            // Constant buffer adapted from ShaderToy inputs.
            vec3      iResolution;           // viewport resolution (in pixels)
            float     ipadding0;

            float     iTime;                 // shader playback time (in seconds)
            float     iTimeDelta;            // render time (in seconds)
            float     iFrameRate;            // shader frame rate
            int       iFrame;                // shader playback frame

            vec4      iChannelTime;          // channel playback time (in seconds)
            vec4      iChannelResolution[4]; // channel resolution (in pixels)

            vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click
            vec4      iDate;                 // (year, month, day, time in seconds)

            vec3      ipadding1;
            float     iSampleRate;           // sound sample rate (i.e., 44100)

            // Pad-up to 256 bytes.
            vec4 padding[5];
        };

        // Types are defined at runtime in the preample.
        layout (set = 1, binding = 0) uniform SAMPLER_TYPE0 iChannel0;
        layout (set = 1, binding = 1) uniform SAMPLER_TYPE1 iChannel1;
        layout (set = 1, binding = 2) uniform SAMPLER_TYPE2 iChannel2;
        layout (set = 1, binding = 3) uniform SAMPLER_TYPE3 iChannel3;

    )";

    constexpr const char* kFragmentShaderMainInvocation = R"(

        layout (location = 0) out vec4 fragColorOut;

        void main()
        {
            // Invoke the ShaderToy shader.
            mainImage(fragColorOut, gl_FragCoord.xy);
        }

    )";

    constexpr const char* kUnsupportedInputs[1] = { "keyboard" };

    // Render Pass
    // -------------------------------------------------

    using RenderPass = RenderInputShaderToy::RenderPass;

    RenderPass::RenderPass(const RenderPass::Args& args)
    {
        // Resolve the output ID.
        // WARNING: Currently ShaderToy does not support MRT, so we assume there will only ever be one output per-pass.
        mOutputID = args.renderPassInfo["outputs"][0]["id"].get<int>();

        // Intermediate renderpasses need full float format and flipped viewport.
        mIntermediateRenderPass = args.renderPassInfo["type"] == "buffer";

        CreateOutputTargets();

        // Create a descriptor heap for 4 samplers
        // ------------------------------------------------

        D3D12_DESCRIPTOR_HEAP_DESC samplerHeapInfo = {};
        {
            samplerHeapInfo.NumDescriptors = 4;
            samplerHeapInfo.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            samplerHeapInfo.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        }
        ThrowIfFailed(gLogicalDevice->CreateDescriptorHeap(&samplerHeapInfo, IID_PPV_ARGS(&mInputSamplerDescriptorHeap)));

        // Obtain a handle to the first sampler.
        CD3DX12_CPU_DESCRIPTOR_HANDLE samplerHandle(mInputSamplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        // Resolve the input IDs.
        // ------------------------------------------------

        for (const auto& input : args.renderPassInfo["inputs"])
        {
            // Check if input is any of the unsupported ones.
            for (const auto& unsupportedInput : kUnsupportedInputs)
            {
                if (input["ctype"].get<std::string>() == unsupportedInput)
                    throw std::runtime_error("Unsupported input type.");
            }

            if (input.contains("sampler"))
            {
                auto& samplerInfo = input["sampler"];

                D3D12_SAMPLER_DESC samplerDesc = {};
                {
                    D3D12_TEXTURE_ADDRESS_MODE addressMode;

                    if (samplerInfo["wrap"].get<std::string>() == "clamp")
                        addressMode = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    else
                        addressMode = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

                    samplerDesc.Filter        = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                    samplerDesc.AddressU      = addressMode;
                    samplerDesc.AddressV      = addressMode;
                    samplerDesc.AddressW      = addressMode;
                    samplerDesc.MinLOD        = 0;
                    samplerDesc.MaxLOD        = D3D12_FLOAT32_MAX;
                    samplerDesc.MaxAnisotropy = 1;
                }
                gLogicalDevice->CreateSampler(&samplerDesc, samplerHandle);

                // Advance pointer to the next sampler descriptor.
                samplerHandle.Offset(1, gSMPDescriptorSize);
            }

            mInputIDs.push_back(input["id"].get<int>());

            mInputToChannelMap[mInputIDs.back()] = input["channel"].get<int>();
        }

        // Compile PSO.
        // ------------------------------------------------
        {
            // 1) Compile GLSL to SPIR-V.
            // --------------------------

            auto renderPassSourceCodeGLSL = args.renderPassInfo["code"].get<std::string>();

            // Compose a GLSL shader that makes the ShaderToy shader Vulkan-conformant.
            const char* shaderStrings[4] = { kFragmentShaderShaderToyInputs,
                                             args.commonShaderGLSL.c_str(),
                                             renderPassSourceCodeGLSL.c_str(),
                                             kFragmentShaderMainInvocation };

            // Build a preamble that defines the correct sampler types for each channel.
            std::stringstream preambleStream;
            {
                preambleStream << std::endl;

                // Shadertoy supports max of 4 input channels.
                for (int samplerIndex = 0; samplerIndex < 4; samplerIndex++)
                {
                    preambleStream << std::format("#define SAMPLER_TYPE{} ", samplerIndex);

                    bool sampleIndexHasInput = false;

                    // Scan the inputs for non-2d samplers.
                    for (const auto& input : args.renderPassInfo["inputs"])
                    {
                        if (input["channel"].get<int>() == samplerIndex)
                        {
                            auto inputType = input["ctype"].get<std::string>();

                            if (inputType == "volume")
                                preambleStream << "sampler3D";

                            if (inputType == "cubemap")
                                preambleStream << "samplerCube";

                            if (inputType == "buffer" || inputType == "texture")
                                preambleStream << "sampler2D";

                            sampleIndexHasInput = true;

                            break;
                        }
                    }

                    if (!sampleIndexHasInput)
                    {
                        // Default to 2D if no input is found.
                        preambleStream << "sampler2D";
                    }

                    preambleStream << std::endl;
                }
            }

            // Cache the SPIR-V in case user requests decompiled HLSL.
            mSPIRV = CompileGLSLToSPIRV(shaderStrings, ARRAYSIZE(shaderStrings), EShLangFragment, preambleStream.str().c_str());

            if (mSPIRV.empty())
                throw std::runtime_error("Failed to compile GLSL to SPIR-V.");

            // 2) Convert SPIR-V to DXIL.
            // --------------------------

            std::vector<uint8_t> renderPassDXIL;
            if (!CrossCompileSPIRVToDXIL("main", mSPIRV, renderPassDXIL))
                throw std::runtime_error("Failed to cross-compile SPIR-V to DXIL.");

            // 3) Create Graphics PSO.
            // --------------------------

            D3D12_GRAPHICS_PIPELINE_STATE_DESC shaderToyPSOInfo = {};
            {
                const auto& fullscreenTriangleDXIL = gShaderDXIL["FullscreenTriangle.vert"];

                shaderToyPSOInfo.PS                    = { renderPassDXIL.data(), renderPassDXIL.size() };
                shaderToyPSOInfo.VS                    = { fullscreenTriangleDXIL->GetBufferPointer(), fullscreenTriangleDXIL->GetBufferSize() };
                shaderToyPSOInfo.RasterizerState       = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                shaderToyPSOInfo.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                shaderToyPSOInfo.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                shaderToyPSOInfo.SampleMask            = UINT_MAX;
                shaderToyPSOInfo.NumRenderTargets      = 1;
                shaderToyPSOInfo.RTVFormats[0]         = mIntermediateRenderPass ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
                shaderToyPSOInfo.SampleDesc.Count      = 1;
                shaderToyPSOInfo.pRootSignature        = args.pRootSignature;
            }

            // Compile the PSO in the driver.
            if (gLogicalDevice->CreateGraphicsPipelineState(&shaderToyPSOInfo, IID_PPV_ARGS(&mPSO)) != S_OK)
                throw std::runtime_error("Failed to create graphics PSO.");
        }
    }

    void RenderPass::CreateOutputTargets()
    {
        DXGI_FORMAT outputFormat;

        if (mIntermediateRenderPass)
            outputFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        else
            outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

        auto CreateOutputColorBuffer = [&](ResourceHandle& colorBufferHandle)
        {
            if (colorBufferHandle.indexResource != UINT_MAX)
                gResourceRegistry->Release(colorBufferHandle);

            auto colorBufferInfo = CD3DX12_RESOURCE_DESC::Tex2D(outputFormat,
                                                                static_cast<UINT>(gViewport.Width),
                                                                static_cast<UINT>(gViewport.Height),
                                                                1,
                                                                1,
                                                                1,
                                                                0,
                                                                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

            colorBufferHandle = gResourceRegistry->Create(colorBufferInfo, DescriptorHeap::Type::RenderTarget | DescriptorHeap::Type::Texture2D);
        };

        CreateOutputColorBuffer(mOutputTargets[0]);
        CreateOutputColorBuffer(mOutputTargets[1]);

        {
            SetDebugName(gResourceRegistry->Get(mOutputTargets[0]), L"RenderPassOutputA");
            SetDebugName(gResourceRegistry->Get(mOutputTargets[1]), L"RenderPassOutputB");
        }

        for (auto& outputTarget : mOutputTargets)
        {
            auto* pRenderTargetHeap = gResourceRegistry->GetDescriptorHeap(DescriptorHeap::Type::RenderTarget);

            auto outputDescriptorHandle = pRenderTargetHeap->GetAddressCPU(outputTarget.indexDescriptorRenderTarget);

            ExecuteCommandListAndWait(gLogicalDevice.Get(),
                                      gCommandQueue.Get(),
                                      [&](ID3D12GraphicsCommandList* pCmd)
                                      {
                                          auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(gResourceRegistry->Get(outputTarget),
                                                                                              D3D12_RESOURCE_STATE_COMMON,
                                                                                              D3D12_RESOURCE_STATE_RENDER_TARGET);

                                          pCmd->ResourceBarrier(1, &barrier);
                                          pCmd->ClearRenderTargetView(outputDescriptorHandle, clearColor, 0, nullptr);
                                      });
        }
    }

    void RenderPass::CreateInputResourceDescriptorTable(const std::unordered_map<int, std::array<ResourceHandle, 2>>& resourceCache)
    {
        // Create a descriptor heap for 4 resources x 2 frames (history).
        // ------------------------------------------------

        D3D12_DESCRIPTOR_HEAP_DESC resourceDescriptorHeapInfo = {};
        {
            resourceDescriptorHeapInfo.NumDescriptors = 4 * 2;
            resourceDescriptorHeapInfo.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            resourceDescriptorHeapInfo.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        }
        ThrowIfFailed(gLogicalDevice->CreateDescriptorHeap(&resourceDescriptorHeapInfo, IID_PPV_ARGS(&mInputResourceDescriptorHeap)));

        for (int frameIndex = 0; frameIndex < 2; frameIndex++)
        {
            for (int inputID : mInputIDs) // NOTE: Should never exceed 4.
            {
                if (!resourceCache.contains(inputID))
                    continue;

                // Generate a pointer to the resource descriptor.
                CD3DX12_CPU_DESCRIPTOR_HANDLE resourceDescriptorHandle(mInputResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                                       (4 * frameIndex) + mInputToChannelMap[inputID],
                                                                       gSRVDescriptorSize);

                // Create SRV for the resource.
                // ------------------------------------------------

                auto* pResource = gResourceRegistry->Get(resourceCache.at(inputID)[frameIndex]);

                // Recover the image's format.
                auto resourceInfo = pResource->GetDesc();

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                {
                    srvDesc.Format                    = resourceInfo.Format;
                    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.Texture2D.MostDetailedMip = 0;
                    srvDesc.Texture2D.MipLevels       = 1;
                }
                gLogicalDevice->CreateShaderResourceView(pResource, &srvDesc, resourceDescriptorHandle);
            }
        }
    }

    static std::mutex gRenderPassCommandMutex;

    void RenderPass::Dispatch(ID3D12GraphicsCommandList* pCmd)
    {
        // Need to lock here because taskflow may dispatch two tasks at the same time that do not depend on each other.
        std::lock_guard<std::mutex> commandLock(gRenderPassCommandMutex);

        // Bind the output render target.
        // ------------------------------------------------

        auto renderTargetsHeap = gResourceRegistry->GetDescriptorHeap(DescriptorHeap::Type::RenderTarget);

        auto currentOutputRenderTargetView = renderTargetsHeap->GetAddressCPU(mOutputTargets[GetCurrentFrameIndex()].indexDescriptorRenderTarget);
        gCommandList->OMSetRenderTargets(1, &currentOutputRenderTargetView, FALSE, nullptr);

        // Bind the input heaps.
        // ------------------------------------------------
        ID3D12DescriptorHeap* ppHeaps[2] = { mInputResourceDescriptorHeap.Get(), mInputSamplerDescriptorHeap.Get() };
        pCmd->SetDescriptorHeaps(ARRAYSIZE(ppHeaps), ppHeaps);

        // Bind the samplers.
        // ------------------------------------------------
        pCmd->SetGraphicsRootDescriptorTable(1, mInputSamplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

        // Bind the current frame's resources
        // ------------------------------------------------
        {
            // Obtain handle to the base of the resource descriptors.
            CD3DX12_GPU_DESCRIPTOR_HANDLE inputResourceDescriptorHandle(mInputResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

            // Offset w.r.t. the frame index.
            inputResourceDescriptorHandle.Offset(4 * GetHistoryFrameIndex(), gSRVDescriptorSize);

            // Bind the input resources.
            pCmd->SetGraphicsRootDescriptorTable(2, inputResourceDescriptorHandle);
        }

        // Bind the PSO.
        // ------------------------------------------------
        pCmd->SetPipelineState(mPSO.Get());

        // Draw the fullscreen triangle.
        // ------------------------------------------------
        pCmd->DrawInstanced(3U, 1U, 0U, 0U);
    }

    // -------------------------------------------------

    RenderInputShaderToy::RenderInputShaderToy() : mShaderID(256, '\0'), mInitialized(false), mUserRequestUnload(false)
    {
        // Initialize the shadertoy to a known-good one.
        // "fractal pyramid" https://www.shadertoy.com/view/tsXBzS
        memcpy(mShaderID.data(), "tsXBzS", 6);

        // Initial async compile state.
        mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Idle);
    }

    RenderInputShaderToy::~RenderInputShaderToy() {}

    void RenderInputShaderToy::Initialize()
    {
        if (mInitialized)
            return;

        //  Resources
        // ---------------------------

        mUBO = gResourceRegistry->Create(CD3DX12_RESOURCE_DESC::Buffer(sizeof(Constants)), DescriptorHeap::Type::Constants, true);

        ThrowIfFailed(gResourceRegistry->Get(mUBO)->Map(0, nullptr, &mpUBOData));

        // Create the root signature (same for all render passes).
        // ---------------------------

        {
            D3D12_DESCRIPTOR_RANGE1 inputSMPRanges = {};
            {
                inputSMPRanges.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                inputSMPRanges.NumDescriptors     = 4;
                inputSMPRanges.BaseShaderRegister = 0;
                inputSMPRanges.RegisterSpace      = 1;
            }

            D3D12_DESCRIPTOR_RANGE1 inputSRVRanges = {};
            {
                inputSRVRanges.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                inputSRVRanges.NumDescriptors     = 4;
                inputSRVRanges.BaseShaderRegister = 0;
                inputSRVRanges.RegisterSpace      = 1;
            }

            std::array<CD3DX12_ROOT_PARAMETER1, 3> rootParameters;
            {
                // Constants (includes the ShaderToy inputs).
                rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);

                // These can't be static samplers since it can vary per-pass.
                rootParameters[1].InitAsDescriptorTable(1, &inputSMPRanges, D3D12_SHADER_VISIBILITY_PIXEL);
                rootParameters[2].InitAsDescriptorTable(1, &inputSRVRanges, D3D12_SHADER_VISIBILITY_PIXEL);
            }

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};

            rootSignatureDesc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_0;
            rootSignatureDesc.Desc_1_0.NumParameters = static_cast<UINT>(rootParameters.size());
            rootSignatureDesc.Desc_1_0.pParameters   = reinterpret_cast<const D3D12_ROOT_PARAMETER*>(rootParameters.data());

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
                                                              IID_PPV_ARGS(&mRootSignature)));
        }

        // Compile
        // ---------------------------

        // Set am idle compile status before attempting anything.
        mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Idle);

        // Re-load PSO
        if (!mShaderID.empty() && !mUserRequestUnload)
        {
            mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Compiling);

            gTaskGroup.run(
                [&]()
                {
                    if (CompileShaderToy(mShaderID))
                        mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Compiled);
                    else
                        mAsyncCompileStatus.store(AsyncCompileShaderToyStatus::Failed);
                });
        }

        mInitialized = true;
    }

    ID3D12GraphicsCommandList* pCommandList = nullptr;

    bool RenderInputShaderToy::BuildRenderGraph(const nlohmann::json& parsedShaderToy)
    {
        // Intermediate memory for tracking renderpass dependencies.
        std::unordered_map<int, tf::Task> renderPassTaskMap;

        mRenderGraph.clear();
        mRenderPasses.clear();
        mCommonShaderGLSL.clear();
        mResourceCache.clear();
        mMediaResources.clear();

        // Scan 1) Pre-pass for the common shader.
        for (const auto& renderPassInfo : parsedShaderToy["Shader"]["renderpass"])
        {
            if (renderPassInfo["name"] != "Common")
                continue;

            // Extract the common shader which is just a fake render pass that
            // serves as a container for the common shader code.
            mCommonShaderGLSL = renderPassInfo["code"].get<std::string>();
            break;
        }

        // Scan 2) Initialize all render passes.
        for (const auto& renderPassInfo : parsedShaderToy["Shader"]["renderpass"])
        {
            if (renderPassInfo["name"] == "Common")
                continue;

            try
            {
                RenderPass::Args renderPassArgs = { mRootSignature.Get(), renderPassInfo, mCommonShaderGLSL };

                // Initialize the render pass.
                mRenderPasses.push_back(std::make_unique<RenderPass>(renderPassArgs));
            }
            catch (std::runtime_error& e)
            {
                spdlog::critical("Failed to initialize render pass: {}", e.what());
                return false;
            }

            // Get the recently added render pass.
            auto* renderPass = mRenderPasses.back().get();

            // Keep track of the final render pass.
            if (renderPassInfo["name"] == "Image")
                mpFinalRenderPass = renderPass;

            // Insert the render pass output into the input provider.
            mResourceCache[renderPass->GetOutputID()][0] = renderPass->GetOutputResources()[0];
            mResourceCache[renderPass->GetOutputID()][1] = renderPass->GetOutputResources()[1];

            // Insert the render pass into the render graph.
            renderPassTaskMap[renderPass->GetOutputID()] = mRenderGraph.emplace([this, renderPass]() { renderPass->Dispatch(mpActiveCommandList); });
        }

        // Parse all non-buffer inputs.
        // ---------------------------------

        std::vector<nlohmann::json> mediaInputs;

        for (const auto& renderPass : parsedShaderToy["Shader"]["renderpass"])
        {
            for (const auto& input : renderPass["inputs"])
            {
                auto inputType = input["ctype"].get<std::string>();

                // Check if any unsupported input is detected.
                if (std::find(std::begin(kUnsupportedInputs), std::end(kUnsupportedInputs), inputType) != std::end(kUnsupportedInputs))
                {
                    spdlog::info("Shader requires unsupported input: {}", inputType);
                    return false;
                }

                if (inputType == "texture" || inputType == "cubemap")
                    mediaInputs.push_back(input);
            }
        }

        // Obtain any media from server.
        // ---------------------------------

        std::unordered_set<int> processedInputs;

        for (const auto& mediaInput : mediaInputs)
        {
            int mediaInputId = mediaInput["id"].get<int>();

            if (processedInputs.contains(mediaInputId))
                continue;

            processedInputs.insert(mediaInputId);

            // Pull image bytes from the API.
            // ------------------------------

            auto data = QueryURL<std::vector<uint8_t>>("https://www.shadertoy.com" + mediaInput["src"].get<std::string>());

            if (data.empty())
                return false;

            // Load the image
            // ------------------------------

            stbi_set_flip_vertically_on_load(true);

            int  width, height, channels;
            auto pImage = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, STBI_rgb_alpha);

            if (!pImage)
            {
                spdlog::error("Failed to load image from bytes.");
                return false;
            }

            ResourceHandle mediaResourceHandle =
                gResourceRegistry->CreateWithData(CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1),
                                                  0x0, // Manually managed descriptor heaps.
                                                  pImage,
                                                  width * height * 4);

            // Transfer the resource to the media resource cache.
            mMediaResources.push_back(std::move(mediaResourceHandle));

            // And specify it here.
            mResourceCache[mediaInputId][0] = mMediaResources.back();
            mResourceCache[mediaInputId][1] = mResourceCache[mediaInputId][0]; // No history for media.
        }

        // Scan 3) Resolve all render pass dependencies.
        for (const auto& renderPass : mRenderPasses)
        {
            // Now that all input resources are allocated, each render pass can build their srv heap.
            renderPass->CreateInputResourceDescriptorTable(mResourceCache);

            for (const auto& inputID : renderPass->GetInputIDs())
            {
                // Skip inputs that are not provided from other render passes.
                if (!renderPassTaskMap.contains(inputID))
                    continue;

                // Skip self-referential inputs.
                if (inputID == renderPass->GetOutputID())
                    continue;

                renderPassTaskMap[inputID].precede(renderPassTaskMap[renderPass->GetOutputID()]);
            }
        }

        return true;
    }

    bool RenderInputShaderToy::CompileShaderToy(const std::string& shaderID)
    {
        // 1) Download the shader toy shader.
        // -----------------------------------

        auto data = QueryURL<std::string>("https://www.shadertoy.com/api/v1/shaders/" + shaderID.substr(0, 6) + "?key=BtrjRM");

        // 2) Parse result into JSON.
        // --------------------------

        nlohmann::json parsedData = nlohmann::json::parse(data);

#ifdef _DEBUG
        // Keep the result in debug builds for optional viewing.
        mShaderAPIRequestResult = parsedData;
#endif

        // 3) Compile GLSL to SPIR-V.
        // ---------------------------

        if (parsedData.contains("Error"))
        {
            spdlog::info("Requested shader is not publically available via API.");
            return false;
        }

        // Build task-graph.
        if (!BuildRenderGraph(parsedData))
            return false;

        return true;
    }

    void RenderInputShaderToy::ResizeViewportTargets(const DirectX::XMINT2& dim)
    {
        // Re-set internal frame counter.
        gInternalFrameIndex = 0;

        for (auto& renderPass : mRenderPasses)
        {
            // 1) Resize the output resources.
            renderPass->CreateOutputTargets();

            // 2) Update the resource cache.
            mResourceCache[renderPass->GetOutputID()][0] = renderPass->GetOutputResources()[0];
            mResourceCache[renderPass->GetOutputID()][1] = renderPass->GetOutputResources()[1];
        }

        for (auto& renderPass : mRenderPasses)
        {
            // 3) Re-create the input resource descriptor table.
            renderPass->CreateInputResourceDescriptorTable(mResourceCache);
        }
    }

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
                        mUserRequestUnload = false;
                        Release();
                        Initialize();
                    });
            }

            if (ImGui::Button("Unload", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
            {
                gPreRenderTaskQueue.push(
                    [&]()
                    {
                        mUserRequestUnload = true;
                        Release();
                        Initialize();
                    });
            }

#ifdef _DEBUG
            if (!mShaderAPIRequestResult.empty())
            {
                if (ImGui::Button("Log API Request Result", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                    spdlog::info(mShaderAPIRequestResult.dump(4));
            }
#endif

            do
            {
                if (mAsyncCompileStatus.load() != AsyncCompileShaderToyStatus::Compiled)
                    break;

                if (ImGui::Button("Write HLSL to Disk", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                {
                    for (const auto& renderPass : mRenderPasses)
                    {
                        if (renderPass->GetSPIRV().empty())
                            continue;

                        spirv_cross::CompilerHLSL compiler(renderPass->GetSPIRV());

                        spirv_cross::CompilerHLSL::Options compileOptions;
                        {
                            // Request SM 6.0 compliant.
                            compileOptions.shader_model = 60;
                        }
                        compiler.set_hlsl_options(compileOptions);

                        // Decompile the render pass SPIR-V to HLSL.
                        auto hlsl = compiler.compile();

                        // Write the HLSL code to a file.
                        std::ofstream file(std::format("{}-renderpass-{}.hlsl", mShaderID.substr(0, 6), renderPass->GetOutputID()));

                        if (!file.is_open())
                            break;

                        file << hlsl;
                        file.close();
                    }
                }
            }
            while (false);
        }

        ImGui::EndChild();
    }

    void RenderInputShaderToy::Render(const FrameParams& frameParams)
    {
        if (!mInitialized)
            return;

        // Check Shadertoy compile status.
        switch (mAsyncCompileStatus.load())
        {
            case AsyncCompileShaderToyStatus::Compiling:
            case AsyncCompileShaderToyStatus::Failed:
            case AsyncCompileShaderToyStatus::Idle     : return;
            default                                    : break;
        };

        Constants constants = {};
        {
            constants.iResolution.x = gViewport.Width;
            constants.iResolution.y = gViewport.Height;
            constants.iTime         = elapsedSeconds;
            constants.iFrame        = elapsedFrames;
            constants.iTimeDelta    = gDeltaTime;
            constants.iFrameRate    = 1.0f / gDeltaTime;

            POINT mousePos;
            if (GetCursorPos(&mousePos))
            {
                RECT windowRect;
                GetWindowRect(gWindowNative, &windowRect);

                int clientMouseX = mousePos.x - windowRect.left;
                int clientMouseY = mousePos.y - windowRect.top;

                constants.iMouse.x = clientMouseX - gViewport.TopLeftX;
                constants.iMouse.y = clientMouseY - gViewport.TopLeftY;

                // Flip
                constants.iMouse.y = gViewport.Height - constants.iMouse.y;
            }

            constants.iMouse.z = ImGui::IsAnyMouseDown();
            constants.iMouse.w = ImGui::IsAnyMouseDown();
        }
        memcpy(mpUBOData, &constants, sizeof(Constants));

        D3D12_RECT scissor = {};
        {
            scissor.left   = static_cast<LONG>(0);
            scissor.top    = static_cast<LONG>(0);
            scissor.right  = static_cast<LONG>(gBackBufferSize.x);
            scissor.bottom = static_cast<LONG>(gBackBufferSize.y);
        }

        D3D12_VIEWPORT viewport = gViewport;
        {
            viewport.TopLeftX = 0.0f;
            viewport.TopLeftY = 0.0f;
        }
        frameParams.pCmd->RSSetViewports(1U, &viewport);
        frameParams.pCmd->RSSetScissorRects(1U, &scissor);

        gResourceRegistry->BindDescriptorHeaps(frameParams.pCmd, DescriptorHeap::Type::Constants);

        // Root signature is the same for all render passes, so set it once.
        frameParams.pCmd->SetGraphicsRootSignature(mRootSignature.Get());
        frameParams.pCmd->SetGraphicsRootConstantBufferView(0u, gResourceRegistry->Get(mUBO)->GetGPUVirtualAddress());
        frameParams.pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Configure the active command lists.
        mpActiveCommandList = frameParams.pCmd;

        tf::Executor renderGraphExecutor;

        // Dispatch all of the render passes in order.
        renderGraphExecutor.run(mRenderGraph).wait();

        elapsedSeconds += gDeltaTime;
        elapsedFrames += 1;

        // Blit final output into swapchain backbuffer.
        {
            auto pFinalPassOutputFrame = gResourceRegistry->Get(mpFinalRenderPass->GetOutputResources()[GetCurrentFrameIndex()]);

            D3D12_RESOURCE_BARRIER transitionBarriers[1];

            transitionBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(pFinalPassOutputFrame,
                                                                         D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            gCommandList->ResourceBarrier(1, transitionBarriers);

            Blitter::Params blitParams = {};
            {
                blitParams.pCmd                       = gCommandList.Get();
                blitParams.bindlessDescriptorSrcIndex = mpFinalRenderPass->GetOutputResources()[GetCurrentFrameIndex()].indexDescriptorTexture2D;
                blitParams.renderTargetDst            = frameParams.currentSwapChainBufferRTV;
                blitParams.viewport                   = gViewport;
            }
            gBlitter->Dispatch(blitParams);

            transitionBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(pFinalPassOutputFrame,
                                                                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                         D3D12_RESOURCE_STATE_RENDER_TARGET);

            gCommandList->ResourceBarrier(1, transitionBarriers);
        }

        // Increment the internal frame index.
        gInternalFrameIndex++;
    }

    void RenderInputShaderToy::Release()
    {
        if (!mInitialized)
            return;

        for (auto& handle : mMediaResources)
            gResourceRegistry->Release(handle);

        mShaderAPIRequestResult.clear();
        mRenderGraph.clear();
        mRenderPasses.clear();
        mCommonShaderGLSL.clear();

        gResourceRegistry->Get(mUBO)->Unmap(0, nullptr);
        gResourceRegistry->Release(mUBO);

        mRootSignature.Reset();
        mPSO.Reset();

        mInitialized = false;
    }

} // namespace ICR
