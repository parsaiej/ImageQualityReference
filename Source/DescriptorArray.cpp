#include <DescriptorArray.h>
#include <State.h>
#include <Common.h>

namespace ImageQualityReference
{
    DescriptorArray::DescriptorArray(Type type) : mType(type), mMaxArraySize(1024u)
    {
        for (uint32_t i = 0; i < mMaxArraySize; ++i)
            mFreeIndices.push(i);

        nri::DescriptorPoolDesc descriptorPoolInfo = {};
        {
            descriptorPoolInfo.descriptorSetMaxNum = 1u;

            switch (mType)
            {
                case Type::Texture2D   : descriptorPoolInfo.storageTextureMaxNum = mMaxArraySize;
                case Type::RenderTarget: descriptorPoolInfo.textureMaxNum = mMaxArraySize;
                default                : break;
            }
        }
        NRI_ABORT_ON_FAILURE(gNRI.CreateDescriptorPool(*gDevice, descriptorPoolInfo, mpDescriptorPool));

        nri::PipelineLayout* pDummyPipelineLayout = nullptr;

        int descriptorSetBindingIndex;

        switch (mType)
        {
            default: break;
        }

        NRI_ABORT_ON_FAILURE(gNRI.AllocateDescriptorSets(*mpDescriptorPool, *pDummyPipelineLayout, 0, &mpDescriptorSet, 0, 0));
    }
} // namespace ImageQualityReference
