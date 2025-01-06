// © 2024 NVIDIA Corporation

#ifndef NRI_COMPATIBILITY_HLSLI
#define NRI_COMPATIBILITY_HLSLI

/*
USAGE:

Textures, buffers, samplers and acceleration structures:
    NRI_RESOURCE(Texture2D<float4>, gInput, t, 0, 2);
    NRI_RESOURCE(RWTexture2D<float>, gOutput, u, 0, 0);
    NRI_RESOURCE(StructuredBuffer<InstanceData>, gInstanceData, t, 2, 2);
    NRI_RESOURCE(RaytracingAccelerationStructure, gTlas, t, 1, 2);
    NRI_RESOURCE(SamplerState, gLinearMipmapLinearSampler, s, 0, 0);

Texture and buffer arrays:
    NRI_RESOURCE(Texture2D<float3>, gInputs[], t, 0, 1); // DXIL/SPIRV only
    NRI_RESOURCE(Texture2D<float>, gInputs[8], t, 0, 0); // DXBC compatible

Dual source blending:
    NRI_BLEND_SOURCE(0) out float4 color : SV_Target0,
    NRI_BLEND_SOURCE(1) out float4 blend : SV_Target1

Constants:
    NRI_RESOURCE(cbuffer, Constants, b, 0, 3) {
        uint32_t gConst1;
        uint32_t gConst2;
        uint32_t gConst3;
        uint32_t gConst4;
    };

Push constants:
    struct RootConstants {
        float const1;
        uint32_t const2;
    };

    NRI_ROOT_CONSTANTS(RootConstants, gRootConstants, 7, 0); // a constant buffer in DXBC

Draw parameters:
    - Add to the global scope:
        NRI_ENABLE_DRAW_PARAMETERS;
    - Add to a function input parameters list:
        void main(..., NRI_DECLARE_DRAW_PARAMETERS) {
            ...
        }
    - Use the following macros:
        NRI_VERTEX_ID and NRI_INSTANCE_ID - start from 0
        NRI_BASE_VERTEX and NRI_BASE_INSTANCE - base vertex / instance from a "Draw" call
        NRI_VERTEX_ID_OFFSET and NRI_INSTANCE_ID_OFFSET - start from "base" vertex / instance
    - To fill commands for indirect drawing in a shader use one of "NRI_FILL_X_DESC" macros
    - "NRI_ENABLE_DRAW_PARAMETERS_EMULATION" must be defined prior inclusion of "NRICompatibility.hlsli"
      for pipelines expecting emulation
*/

#ifndef __cplusplus
    #define _NRI_MERGE_TOKENS(a, b) a##b
    #define NRI_MERGE_TOKENS(a, b) _NRI_MERGE_TOKENS(a, b)
#endif

// Container detection
#ifdef __hlsl_dx_compiler
    #ifdef __spirv__
        #define NRI_SPIRV
        #define NRI_PRINTF_AVAILABLE
    #else
        #define NRI_DXIL
    #endif
#else
    #ifndef __cplusplus
        #define NRI_DXBC
    #endif
#endif

// Shader model
#ifdef NRI_DXBC
    #define NRI_SHADER_MODEL 50
#else
    #define NRI_SHADER_MODEL (__SHADER_TARGET_MAJOR * 10 + __SHADER_TARGET_MINOR)
#endif

// Indirect commands filling // TODO: change to StructuredBuffers?
#ifdef NRI_USE_BYTE_ADDRESS_BUFFER
    #define NRI_BUFFER_WRITE(buffer, offset, index, value) buffer.Store(offset * 4 + index * 4, value)
#else
    #define NRI_BUFFER_WRITE(buffer, offset, index, value) buffer[offset + index] = value
#endif

// "DrawDesc"
#define NRI_FILL_DRAW_DESC(buffer, cmdIndex, vertexNum, instanceNum, baseVertex, baseInstance) \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 4, 0, vertexNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 4, 1, instanceNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 4, 2, baseVertex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 4, 3, baseInstance)

// see "DrawIndexedDesc"
#define NRI_FILL_DRAW_INDEXED_DESC(buffer, cmdIndex, indexNum, instanceNum, baseIndex, baseVertex, baseInstance) \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 0, indexNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 1, instanceNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 2, baseIndex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 3, baseVertex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 4, baseInstance)

// see "DrawBaseDesc"
#define NRI_FILL_DRAW_BASE_DESC(buffer, cmdIndex, vertexNum, instanceNum, baseVertex, baseInstance) \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 0, baseVertex); /* root constant */ \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 1, baseInstance); /* root constant */ \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 2, vertexNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 3, instanceNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 4, baseVertex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 5, baseInstance)

// see "DrawIndexedBaseDesc"
#define NRI_FILL_DRAW_INDEXED_BASE_DESC(buffer, cmdIndex, indexNum, instanceNum, baseIndex, baseVertex, baseInstance) \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 0, baseVertex); /* root constant */ \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 1, baseInstance); /* root constant */ \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 2, indexNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 3, instanceNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 4, baseIndex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 5, baseVertex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 6, baseInstance)

// Shading rate
#define NRI_SHADING_RATE(xLogSize, yLogSize) ((xLogSize << 2) | yLogSize)

