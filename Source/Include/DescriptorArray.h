#ifndef DESCRIPTOR_ARRAY_H
#define DESCRIPTOR_ARRAY_H

namespace ImageQualityReference
{
    class DescriptorArray
    {
    public:

        enum Type
        {
            Texture2D    = 1 << 0,
            RenderTarget = 1 << 1
        };

        DescriptorArray(Type);

    private:

        nri::DescriptorPool* mpDescriptorPool;
        nri::DescriptorSet*  mpDescriptorSet;

        Type            mType;
        uint32_t        mMaxArraySize;
        std::queue<int> mFreeIndices;
    };
} // namespace ImageQualityReference

#endif
