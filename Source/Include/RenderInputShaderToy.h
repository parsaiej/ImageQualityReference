#ifndef RENDER_INPUT_SHADER_TOY_H
#define RENDER_INPUT_SHADER_TOY_H

#include <RenderInput.h>

namespace ICR
{

    class RenderInputShaderToy : public RenderInput
    {
    public:

        class MediaCache
        {
        };

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

            void Dispatch();

            inline const int&              GetOutputID() const { return mOutputID; }
            inline const std::vector<int>& GetInputIDs() const { return mInputIDs; }
            inline ID3D12Resource*         GetOutputResource() const { return mOutputResource.Get(); }

        private:

            int                         mOutputID;
            std::vector<int>            mInputIDs;
            ComPtr<ID3D12PipelineState> mPSO;
            ComPtr<ID3D12Resource>      mOutputResource;
        };

        enum AsyncCompileShaderToyStatus
        {
            Idle,
            Compiling,
            Compiled,
            Failed
        };

        struct Constants
        {
            DirectX::XMFLOAT4 iAppViewport;

            DirectX::XMFLOAT3 iResolution;
            float             iTime;
            float             iTimeDelta;
            float             iFrameRate;
            int               iFrame;
            float             iChannelTime[4];
            DirectX::XMFLOAT3 iChannelResolution[4];
            DirectX::XMFLOAT4 iMouse;
            DirectX::XMFLOAT4 iDate;
            float             iSampleRate;

            // Pad-up to 256 bytes.
            float padding[28];
        };

        RenderInputShaderToy();
        ~RenderInputShaderToy();

        void Initialize() override;
        void Render(const FrameParams& frameParams) override;
        void RenderInterface() override;
        void Release() override;

    private:

        bool CompileShaderToy(const std::string& shaderID);
        bool BuildRenderGraph(const nlohmann::json& parsedShaderToy);

        tf::Taskflow                             mRenderGraph;
        std::string                              mCommonShaderGLSL;
        std::vector<std::unique_ptr<RenderPass>> mRenderPasses;
        std::string                              mShaderID;
        MediaCache                               mMediaCache;
        bool                                     mInitialized;
        ComPtr<ID3D12PipelineState>              mPSO;
        ComPtr<ID3D12Resource>                   mUBO;
        ComPtr<D3D12MA::Allocation>              mUBOAllocation;
        ComPtr<ID3D12DescriptorHeap>             mUBOHeap;
        void*                                    mpUBOData;
        ComPtr<ID3D12RootSignature>              mRootSignature;
        std::atomic<AsyncCompileShaderToyStatus> mAsyncCompileStatus;
        bool                                     mUserRequestUnload;
    };
} // namespace ICR

#endif
