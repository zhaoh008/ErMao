struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float4 color : COLOR0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // 三个顶点：屏幕坐标 + 颜色
    float2 positions[3] =
    {
        float2( 0.0,  0.55),
        float2( 0.5, -0.45),
        float2(-0.5, -0.45)
    };
    float4 colors[3] =
    {
        float4(0.95, 0.65, 0.15, 1.0),
        float4(0.25, 0.65, 0.95, 1.0),
        float4(0.55, 0.85, 0.35, 1.0)
    };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.color = colors[vertexId];
    return output;
}
