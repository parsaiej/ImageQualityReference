// © 2021 NVIDIA Corporation

#pragma once

NriNamespaceBegin

NriStruct(VideoMemoryInfo) {
    uint64_t budgetSize;    // the OS-provided video memory budget. If "usageSize" > "budgetSize", the application may incur stuttering or performance penalties
    uint64_t usageSize;     // specifies the application’s current video memory usage
};

NriStruct(TextureSubresourceUploadDesc) {
    const void* slices;
    uint32_t sliceNum;
    uint32_t rowPitch;
    uint32_t slicePitch;
};

NriStruct(TextureUploadDesc) {
    NriOptional const NriPtr(TextureSubresourceUploadDesc) subresources; // if provided, must include ALL subresources = layerNum * mipNum
    NriPtr(Texture) texture;
    Nri(AccessLayoutStage) after;
    Nri(PlaneBits) planes;
};

NriStruct(BufferUploadDesc) {
    const void* data;
    uint64_t dataSize;
    NriPtr(Buffer) buffer;
    uint64_t bufferOffset;
    Nri(AccessStage) after;
};

NriStruct(ResourceGroupDesc) {
    Nri(MemoryLocation) memoryLocation;
    NriPtr(Texture) const* textures;
    uint32_t textureNum;
    NriPtr(Buffer) const* buffers;
    uint32_t bufferNum;
    uint64_t preferredMemorySize; // desired chunk size (but can be greater if a resource doesn't fit), 256 Mb if 0
};

NriStruct(FormatProps) {
    const char* name;            // format name
    Nri(Format) format;          // self
    uint8_t redBits;             // R (or depth) bits
    uint8_t greenBits;           // G (or stencil) bits (0 if channels < 2)
    uint8_t blueBits;            // B bits (0 if channels < 3)
    uint8_t alphaBits;           // A (or shared exponent) bits (0 if channels < 4)
    uint32_t stride         : 6; // block size in bytes
    uint32_t blockWidth     : 4; // 1 for plain formats, >1 for compressed
    uint32_t blockHeight    : 4; // 1 for plain formats, >1 for compressed
    uint32_t isBgr          : 1; // reversed channels (RGBA => BGRA)
    uint32_t isCompressed   : 1; // block-compressed format
    uint32_t isDepth        : 1; // has depth component
    uint32_t isExpShared    : 1; // shared exponent in alpha channel
    uint32_t isFloat        : 1; // floating point
    uint32_t isPacked       : 1; // 16- or 32- bit packed
    uint32_t isInteger      : 1; // integer
    uint32_t isNorm         : 1; // [0; 1] normalized
    uint32_t isSigned       : 1; // signed
    uint32_t isSrgb         : 1; // sRGB
    uint32_t isStencil      : 1; // has stencil component
    uint32_t unused         : 7;
};

NriStruct(HelperInterface) {
    // Optimized memory allocation for a group of resources
    uint32_t    (NRI_CALL *CalculateAllocationNumber)   (const NriRef(Device) device, const NriRef(ResourceGroupDesc) resourceGroupDesc);
    Nri(Result) (NRI_CALL *AllocateAndBindMemory)       (NriRef(Device) device, const NriRef(ResourceGroupDesc) resourceGroupDesc, NriPtr(Memory)* allocations);

    // Populate resources with data (not for streaming!)
    Nri(Result) (NRI_CALL *UploadData)                  (NriRef(CommandQueue) commandQueue, const NriPtr(TextureUploadDesc) textureUploadDescs, uint32_t textureUploadDescNum,
                                                            const NriPtr(BufferUploadDesc) bufferUploadDescs, uint32_t bufferUploadDescNum);

    // WFI
    Nri(Result) (NRI_CALL *WaitForIdle)                 (NriRef(CommandQueue) commandQueue);

    // Information about video memory
    Nri(Result) (NRI_CALL *QueryVideoMemoryInfo)        (const NriRef(Device) device, Nri(MemoryLocation) memoryLocation, NriOut NriRef(VideoMemoryInfo) videoMemoryInfo);
};

// Format utilities
NRI_API Nri(Format) NRI_CALL nriConvertDXGIFormatToNRI(uint32_t dxgiFormat);
NRI_API Nri(Format) NRI_CALL nriConvertVKFormatToNRI(uint32_t vkFormat);
NRI_API uint32_t NRI_CALL nriConvertNRIFormatToDXGI(Nri(Format) format);
NRI_API uint32_t NRI_CALL nriConvertNRIFormatToVK(Nri(Format) format);
NRI_API const NriRef(FormatProps) NRI_CALL nriGetFormatProps(Nri(Format) format);

// Strings
NRI_API const char* NRI_CALL nriGetGraphicsAPIString(Nri(GraphicsAPI) graphicsAPI);

// A convinient way to fit pipeline layout settings into the device limits, respecting various restrictions
NriStruct(PipelineLayoutSettingsDesc) {
    uint32_t descriptorSetNum;
    uint32_t descriptorRangeNum;
    uint32_t rootConstantSize;
    uint32_t rootDescriptorNum;
    bool preferRootDescriptorsOverConstants;
};

