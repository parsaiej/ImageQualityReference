#ifndef RENDER_INPUT_SHADER_TOY_H
#define RENDER_INPUT_SHADER_TOY_H

#include <RenderInput.h>

namespace ICR
{
    class RenderInputShaderToy : public RenderInput
    {
    public:

        RenderInputShaderToy();
        ~RenderInputShaderToy();

        void Initialize() override;
        void Render(const FrameParams& frameParams) override;
        void RenderInterface() override;
        void Release() override;

    private:

        std::string mURL;

        ComPtr<ID3D12PipelineState> mPSO;
        ComPtr<ID3D12RootSignature> mRootSignature;
    };
} // namespace ICR

#endif
