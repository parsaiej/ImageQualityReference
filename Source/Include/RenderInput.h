#ifndef RENDER_INPUT_H
#define RENDER_INPUT_H

namespace ICR
{
    struct FrameParams
    {
        ID3D12GraphicsCommandList* pCmd;
    };

    enum RenderInputMode
    {
        // Allow users to render ShaderToys locally.
        ShaderToy,

        // Allow users to render USD files (via Hydra delegate).
        OpenUSD
    };

    class RenderInput
    {
    public:

        virtual void Initialize()                                      = 0;
        virtual void ResizeViewportTargets(const DirectX::XMINT2& dim) = 0;
        virtual void Render(const FrameParams& frameParams)            = 0;
        virtual void RenderInterface()                                 = 0;
        virtual void Release()                                         = 0;
    };

} // namespace ICR

#endif
