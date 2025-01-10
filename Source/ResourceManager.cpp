
#include "NRI/NRIDescs.h"
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

    void ResourceManager::ReleaseAll()
    {
        for (auto& resource : mResources)
        {
            for (auto& resourceViews : resource.views)
                gNRI.DestroyDescriptor(*resourceViews.second);

            // Exit early for resources that were made externally and only had their
            // views created here.
            if (resource.externallyManangedMemory)
                continue;

            if (resource.pBuffer)
                gNRI.DestroyBuffer(*resource.pBuffer);

            if (resource.pTexture)
                gNRI.DestroyTexture(*resource.pTexture);
        }
    }

    void CreateViewsForResource(Resource* pResource, const ResourceViewBits& viewBits)
    {
        if ((viewBits & ResourceView::RenderTarget) != 0)
        {
            auto textureInfo = gNRI.GetTextureDesc(*pResource->pTexture);

            nri::Texture2DViewDesc renderTargetViewInfo = {};
            {
                renderTargetViewInfo.viewType = nri::Texture2DViewType::COLOR_ATTACHMENT;
                renderTargetViewInfo.format   = textureInfo.format;
                renderTargetViewInfo.mipNum   = 1;
                renderTargetViewInfo.layerNum = 1;
                renderTargetViewInfo.texture  = pResource->pTexture;
            }
            NRI_ABORT_ON_FAILURE(gNRI.CreateTexture2DView(renderTargetViewInfo, pResource->views[ResourceView::RenderTarget]));
        }

        if ((viewBits & ResourceView::SampledTexture2D) != 0)
        {
            auto textureInfo = gNRI.GetTextureDesc(*pResource->pTexture);

            nri::Texture2DViewDesc sampledTexture2DViewInfo = {};
            {
                sampledTexture2DViewInfo.viewType = nri::Texture2DViewType::SHADER_RESOURCE_STORAGE_2D;
                sampledTexture2DViewInfo.format   = textureInfo.format;
                sampledTexture2DViewInfo.mipNum   = 1;
                sampledTexture2DViewInfo.layerNum = 1;
                sampledTexture2DViewInfo.texture  = pResource->pTexture;
            }
            NRI_ABORT_ON_FAILURE(gNRI.CreateTexture2DView(sampledTexture2DViewInfo, pResource->views[ResourceView::SampledTexture2D]));
        }
    }

    ResourceHandle ResourceManager::Create(const nri::TextureDesc& textureInfo)
    {
        ResourceHandle handle = Allocate();

        NRI_ABORT_ON_FAILURE(gNRI.CreateTexture(*gDevice, textureInfo, mResources[handle].pTexture));

        nri::AllocateTextureDesc allocationInfo = {};
        {
            allocationInfo.desc           = textureInfo;
            allocationInfo.memoryLocation = nri::MemoryLocation::DEVICE;
        }
        NRI_ABORT_ON_FAILURE(gNRI.AllocateTexture(*gDevice, allocationInfo, mResources[handle].pTexture));

        return handle;
    }

    ResourceHandle ResourceManager::Create(const nri::BufferDesc& bufferInfo)
    {
        ResourceHandle handle = Allocate();

        NRI_ABORT_ON_FAILURE(gNRI.CreateBuffer(*gDevice, bufferInfo, mResources[handle].pBuffer));

        nri::AllocateBufferDesc allocationInfo = {};
        {
            allocationInfo.desc           = bufferInfo;
            allocationInfo.memoryLocation = nri::MemoryLocation::DEVICE;
        }
        NRI_ABORT_ON_FAILURE(gNRI.AllocateBuffer(*gDevice, allocationInfo, mResources[handle].pBuffer));

        return handle;
    }

    ResourceHandle ResourceManager::Create(nri::Texture* pTexture, const ResourceViewBits& viewBits)
    {
        ResourceHandle handle = Allocate();

        mResources[handle].pTexture                 = pTexture;
        mResources[handle].externallyManangedMemory = true;

        CreateViewsForResource(&mResources[handle], viewBits);

        return handle;
    }

    void ResourceManager::Release(const ResourceHandle& handle)
    {
        if (handle == UINT_MAX)
            throw std::runtime_error("Attempted to release resource with an invalid handle.");

        auto* pResource = Get(handle);

        if (pResource->pBuffer)
            gNRI.DestroyBuffer(*pResource->pBuffer);

        if (pResource->pTexture)
            gNRI.DestroyTexture(*pResource->pTexture);

        mFreeIndices.push(handle);
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
