struct Interpolators
{
	float4 position : SV_Position;
};

Texture2D<float4> _InputTexture : register(t0, space0);

float4 Main(Interpolators i) : SV_Target0
{
    return _InputTexture.Load(int3(i.position.xy, 0));
}