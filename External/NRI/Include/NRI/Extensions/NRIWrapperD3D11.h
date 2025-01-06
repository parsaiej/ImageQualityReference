// © 2021 NVIDIA Corporation

#pragma once

#include "NRIDeviceCreation.h" // CallbackInterface, AllocationCallbacks

NonNriForwardStruct(AGSContext);
NonNriForwardStruct(ID3D11Device);
NonNriForwardStruct(ID3D11Resource);
NonNriForwardStruct(ID3D11DeviceContext);

NriNamespaceBegin

NriStruct(DeviceCreationD3D11Desc) {
    ID3D11Device* d3d11Device;
    NriOptional AGSContext* agsContext;
    Nri(CallbackInterface) callbackInterface;
    Nri(AllocationCallbacks) allocationCallbacks;
    bool isNVAPILoaded; // at least NVAPI requires calling "NvAPI_Initialize" in DLL/EXE where the device is created in addition to NRI

    // Switches (disabled by default)
    bool enableNRIValidation;
    bool enableD3D11CommandBufferEmulation; // enable? but why? (auto-enabled if deferred contexts are not supported)
};

NriStruct(CommandBufferD3D11Desc) {
    ID3D11DeviceContext* d3d11DeviceContext;
};

NriStruct(BufferD3D11Desc) {
    ID3D11Resource* d3d11Resource;
    NriOptional const NriPtr(BufferDesc) desc; // not all information can be retrieved from the resource if not provided
};

NriStruct(TextureD3D11Desc) {
    ID3D11Resource* d3d11Resource;
    NriOptional const NriPtr(TextureDesc) desc; // not all information can be retrieved from the resource if not provided
};

NriStruct(WrapperD3D11Interface) {
    Nri(Result) (NRI_CALL *CreateCommandBufferD3D11)    (NriRef(Device) device, const NriRef(CommandBufferD3D11Desc) commandBufferD3D11Desc, NriOut NriRef(CommandBuffer*) commandBuffer);
    Nri(Result) (NRI_CALL *CreateBufferD3D11)           (NriRef(Device) device, const NriRef(BufferD3D11Desc) bufferD3D11Desc, NriOut NriRef(Buffer*) buffer);
    Nri(Result) (NRI_CALL *CreateTextureD3D11)          (NriRef(Device) device, const NriRef(TextureD3D11Desc) textureD3D11Desc, NriOut NriRef(Texture*) texture);
};

NRI_API Nri(Result) NRI_CALL nriCreateDeviceFromD3D11Device(const NriRef(DeviceCreationD3D11Desc) deviceDesc, NriOut NriRef(Device*) device);

NriNamespaceEnd
