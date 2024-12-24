#ifndef RENDER_INPUT_SHADER_TOY_H
#define RENDER_INPUT_SHADER_TOY_H

#include <RenderInput.h>

namespace ICR
{

    class RenderInputShaderToy : public RenderInput
    {
    public:

        struct Constants
        {
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
            float padding[32];
        };

        RenderInputShaderToy();
        ~RenderInputShaderToy();

        void Initialize() override;
        void Render(const FrameParams& frameParams) override;
        void RenderInterface() override;
        void Release() override;

    private:

        std::string mURL;

        ComPtr<ID3D12PipelineState>  mPSO;
        ComPtr<D3D12MA::Allocation>  mUBOAllocation;
        ComPtr<ID3D12DescriptorHeap> mUBOHeap;
        ComPtr<ID3D12Resource>       mUBO;
        void*                        mpUBOData;
        ComPtr<ID3D12RootSignature>  mRootSignature;
    };
} // namespace ICR

#endif
