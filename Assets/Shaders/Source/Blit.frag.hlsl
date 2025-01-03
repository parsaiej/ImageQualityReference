#include "RegisterSpaces.h"

struct Interpolators
{
	float4 position : SV_Position;
    float2 coord    : TEXCOORD0;
};

cbuffer RootConstants : register(b0, space0)
{
    uint bindlessDescriptorSrcIndex;
};

Texture2D _Texture2DTable[] : register(t0, space3);

float4 Main(Interpolators i) : SV_Target0
{
    return _Texture2DTable[bindlessDescriptorSrcIndex].Load(int3(int2(960, 540) * i.coord, 0));
}