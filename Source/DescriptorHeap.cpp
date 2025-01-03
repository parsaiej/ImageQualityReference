#include <DescriptorHeap.h>
#include <ResourceRegistry.h>
#include <State.h>

// Shared C++ / HLSL file for synchronization of register spaces.
#include "..\\..\\Assets\\Shaders\\Source\\RegisterSpaces.h"

namespace ICR
{
    DescriptorHeap::DescriptorHeap(Type type) : mType(type), mMaxDescriptors(1024u)
    {
        for (uint32_t i = 0; i < mMaxDescriptors; ++i)
            mFreeIndices.push(i);

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors             = mMaxDescriptors;

        switch (mType)
        {
            case Texture2D:
            case Constants:
            case RawBuffer:
            {
                heapDesc.Type   = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                heapDesc.Flags  = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                mDescriptorSize = gLogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                break;
            }

            case RenderTarget:
            {
                heapDesc.Type   = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                heapDesc.Flags  = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                mDescriptorSize = gLogicalDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            }
        }

        ThrowIfFailed(gLogicalDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mHeap)));

        mBaseAddressCPU = mHeap->GetCPUDescriptorHandleForHeapStart();

        // GPU Addressed only necessary for shader-visible descriptors.
        if (mType != Type::RenderTarget)
            mBaseAddressGPU = mHeap->GetGPUDescriptorHandleForHeapStart();

        // Create corresponding descriptor table for root signature creation.
        if (mType != Type::RenderTarget)
        {
            mTable                = {};
            mTable.NumDescriptors = mMaxDescriptors;

            switch (mType)
            {
                case Texture2D:
                {
                    mTable.RangeType     = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    mTable.RegisterSpace = REGISTER_SPACE_TEXTURE2D;
                    break;
                }
                case RenderTarget:
                {
                    // Not needed.
                    break;
                }
            }

            mRootParameter                                     = {};
            mRootParameter.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            mRootParameter.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
            mRootParameter.DescriptorTable.NumDescriptorRanges = 1;
            mRootParameter.DescriptorTable.pDescriptorRanges   = &mTable;
        }
    }

    void DescriptorHeap::CreateView(ResourceHandle* pResourceHandle)
    {
        auto* pResource = gResourceRegistry->Get(*pResourceHandle);

        auto resourceInfo = pResource->GetDesc();

        switch (mType)
        {
            case Texture2D:
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC infoSRV = {};
                infoSRV.Format                          = resourceInfo.Format;
                infoSRV.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
                infoSRV.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                infoSRV.Texture2D.MostDetailedMip       = 0;
                infoSRV.Texture2D.MipLevels             = resourceInfo.MipLevels;

                pResourceHandle->indexDescriptorTexture2D = Allocate();
                gLogicalDevice->CreateShaderResourceView(pResource, &infoSRV, GetAddressCPU(pResourceHandle->indexDescriptorTexture2D));
                break;
            }

            case RenderTarget:
            {
                D3D12_RENDER_TARGET_VIEW_DESC infoRTV = {};
                infoRTV.Format                        = resourceInfo.Format;
                infoRTV.ViewDimension                 = D3D12_RTV_DIMENSION_TEXTURE2D;
                infoRTV.Texture2D.MipSlice            = 0;
                infoRTV.Texture2D.PlaneSlice          = 0;

                pResourceHandle->indexDescriptorRenderTarget = Allocate();
                gLogicalDevice->CreateRenderTargetView(pResource, &infoRTV, GetAddressCPU(pResourceHandle->indexDescriptorRenderTarget));
                break;
            }

            case Constants:
            {
                D3D12_CONSTANT_BUFFER_VIEW_DESC infoCBV = {};
                infoCBV.BufferLocation                  = pResource->GetGPUVirtualAddress();
                infoCBV.SizeInBytes                     = static_cast<UINT>(resourceInfo.Width);

                pResourceHandle->indexDescriptorConstants = Allocate();
                gLogicalDevice->CreateConstantBufferView(&infoCBV, GetAddressCPU(pResourceHandle->indexDescriptorConstants));
                break;
            }

            default: break;
        }
    }
} // namespace ICR
