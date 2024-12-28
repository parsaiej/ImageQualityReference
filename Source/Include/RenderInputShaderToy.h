#ifndef RENDER_INPUT_SHADER_TOY_H
#define RENDER_INPUT_SHADER_TOY_H

#include <Util.h>
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

            void CreateInputResourceDescriptorTable(const std::unordered_map<int, ID3D12Resource*>& resourceCache);

            void Dispatch(ID3D12GraphicsCommandList* pCmd);

            inline const int&                   GetOutputID() const { return mOutputID; }
            inline const std::vector<int>&      GetInputIDs() const { return mInputIDs; }
            inline const std::vector<uint32_t>& GetSPIRV() const { return mSPIRV; }
            inline ID3D12Resource*              GetOutputResource() const { return mOutputResource.Get(); }

        private:

            int                           mOutputID;
            std::vector<int>              mInputIDs;
            ComPtr<ID3D12PipelineState>   mPSO;
            ComPtr<ID3D12Resource>        mOutputResource;
            ComPtr<ID3D12DescriptorHeap>  mInputSamplerDescriptorHeap;
            ComPtr<ID3D12DescriptorHeap>  mInputResourceDescriptorHeap;
            ComPtr<ID3D12DescriptorHeap>  mOutputResourceDescriptorHeap;
            std::vector<uint32_t>         mSPIRV;
            std::array<DeviceResource, 2> mOutputTargets;
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

        DeviceResource               mUBO;
        ComPtr<ID3D12DescriptorHeap> mUBOHeap;
        void*                        mpUBOData;

        tf::Taskflow                             mRenderGraph;
        std::string                              mCommonShaderGLSL;
        ID3D12GraphicsCommandList*               mpActiveCommandList;
        std::vector<std::unique_ptr<RenderPass>> mRenderPasses;
        std::string                              mShaderID;
        MediaCache                               mMediaCache;
        bool                                     mInitialized;
        ComPtr<ID3D12PipelineState>              mPSO;
        ComPtr<ID3D12RootSignature>              mRootSignature;
        std::atomic<AsyncCompileShaderToyStatus> mAsyncCompileStatus;
        bool                                     mUserRequestUnload;
        std::unordered_map<int, ID3D12Resource*> mResourceCache;
        std::vector<DeviceResource>              mMediaResources;
    };
} // namespace ICR

#endif
