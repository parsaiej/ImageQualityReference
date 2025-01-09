#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include "NRI/NRIDescs.h"
namespace ImageQualityReference
{
    typedef uint64_t ResourceHandle;
    typedef uint32_t ResourceViewBits;

    enum ResourceView
    {
        Texture2D    = 1 << 0,
        RenderTarget = 1 << 1
    };

    struct Resource
    {
        nri::Buffer*  pBuffer  = nullptr;
        nri::Texture* pTexture = nullptr;

        // Map of all views for the resource.
        std::map<ResourceView, nri::Descriptor*> views;
    };

    class ResourceManager
    {
    public:

        ResourceManager();

        ResourceHandle Create(const nri::TextureDesc& textureInfo);
        ResourceHandle Create(const nri::BufferDesc& bufferInfo);

        // Create texture with pre-existing resource.
        ResourceHandle Create(nri::Texture* pTexture, const ResourceViewBits& viewBits);

        void Release(const ResourceHandle& handle);

        inline Resource* Get(const ResourceHandle& handle)
        {
            if (handle == UINT_MAX)
                throw std::runtime_error("Attempted to retrieve resource with an invalid handle.");

            return &mResources[handle];
        }

    private:

        uint32_t Allocate();

        std::vector<Resource> mResources;
        uint32_t              mMaxResources;
        std::queue<uint32_t>  mFreeIndices;
    };
} // namespace ImageQualityReference

#endif
