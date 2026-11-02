// ErMao Engine - 全屏拷贝（离屏视口 → 后备缓冲）
//
// 无编辑器模式也先渲染到离屏 RT 再拷贝到后备缓冲，这样：
//   * GPU 计时口径与编辑器路径一致（不会把 Present 的 vsync 等待算进来）；
//   * 两条路径的颜色管线完全相同。
// 全屏三角形不需要顶点缓冲，只需一次纹理采样。

cbuffer CopyParams : register(b0)
{
    float4 gParams; // xy=屏幕尺寸 zw=1/屏幕尺寸
};

Texture2D    gSource : register(t0);
SamplerState gSamp   : register(s0);

struct VSOutput
{
    float4 clip : SV_POSITION;
    float2 uv   : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput o;
    o.clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(gSource.SampleLevel(gSamp, input.uv, 0).rgb, 1.0);
}
