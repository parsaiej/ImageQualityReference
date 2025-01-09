
#include <ResourceManager.h>
#include <State.h>
#include <Common.h>

using namespace ImageQualityReference;

namespace ImageQualityReference
{
    ResourceManager::ResourceManager() : mMaxResources(2048u)
    {
        mResources.resize(mMaxResources);

        for (uint32_t i = 0; i < mMaxResources; ++i)
            mFreeIndices.push(i);
    }

    ResourceHandle ResourceManager::Create(const nri::TextureDesc& textureInfo)
    {
        ResourceHandle handle;
        handle.indexResource = Allocate();

        NRI_ABORT_ON_FAILURE(gNRI.CreateTexture(*gDevice, textureInfo, mResources[handle.indexResource].pTexture));

        nri::AllocateTextureDesc allocationInfo = {};
        {
            allocationInfo.desc           = textureInfo;
            allocationInfo.memoryLocation = nri::MemoryLocation::DEVICE;
        }
        NRI_ABORT_ON_FAILURE(gNRI.AllocateTexture(*gDevice, allocationInfo, mResources[handle.indexResource].pTexture));

        return handle;
    }

    ResourceHandle ResourceManager::Create(const nri::BufferDesc& bufferInfo)
    {
        ResourceHandle handle;
        handle.indexResource = Allocate();

        NRI_ABORT_ON_FAILURE(gNRI.CreateBuffer(*gDevice, bufferInfo, mResources[handle.indexResource].pBuffer));

        nri::AllocateBufferDesc allocationInfo = {};
        {
            allocationInfo.desc           = bufferInfo;
            allocationInfo.memoryLocation = nri::MemoryLocation::DEVICE;
        }
        NRI_ABORT_ON_FAILURE(gNRI.AllocateBuffer(*gDevice, allocationInfo, mResources[handle.indexResource].pBuffer));

        return handle;
    }

    ResourceHandle ResourceManager::Create(nri::Texture* pTexture)
    {
        ResourceHandle handle;
        handle.indexResource = Allocate();

        mResources[handle.indexResource].pTexture = pTexture;

        return handle;
    }

    void ResourceManager::Release(const ResourceHandle& handle)
    {
        if (handle.indexResource == UINT_MAX)
            throw std::runtime_error("Attempted to release resource with an invalid handle.");

        auto* pResource = Get(handle);

        if (pResource->pBuffer)
            gNRI.DestroyBuffer(*pResource->pBuffer);

        if (pResource->pTexture)
            gNRI.DestroyTexture(*pResource->pTexture);

        mFreeIndices.push(handle.indexResource);
    }

    uint32_t ResourceManager::Allocate()
    {
        if (mFreeIndices.empty())
            throw std::runtime_error("Image Quality Reference: Ran out of GPU allocations.");

        uint32_t resourceAllocationIndex = mFreeIndices.front();

        mFreeIndices.pop();

        return resourceAllocationIndex;
    }

} // namespace ImageQualityReference