#ifndef BLITTER_H
#define BLITTER_H

namespace ICR
{
    class Blitter
    {
    public:

        Blitter();

        void Dispatch(ID3D12GraphicsCommandList* pCmd);

    private:

        ComPtr<ID3D12PipelineState>  mPSO;
        ComPtr<ID3D12RootSignature>  mRootSignature;
        ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
    };
} // namespace ICR

#endif
