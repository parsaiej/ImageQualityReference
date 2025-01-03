#ifndef RESOURCE_REGISTRY_H
#define RESOURCE_REGISTRY_H

#include <DescriptorHeap.h>

namespace ICR
{
    struct ResourceHandle
    {
        uint32_t indexResource               = UINT_MAX;
        uint32_t indexDescriptorTexture2D    = UINT_MAX;
        uint32_t indexDescriptorRenderTarget = UINT_MAX;
    };

    class ResourceRegistry
    {
    public:

        struct Resource
        {
            ComPtr<ID3D12Resource>      primitive;
            ComPtr<D3D12MA::Allocation> primitiveAlloc;
        };

        ResourceRegistry();

        // Creates a device resource with bound memory and returns a handle.
        ResourceHandle Create(const CD3DX12_RESOURCE_DESC& resourceInfo, DescriptorHeapFlags descriptorHeapFlags);

        // Optional version that can wrap an existing D3D12 resource with a handle + descriptor views.
        ResourceHandle Create(ID3D12Resource* pResource, DescriptorHeapFlags descriptorHeapFlags);

        // Same as above but copies data to the created with staging buffer.
        ResourceHandle CreateWithData(const CD3DX12_RESOURCE_DESC& resourceInfo,
                                      DescriptorHeapFlags          descriptorHeapFlags,
                                      const void*                  data,
                                      size_t                       size);

        // Frees a resource with a provided handle.
        void Release(const ResourceHandle& handle);

        inline uint32_t Allocate()
        {
            if (mFreeIndices.empty())
                throw std::runtime_error("BindlessDescriptorHeap: Maximum number of descriptors reached.");

            uint32_t index = mFreeIndices.front();

            mFreeIndices.pop();

            return index;
        }

        inline void Free(uint32_t index)
        {
            if (index >= mMaxAllocations)
                throw std::runtime_error("BindlessDescriptorHeap: No descriptors to free.");

            mFreeIndices.push(index);
        }

        inline ID3D12Resource* Get(const ResourceHandle& handle) const
        {
            if (handle.indexResource == UINT_MAX)
                throw std::runtime_error("ResourceRegistry: Invalid resource handle.");

            return mResources[handle.indexResource].primitive.Get();
        }

        inline DescriptorHeap* GetDescriptorHeap(DescriptorHeap::Type type) const { return mDescriptorHeaps.at(type).get(); }

    private:

        void AddResourceToDescriptorHeaps(ResourceHandle& handle, DescriptorHeapFlags descriptorHeapFlags);

        uint32_t                                                                  mMaxAllocations;
        ComPtr<D3D12MA::Allocator>                                                mAllocator;
        std::vector<Resource>                                                     mResources;
        std::queue<int>                                                           mFreeIndices;
        std::unordered_map<DescriptorHeap::Type, std::unique_ptr<DescriptorHeap>> mDescriptorHeaps;
    };
} // namespace ICR

#endif
