#ifndef RENDER_INPUT_SHADER_TOY_H
#define RENDER_INPUT_SHADER_TOY_H

#include <Util.h>
#include <RenderInput.h>
#include <ResourceRegistry.h>

namespace ICR
{
    class RenderInputShaderToy : public RenderInput
    {
    public:

        class RenderPass
        {
        public:

            struct Args
            {
                ID3D12RootSignature*  pRootSignature;
                const nlohmann::json& renderPassInfo;
                const std::string&    commonShaderGLSL;
            };

            RenderPass(const Args& args);

            void CreateInputResourceDescriptorTable(const std::unordered_map<int, std::array<ResourceHandle, 2>>& resourceCache);

            void CreateOutputTargets();

            void Dispatch(ID3D12GraphicsCommandList* pCmd);

            inline const int&                          GetOutputID() const { return mOutputID; }
            inline const std::vector<int>&             GetInputIDs() const { return mInputIDs; }
            inline const std::vector<uint32_t>&        GetSPIRV() const { return mSPIRV; }
            inline const std::array<ResourceHandle, 2> GetOutputResources() const { return mOutputTargets; }

        private:

            int                           mOutputID;
            std::vector<int>              mInputIDs;
            ComPtr<ID3D12PipelineState>   mPSO;
            ComPtr<ID3D12DescriptorHeap>  mInputSamplerDescriptorHeap;
            ComPtr<ID3D12DescriptorHeap>  mInputResourceDescriptorHeap;
            std::vector<uint32_t>         mSPIRV;
            std::array<ResourceHandle, 2> mOutputTargets;
            std::unordered_map<int, int>  mInputToChannelMap;
            bool                          mIntermediateRenderPass;
        };

        enum AsyncCompileShaderToyStatus
        {
            Idle,
            Compiling,
            Compiled,
            Failed
        };

        struct alignas(16) Constants
        {
            DirectX::XMFLOAT4 iAppParams0;

            DirectX::XMFLOAT3 iResolution;
            float             _padding0;

            float iTime;
            float iTimeDelta;
            float iFrameRate;
            int   iFrame;

            DirectX::XMFLOAT4 iChannelTime;

            DirectX::XMFLOAT4 iChannelResolution[4];

            DirectX::XMFLOAT4 iMouse;

            DirectX::XMFLOAT4 iDate;

            DirectX::XMFLOAT3 _padding1;
            float             iSampleRate;

            // Pad-up to 256 bytes.
            DirectX::XMFLOAT4 padding[5];
        };

        RenderInputShaderToy();
        ~RenderInputShaderToy();

        void Initialize() override;
        void ResizeViewportTargets(const DirectX::XMINT2& dim) override;
        void Render(const FrameParams& frameParams) override;
        void RenderInterface() override;
        void Release() override;

    private:

        bool CompileShaderToy(const std::string& shaderID);
        bool BuildRenderGraph(const nlohmann::json& parsedShaderToy);

#ifdef _DEBUG
        nlohmann::json mShaderAPIRequestResult;
#endif

        ResourceHandle mUBO;
        void*          mpUBOData;

        tf::Taskflow                                           mRenderGraph;
        std::string                                            mCommonShaderGLSL;
        ID3D12GraphicsCommandList*                             mpActiveCommandList;
        std::vector<std::unique_ptr<RenderPass>>               mRenderPasses;
        RenderPass*                                            mpFinalRenderPass;
        std::string                                            mShaderID;
        bool                                                   mInitialized;
        ComPtr<ID3D12PipelineState>                            mPSO;
        ComPtr<ID3D12RootSignature>                            mRootSignature;
        std::atomic<AsyncCompileShaderToyStatus>               mAsyncCompileStatus;
        bool                                                   mUserRequestUnload;
        std::unordered_map<int, std::array<ResourceHandle, 2>> mResourceCache;
        std::vector<ResourceHandle>                            mMediaResources;
    };
} // namespace ICR

#endif
