#ifndef BLITTER_H
#define BLITTER_H

namespace ICR
{
    class Blitter
    {
    public:

        struct Params
        {
            ID3D12GraphicsCommandList*  pCmd;
            uint32_t                    bindlessDescriptorSrcIndex;
            D3D12_CPU_DESCRIPTOR_HANDLE renderTargetDst;
            D3D12_VIEWPORT              viewport;
        };

        Blitter();

        void Dispatch(const Params& params);

    private:

        ComPtr<ID3D12PipelineState> mPSO;
        ComPtr<ID3D12RootSignature> mRootSignature;
    };
} // namespace ICR

#endif
