#include <Blitter.h>
#include <State.h>

namespace ICR
{
    Blitter::Blitter()
    {
        assert(gLogicalDevice != nullptr);
        assert(!gShaderDXIL.empty());

        // Root Signature
        // -----------------------------

        D3D12_DESCRIPTOR_RANGE1 inputSRVRanges = {};
        {
            inputSRVRanges.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            inputSRVRanges.NumDescriptors     = 1;
            inputSRVRanges.BaseShaderRegister = 0;
            inputSRVRanges.RegisterSpace      = 0;
        }

        std::array<CD3DX12_ROOT_PARAMETER1, 1> rootParameters;
        {
            rootParameters[0].InitAsDescriptorTable(1, &inputSRVRanges, D3D12_SHADER_VISIBILITY_PIXEL);
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

        ThrowIfFailed(gLogicalDevice->CreateRootSignature(0u,
                                                          pBlobSignature->GetBufferPointer(),
                                                          pBlobSignature->GetBufferSize(),
                                                          IID_PPV_ARGS(&mRootSignature)));

        // PSO
        // -----------------------------

        D3D12_GRAPHICS_PIPELINE_STATE_DESC blitPSOInfo = {};
        {
            auto& fullscreenTriangleDXIL = gShaderDXIL["FullscreenTriangle.vert"];
            auto& blitDXIL               = gShaderDXIL["Blit.frag"];

            blitPSOInfo.pRootSignature                  = mRootSignature.Get();
            blitPSOInfo.VS                              = { fullscreenTriangleDXIL->GetBufferPointer(), fullscreenTriangleDXIL->GetBufferSize() };
            blitPSOInfo.PS                              = { blitDXIL->GetBufferPointer(), blitDXIL->GetBufferSize() };
            blitPSOInfo.BlendState                      = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            blitPSOInfo.SampleMask                      = UINT_MAX;
            blitPSOInfo.RasterizerState                 = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            blitPSOInfo.DepthStencilState.DepthEnable   = FALSE;
            blitPSOInfo.DepthStencilState.StencilEnable = FALSE;
            blitPSOInfo.InputLayout                     = { nullptr, 0 };
            blitPSOInfo.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            blitPSOInfo.NumRenderTargets                = 1;
            blitPSOInfo.RTVFormats[0]                   = DXGI_FORMAT_R8G8B8A8_UNORM;
            blitPSOInfo.SampleDesc.Count                = 1;
        }
        ThrowIfFailed(gLogicalDevice->CreateGraphicsPipelineState(&blitPSOInfo, IID_PPV_ARGS(&mPSO)));

        // Descriptors
        // -----------------------------

        D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
        {
            descriptorHeapDesc.NumDescriptors = 1;
            descriptorHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            descriptorHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        }
        ThrowIfFailed(gLogicalDevice->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&mDescriptorHeap)));
    }

    void Blitter::Dispatch(ID3D12GraphicsCommandList* pCmd)
    {
        pCmd->SetGraphicsRootSignature(mRootSignature.Get());
        pCmd->SetDescriptorHeaps(1u, &mDescriptorHeap);
        pCmd->SetGraphicsRootDescriptorTable(0u, mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        pCmd->SetPipelineState(mPSO.Get());
        pCmd->DrawInstanced(3u, 1u, 0u, 0u);
    }
} // namespace ICR
