#include <Blitter.h>
#include <State.h>
#include <ResourceRegistry.h>

namespace ICR
{
    Blitter::Blitter()
    {
        assert(gLogicalDevice != nullptr);
        assert(!gShaderDXIL.empty());

        // Root Signature
        // -----------------------------

        std::array<CD3DX12_ROOT_PARAMETER1, 2> rootParameters;
        {
            // Bindless texture source index.
            rootParameters[0].InitAsConstants(1, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

            // Obtain the bindless descriptor table for texture 2D.
            rootParameters[1] = *gResourceRegistry->GetDescriptorHeap(DescriptorHeap::Type::Texture2D)->GetRootParameter();
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
    }

    void Blitter::Dispatch(const Params& params)
    {
        auto* pCmd = params.pCmd;

        gResourceRegistry->BindDescriptorHeaps(pCmd, DescriptorHeap::Type::Texture2D);

        pCmd->OMSetRenderTargets(1u, &params.renderTargetDst, true, nullptr);
        pCmd->SetGraphicsRootSignature(mRootSignature.Get());
        pCmd->SetGraphicsRoot32BitConstant(0u, params.bindlessDescriptorSrcIndex, 0u);
        pCmd->SetGraphicsRootDescriptorTable(1u, gResourceRegistry->GetDescriptorHeap(DescriptorHeap::Type::Texture2D)->GetBaseAddressGPU());
        pCmd->RSSetViewports(1u, &params.viewport);
        pCmd->SetPipelineState(mPSO.Get());
        pCmd->DrawInstanced(3u, 1u, 0u, 0u);
    }
} // namespace ICR
