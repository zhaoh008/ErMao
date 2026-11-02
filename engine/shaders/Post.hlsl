// ErMao Engine - 后处理（GAMES104 第 7 讲：管线与后处理）
// HDR → ACES 色调映射 → gamma → 暗角，输出到 LDR 后备缓冲。
cbuffer PostParams : register(b0)
{
    float4 gParams;   // x=曝光 y=时间 z=暗角强度 w=调试模式(0正常 1阴影 2法线)
    float4 gScreen;   // xy=分辨率 zw=1/分辨率
};

Texture2D    gSceneColor : register(t0);
Texture2D    gShadowTex  : register(t1);
SamplerState gLinearSamp : register(s0);

struct VSOutput
{
    float4 clip : SV_POSITION;
    float2 uv   : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput o;
    o.clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
    o.uv = uv;
    return o;
}

float3 ACESFilm(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;

    // 调试：直接显示阴影贴图深度（F3 循环到 5）
    if (gParams.w > 1.5)
    {
        float d = gShadowTex.Sample(gLinearSamp, uv).r;
        return float4(d.xxx, 1.0);
    }

    float3 hdr = gSceneColor.Sample(gLinearSamp, uv).rgb * gParams.x;

    float3 mapped = ACESFilm(hdr);
    // 暗角：屏幕边缘轻微压暗，提升画面集中度
    float2 d = uv - 0.5;
    float vignette = 1.0 - dot(d, d) * gParams.z;
    mapped *= saturate(vignette);
    // gamma
    mapped = pow(mapped, 1.0 / 2.2);
    return float4(mapped, 1.0);
}
