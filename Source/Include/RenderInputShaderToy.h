#ifndef RENDER_INPUT_SHADER_TOY_H
#define RENDER_INPUT_SHADER_TOY_H

#include <RenderInput.h>

namespace ICR
{

    class RenderInputShaderToy : public RenderInput
    {
    public:

        enum AsyncCompileShaderToyStatus
        {
            Idle,
            Compiling,
            Compiled,
            Failed
        };

        struct Buffer
        {
            ComPtr<ID3D12Resource>      resource;
            ComPtr<D3D12MA::Allocation> allocation;
        };

        struct RenderPass
        {
            int                         output;
            std::array<int, 4>          inputs;
            ComPtr<ID3D12PipelineState> pso;
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

        std::string                              mShaderID;
        bool                                     mInitialized = false;
        ComPtr<ID3D12PipelineState>              mPSO;
        ComPtr<ID3D12Resource>                   mUBO;
        ComPtr<ID3D12DescriptorHeap>             mUBOHeap;
        ComPtr<D3D12MA::Allocation>              mUBOAllocation;
        void*                                    mpUBOData;
        ComPtr<ID3D12RootSignature>              mRootSignature;
        std::atomic<AsyncCompileShaderToyStatus> mAsyncCompileStatus;
    };
} // namespace ICR

#endif
