// ErMao Engine - 粒子 billboard（GAMES104 第 12 讲）
//
// 顶点由 CPU 每帧生成：位置 + 颜色 + uv（不做实例化，教学规模足够）。
// 像素阶段按 uv 到中心的距离算 alpha，得到一个柔和的圆点，不需要纹理。

cbuffer PerFrame : register(b0)
{
    float4x4 gViewProj;
    float4x4 gLightViewProj;
    float4x4 gInvViewProj;
    float4   gCameraPosTime;
    float4   gSunDirIntensity;
    float4   gSunColorTexel;
    float4   gAmbientUpCount;
    float4   gAmbientDownExposure;
    float4   gShadowParams;
    float4   gMisc;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 clip  : SV_POSITION;
    float4 color : COLOR;
    float2 uv    : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.clip = mul(gViewProj, float4(input.position, 1.0));
    o.color = input.color;
    o.uv = input.uv;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    // uv ∈ [0,1]^2 → d ∈ [-1,1]^2，半径 1 的圆盘外丢弃
    float2 d = input.uv * 2.0 - 1.0;
    float  r2 = dot(d, d);
    if (r2 > 1.0) discard;

    // 中心亮、边缘透明（1 - r² 的 1.5 次方，近似高斯但更便宜）
    float alpha = input.color.a * pow(saturate(1.0 - r2), 1.5);
    return float4(input.color.rgb, alpha);
}
