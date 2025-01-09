#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

namespace ImageQualityReference
{
    struct ResourceHandle
    {
        uint32_t indexResource               = UINT_MAX;
        uint32_t indexDescriptorTexture2D    = UINT_MAX;
        uint32_t indexDescriptorRenderTarget = UINT_MAX;
    };

    struct Resource
    {
        nri::Buffer*  pBuffer  = nullptr;
        nri::Texture* pTexture = nullptr;
    };

    class ResourceManager
    {
    public:

        ResourceManager();

        ResourceHandle Create(const nri::TextureDesc& textureInfo);
        ResourceHandle Create(const nri::BufferDesc& bufferInfo);

        // Create texture with pre-existing resource.
        ResourceHandle Create(nri::Texture* pTexture);

        void Release(const ResourceHandle& handle);

        inline Resource* Get(const ResourceHandle& handle)
        {
            if (handle.indexResource == UINT_MAX)
                throw std::runtime_error("Attempted to retrieve resource with an invalid handle.");

            return &mResources[handle.indexResource];
        }

    private:

        uint32_t Allocate();

        std::vector<Resource> mResources;
        uint32_t              mMaxResources;
        std::queue<uint32_t>  mFreeIndices;
    };
} // namespace ImageQualityReference

#endif
