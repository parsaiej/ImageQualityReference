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

    ResourceHandle ResourceRegistry::Create(const CD3DX12_RESOURCE_DESC& resourceDesc, DescriptorHeapFlags descriptorHeapFlags)
    {
        ResourceHandle handle;
        handle.indexResource = Allocate();

        D3D12MA::ALLOCATION_DESC allocationDesc = { D3D12MA::ALLOCATION_FLAG_NONE, D3D12_HEAP_TYPE_DEFAULT };

        ThrowIfFailed(mAllocator->CreateResource(&allocationDesc,
                                                 &resourceDesc,
                                                 D3D12_RESOURCE_STATE_COMMON,
                                                 nullptr,
                                                 &mResources[handle.indexResource].primitiveAlloc,
                                                 IID_PPV_ARGS(&mResources[handle.indexResource].primitive)));

        AddResourceToDescriptorHeaps(handle, descriptorHeapFlags);

        return handle;
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
