struct Interpolators
{
	float4 position : SV_Position;
};

// Ref: https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.core/ShaderLibrary/Common.hlsl#L1553

// Generates a triangle in homogeneous clip space, s.t.
// v0 = (-1, -1, 1), v1 = (3, -1, 1), v2 = (-1, 3, 1).
float2 GetFullScreenTriangleTexCoord(uint vertexID)
{
    return float2((vertexID << 1) & 2, 1.0 - (vertexID & 2));
}

float4 GetFullScreenTriangleVertexPosition(uint vertexID, float z = 0)
{
    // note: the triangle vertex position coordinates are x2 so the returned UV coordinates are in range -1, 1 on the screen.
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    return float4(uv * 2.0 - 1.0, z, 1.0);
}


Interpolators Main(uint vertexID : SV_VertexID)
{
	Interpolators i;

	i.position = GetFullScreenTriangleVertexPosition(vertexID);

	return i;
}
