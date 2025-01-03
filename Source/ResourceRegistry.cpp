#include <ResourceRegistry.h>
#include <State.h>

namespace ICR
{
    ResourceRegistry::ResourceRegistry() : mMaxAllocations(1024u)
    {
        D3D12MA::ALLOCATOR_DESC memoryAllocatorDesc = {};
        {
            memoryAllocatorDesc.pDevice  = gLogicalDevice.Get();
            memoryAllocatorDesc.pAdapter = gDXGIAdapter.Get();
            memoryAllocatorDesc.Flags    = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED | D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
        }
        ThrowIfFailed(D3D12MA::CreateAllocator(&memoryAllocatorDesc, &mAllocator));

        mResources.resize(mMaxAllocations);

        for (uint32_t i = 0; i < mMaxAllocations; i++)
            mFreeIndices.push(i);

        mDescriptorHeaps[DescriptorHeap::Type::Texture2D]    = std::make_unique<DescriptorHeap>(DescriptorHeap::Type::Texture2D);
        mDescriptorHeaps[DescriptorHeap::Type::RenderTarget] = std::make_unique<DescriptorHeap>(DescriptorHeap::Type::RenderTarget);
        mDescriptorHeaps[DescriptorHeap::Type::Constants]    = std::make_unique<DescriptorHeap>(DescriptorHeap::Type::Constants);
        mDescriptorHeaps[DescriptorHeap::Type::RawBuffer]    = std::make_unique<DescriptorHeap>(DescriptorHeap::Type::RawBuffer);

        mStagingBuffer = Create(CD3DX12_RESOURCE_DESC::Buffer(32 * 1024 * 1024), 0x0, true);
    }

    void ResourceRegistry::AddResourceToDescriptorHeaps(ResourceHandle& handle, DescriptorHeapFlags descriptorHeapFlags)
    {
        if ((descriptorHeapFlags & DescriptorHeap::Type::Texture2D) != 0)
            mDescriptorHeaps[DescriptorHeap::Type::Texture2D]->CreateView(&handle);

        if ((descriptorHeapFlags & DescriptorHeap::Type::RenderTarget) != 0)
            mDescriptorHeaps[DescriptorHeap::Type::RenderTarget]->CreateView(&handle);
    }

    ResourceHandle ResourceRegistry::Create(ID3D12Resource* pResource, DescriptorHeapFlags descriptorHeapFlags)
    {
        ResourceHandle handle;
        handle.indexResource = Allocate();

        mResources[handle.indexResource].primitive.Attach(pResource);
        mResources[handle.indexResource].primitiveAlloc = nullptr; // No need to track allocations for externally created resources.

        AddResourceToDescriptorHeaps(handle, descriptorHeapFlags);

        return handle;
    }

    ResourceHandle ResourceRegistry::Create(const CD3DX12_RESOURCE_DESC& resourceDesc, DescriptorHeapFlags descriptorHeapFlags, bool hostVisible)
    {
        ResourceHandle handle;
        handle.indexResource = Allocate();

        D3D12MA::ALLOCATION_DESC allocationDesc = { D3D12MA::ALLOCATION_FLAG_NONE, D3D12_HEAP_TYPE_DEFAULT };

        if (hostVisible)
            allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
        else
            allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE* pClearValue = nullptr;

        if ((resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
        {
            pClearValue           = new D3D12_CLEAR_VALUE();
            pClearValue->Format   = resourceDesc.Format;
            pClearValue->Color[0] = 0.0f;
            pClearValue->Color[1] = 0.0f;
            pClearValue->Color[2] = 0.0f;
            pClearValue->Color[3] = 1.0f;
        }

        ThrowIfFailed(mAllocator->CreateResource(&allocationDesc,
                                                 &resourceDesc,
                                                 D3D12_RESOURCE_STATE_COMMON,
                                                 pClearValue,
                                                 &mResources[handle.indexResource].primitiveAlloc,
                                                 IID_PPV_ARGS(&mResources[handle.indexResource].primitive)));

        if (pClearValue)
            delete pClearValue;

        AddResourceToDescriptorHeaps(handle, descriptorHeapFlags);

        return handle;
    }

    ResourceHandle ResourceRegistry::CreateWithData(const CD3DX12_RESOURCE_DESC& resourceInfo,
                                                    DescriptorHeapFlags          descriptorHeapFlags,
                                                    const void*                  data,
                                                    size_t                       size)
    {
        ResourceHandle handle = Create(resourceInfo, descriptorHeapFlags);

        void* pMappedData = nullptr;
        {
            D3D12_RANGE range = { 0, 0 };
            ThrowIfFailed(mResources[mStagingBuffer.indexResource].primitive->Map(0, &range, &pMappedData));
        }

        memcpy(pMappedData, data, size);

        mResources[mStagingBuffer.indexResource].primitive->Unmap(0, nullptr);

        ExecuteCommandListAndWait(gLogicalDevice.Get(),
                                  gCommandQueue.Get(),
                                  [&](ID3D12GraphicsCommandList* pCmd)
                                  {
                                      D3D12_SUBRESOURCE_DATA subresourceData = {};
                                      subresourceData.pData                  = pMappedData;
                                      subresourceData.RowPitch               = resourceInfo.Width * 4;
                                      subresourceData.SlicePitch             = subresourceData.RowPitch * resourceInfo.Height;

                                      UpdateSubresources(pCmd,
                                                         mResources[handle.indexResource].primitive.Get(),
                                                         mResources[mStagingBuffer.indexResource].primitive.Get(),
                                                         0,
                                                         0,
                                                         1,
                                                         &subresourceData);
                                  });

        return handle;
    }

    void ResourceRegistry::BindDescriptorHeaps(ID3D12GraphicsCommandList* pCmd, DescriptorHeapFlags descriptorHeapFlags)
    {
        ID3D12DescriptorHeap* pHeaps[4];

        uint32_t heapCount = 0;

        if ((descriptorHeapFlags & DescriptorHeap::Type::Texture2D) != 0)
            pHeaps[heapCount++] = mDescriptorHeaps[DescriptorHeap::Type::Texture2D]->GetHeap();

        if ((descriptorHeapFlags & DescriptorHeap::Type::Constants) != 0)
            pHeaps[heapCount++] = mDescriptorHeaps[DescriptorHeap::Type::Constants]->GetHeap();

        pCmd->SetDescriptorHeaps(heapCount, pHeaps);
    }

    void ResourceRegistry::Release(const ResourceHandle& handle)
    {
        if (handle.indexResource == UINT_MAX)
            throw std::runtime_error("ResourceRegistry: Invalid resource handle.");

        mResources[handle.indexResource].primitive.Reset();
        mResources[handle.indexResource].primitiveAlloc.Reset();

        mFreeIndices.push(handle.indexResource);
    }
} // namespace ICR