// SPIRV
#ifdef NRI_SPIRV
    #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
        resourceType name : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    #define NRI_ROOT_CONSTANTS(structName, name, bindingIndex, setIndex) \
        [[vk::push_constant]] structName name

    #define NRI_BLEND_SOURCE(source) \
        [[vk::location(0)]] [[vk::index(source)]]

    // Draw parameters (full support, requires SPV_KHR_shader_draw_parameters)
    #define NRI_ENABLE_DRAW_PARAMETERS

    #define NRI_DECLARE_DRAW_PARAMETERS \
        int NRI_VERTEX_ID_OFFSET : SV_VertexID, \
        uint NRI_INSTANCE_ID_OFFSET : SV_InstanceID, \
        [[vk::builtin("BaseVertex")]] int NRI_BASE_VERTEX : _SV_Nothing1, \
        [[vk::builtin("BaseInstance")]] uint NRI_BASE_INSTANCE : _SV_Nothing2

    #define NRI_VERTEX_ID (NRI_VERTEX_ID_OFFSET - NRI_BASE_VERTEX)
    #define NRI_INSTANCE_ID (NRI_INSTANCE_ID_OFFSET - NRI_BASE_INSTANCE)
#endif

// DXIL
#define NRI_BASE_ATTRIBUTES_EMULATION_SPACE 999
#ifdef NRI_DXIL
    #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
        resourceType name : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    #define NRI_ROOT_CONSTANTS(structName, name, bindingIndex, setIndex) \
        ConstantBuffer<structName> name : register(NRI_MERGE_TOKENS(b, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    #define NRI_BLEND_SOURCE(source)

    // Draw parameters
    #if (NRI_SHADER_MODEL < 68)
        #ifdef NRI_ENABLE_DRAW_PARAMETERS_EMULATION
            // Full support (emulation)
            #define NRI_ENABLE_DRAW_PARAMETERS \
                struct _BaseAttributeConstants { \
                    int baseVertex; \
                    uint baseInstance; \
                }; \
                ConstantBuffer<_BaseAttributeConstants> _BaseAttributes : register(b0, NRI_MERGE_TOKENS(space, NRI_BASE_ATTRIBUTES_EMULATION_SPACE))

            #define NRI_DECLARE_DRAW_PARAMETERS \
                uint NRI_VERTEX_ID : SV_VertexID, \
                uint NRI_INSTANCE_ID : SV_InstanceID

            #define NRI_BASE_VERTEX _BaseAttributes.baseVertex
            #define NRI_BASE_INSTANCE _BaseAttributes.baseInstance
            #define NRI_VERTEX_ID_OFFSET (NRI_BASE_VERTEX + NRI_VERTEX_ID)
            #define NRI_INSTANCE_ID_OFFSET (NRI_BASE_INSTANCE + NRI_INSTANCE_ID)

            #undef NRI_FILL_DRAW_DESC
            #define NRI_FILL_DRAW_DESC NRI_FILL_DRAW_BASE_DESC

            #undef NRI_FILL_DRAW_INDEXED_DESC
            #define NRI_FILL_DRAW_INDEXED_DESC NRI_FILL_DRAW_INDEXED_BASE_DESC
        #else
            // Partial support
            #define NRI_ENABLE_DRAW_PARAMETERS

            #define NRI_DECLARE_DRAW_PARAMETERS \
                uint NRI_VERTEX_ID : SV_VertexID, \
                uint NRI_INSTANCE_ID : SV_InstanceID

            #define NRI_BASE_VERTEX NRI_BASE_VERTEX_is_unsupported
            #define NRI_BASE_INSTANCE NRI_BASE_INSTANCE_is_unsupported
            #define NRI_VERTEX_ID_OFFSET NRI_VERTEX_ID_OFFSET_is_unsupported
            #define NRI_INSTANCE_ID_OFFSET NRI_INSTANCE_ID_OFFSET_is_unsupported
        #endif
    #else
        // Full support
        #define NRI_ENABLE_DRAW_PARAMETERS

        #define NRI_DECLARE_DRAW_PARAMETERS \
            uint NRI_VERTEX_ID : SV_VertexID, \
            uint NRI_INSTANCE_ID : SV_InstanceID, \
            int NRI_BASE_VERTEX : SV_StartVertexLocation, \
            uint NRI_BASE_INSTANCE : SV_StartInstanceLocation
    #endif
#endif

// DXBC
#ifdef NRI_DXBC
    #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
        resourceType name : register(NRI_MERGE_TOKENS(regName, bindingIndex))

    #define NRI_ROOT_CONSTANTS(structName, name, bindingIndex, setIndex) \
        cbuffer structName##_##name : register(NRI_MERGE_TOKENS(b, bindingIndex)) { \
            structName name; \
        }

    #define NRI_BLEND_SOURCE(source)

    // Draw parameters (partial support)
    #define NRI_ENABLE_DRAW_PARAMETERS

    #define NRI_DECLARE_DRAW_PARAMETERS \
        uint NRI_VERTEX_ID : SV_VertexID, \
        uint NRI_INSTANCE_ID : SV_InstanceID

    #define NRI_BASE_VERTEX NRI_BASE_VERTEX_is_unsupported
    #define NRI_BASE_INSTANCE NRI_BASE_INSTANCE_is_unsupported
    #define NRI_VERTEX_ID_OFFSET NRI_VERTEX_ID_OFFSET_is_unsupported
    #define NRI_INSTANCE_ID_OFFSET NRI_INSTANCE_ID_OFFSET_is_unsupported

    // Missing data types
    #define uint32_t uint
    #define uint32_t2 uint2
    #define uint32_t3 uint3
    #define uint32_t4 uint4

    #define int32_t int
    #define int32_t2 int2
    #define int32_t3 int3
    #define int32_t4 int4

    #define float16_t float
    #define float16_t2 float2
    #define float16_t3 float3
    #define float16_t4 float4
#endif

// C/C++
#ifdef __cplusplus
    #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
        struct name
#endif

// Misc (assumes D3D-style viewport)
#define NRI_UV_TO_CLIP(uv) (uv * float2(2, -2) + float2(-1, 1))
#define NRI_CLIP_TO_UV(clip) (clip * float2(0.5, -0.5) + 0.5)

#endif