static inline Nri(PipelineLayoutSettingsDesc) NriFunc(FitPipelineLayoutSettingsIntoDeviceLimits)(const NriRef(DeviceDesc) deviceDesc, const NriRef(PipelineLayoutSettingsDesc) pipelineLayoutSettingsDesc) {
    uint32_t descriptorSetNum = NriDeref(pipelineLayoutSettingsDesc)->descriptorSetNum;
    uint32_t descriptorRangeNum = NriDeref(pipelineLayoutSettingsDesc)->descriptorRangeNum;
    uint32_t rootConstantSize = NriDeref(pipelineLayoutSettingsDesc)->rootConstantSize;
    uint32_t rootDescriptorNum = NriDeref(pipelineLayoutSettingsDesc)->rootDescriptorNum;

    // Apply global limits
    if (rootConstantSize > NriDeref(deviceDesc)->pipelineLayoutRootConstantMaxSize)
        rootConstantSize = NriDeref(deviceDesc)->pipelineLayoutRootConstantMaxSize;

    if (rootDescriptorNum > NriDeref(deviceDesc)->pipelineLayoutRootDescriptorMaxNum)
        rootDescriptorNum = NriDeref(deviceDesc)->pipelineLayoutRootDescriptorMaxNum;

    uint32_t pipelineLayoutDescriptorSetMaxNum = NriDeref(deviceDesc)->pipelineLayoutDescriptorSetMaxNum;

    // D3D12 has limited-size root signature
    if (NriDeref(deviceDesc)->graphicsAPI == NriScopedMember(GraphicsAPI, D3D12)) {
        const uint32_t descriptorTableCost = 4;
        const uint32_t rootDescriptorCost = 8;

        uint32_t freeBytesInRootSignature = 256;

        // 1 root descriptor can be reserved for "draw parameters" emulation
        if (NriDeref(deviceDesc)->isDrawParametersEmulationEnabled)
            freeBytesInRootSignature -= 8;

        // Must fit
        uint32_t availableDescriptorRangeNum = freeBytesInRootSignature / descriptorTableCost;
        if (descriptorRangeNum > availableDescriptorRangeNum)
            descriptorRangeNum = availableDescriptorRangeNum;

        freeBytesInRootSignature -= descriptorRangeNum * descriptorTableCost;

        // Desired fit
        if (NriDeref(pipelineLayoutSettingsDesc)->preferRootDescriptorsOverConstants) {
            uint32_t availableRootDescriptorNum = freeBytesInRootSignature / rootDescriptorCost;
            if (rootDescriptorNum > availableRootDescriptorNum)
                rootDescriptorNum = availableRootDescriptorNum;

            freeBytesInRootSignature -= rootDescriptorNum * rootDescriptorCost;

            if (rootConstantSize > freeBytesInRootSignature)
                rootConstantSize = freeBytesInRootSignature;
        } else {
            if (rootConstantSize > freeBytesInRootSignature)
                rootConstantSize = freeBytesInRootSignature;

            freeBytesInRootSignature -= rootConstantSize;

            uint32_t availableRootDescriptorNum = freeBytesInRootSignature / rootDescriptorCost;
            if (rootDescriptorNum > availableRootDescriptorNum)
                rootDescriptorNum = availableRootDescriptorNum;
        }
    } else if (rootDescriptorNum)
        pipelineLayoutDescriptorSetMaxNum--;

    if (descriptorSetNum > pipelineLayoutDescriptorSetMaxNum)
        descriptorSetNum = pipelineLayoutDescriptorSetMaxNum;

    Nri(PipelineLayoutSettingsDesc) modifiedPipelineLayoutLimitsDesc = *NriDeref(pipelineLayoutSettingsDesc);
    modifiedPipelineLayoutLimitsDesc.descriptorSetNum = descriptorSetNum;
    modifiedPipelineLayoutLimitsDesc.descriptorRangeNum = descriptorRangeNum;
    modifiedPipelineLayoutLimitsDesc.rootConstantSize = rootConstantSize;
    modifiedPipelineLayoutLimitsDesc.rootDescriptorNum = rootDescriptorNum;

    return modifiedPipelineLayoutLimitsDesc;
}

static inline Nri(TextureBarrierDesc) NriFunc(TextureBarrierFromUnknown)(NriPtr(Texture) texture,
    Nri(AccessLayoutStage) after,
    Nri(Mip_t) mipOffset NriDefault(0),
    Nri(Mip_t) mipNum NriDefault(Nri(REMAINING_MIPS)),
    Nri(Dim_t) layerOffset NriDefault(0),
    Nri(Dim_t) layerNum NriDefault(Nri(REMAINING_LAYERS)))
{
    Nri(TextureBarrierDesc) textureBarrierDesc = NriZero;
    textureBarrierDesc.texture = texture;
    textureBarrierDesc.before.access = NriScopedMember(AccessBits, UNKNOWN);
    textureBarrierDesc.before.layout = NriScopedMember(Layout, UNKNOWN);
    textureBarrierDesc.before.stages = NriScopedMember(StageBits, ALL);
    textureBarrierDesc.after = after;
    textureBarrierDesc.mipOffset = mipOffset;
    textureBarrierDesc.mipNum = mipNum;
    textureBarrierDesc.layerOffset = layerOffset;
    textureBarrierDesc.layerNum = layerNum;

    return textureBarrierDesc;
}

static inline Nri(TextureBarrierDesc) NriFunc(TextureBarrierFromState)(NriRef(TextureBarrierDesc) prevState,
    Nri(AccessLayoutStage) after,
    Nri(Mip_t) mipOffset NriDefault(0),
    Nri(Mip_t) mipNum NriDefault(Nri(REMAINING_MIPS)))
{
    NriDeref(prevState)->mipOffset = mipOffset;
    NriDeref(prevState)->mipNum = mipNum;
    NriDeref(prevState)->before = NriDeref(prevState)->after;
    NriDeref(prevState)->after = after;

    return *NriDeref(prevState);
}

NriNamespaceEnd
