#ifndef DESCRIPTOR_HEAP
#define DESCRIPTOR_HEAP

namespace ICR
{
    struct ResourceHandle;

    typedef uint32_t DescriptorHeapFlags;

    class DescriptorHeap
    {
    public:

        enum Type
        {
            Texture2D    = 1 << 0,
            RenderTarget = 1 << 1,
            Constants    = 1 << 2,
            RawBuffer    = 1 << 3
        };

        DescriptorHeap(Type);

        void CreateView(ResourceHandle*);
        void ReleaseView(ResourceHandle*);

        void CreateView(ID3D12Resource* pResource);
        void ReleaseView(ID3D12Resource* pResource);

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
            if (index >= mMaxDescriptors)
                throw std::runtime_error("BindlessDescriptorHeap: No descriptors to free.");

            mFreeIndices.push(index);
        }

        inline D3D12_CPU_DESCRIPTOR_HANDLE GetAddressCPU(uint32_t index) const
        {
            if (index >= mMaxDescriptors)
                throw std::invalid_argument("Index out of range.");

            return CD3DX12_CPU_DESCRIPTOR_HANDLE(mBaseAddressCPU, index, mDescriptorSize);
        }

        inline D3D12_GPU_DESCRIPTOR_HANDLE GetAddressGPU(uint32_t index) const
        {
            if (index >= mMaxDescriptors)
                throw std::invalid_argument("Index out of range.");

            return CD3DX12_GPU_DESCRIPTOR_HANDLE(mBaseAddressGPU, index, mDescriptorSize);
        }

        inline D3D12_GPU_DESCRIPTOR_HANDLE GetBaseAddressGPU() const { return mBaseAddressGPU; }

        inline ID3D12DescriptorHeap*          GetHeap() const { return mHeap.Get(); }
        inline const D3D12_DESCRIPTOR_RANGE1& GetTable() const { return mTable; }
        inline const CD3DX12_ROOT_PARAMETER1* GetRootParameter() const { return &mRootParameter; }

    private:

        ComPtr<ID3D12DescriptorHeap> mHeap;
        D3D12_DESCRIPTOR_RANGE1      mTable;
        CD3DX12_ROOT_PARAMETER1      mRootParameter;
        Type                         mType;
        D3D12_CPU_DESCRIPTOR_HANDLE  mBaseAddressCPU;
        D3D12_GPU_DESCRIPTOR_HANDLE  mBaseAddressGPU;
        uint32_t                     mMaxDescriptors;
        uint32_t                     mDescriptorSize;
        std::queue<int>              mFreeIndices;
    };
} // namespace ICR

#endif
