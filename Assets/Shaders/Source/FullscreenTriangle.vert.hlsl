float4 Main(uint vertexID : SV_VertexID) : SV_Position
{
    return float4(float2((vertexID << 1) & 2, vertexID & 2), 0.0, 1.0);
}
