// ErMao Engine - 粒子 billboard（GAMES104 第 12 讲）
//
// 两种像素：程序化圆点（无贴图）或采样 Kenney 粒子贴图（火焰/火花/烟）。
// 有贴图时用加法混合（黑底素材），没有时用普通 alpha 混合。

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

cbuffer ParticleParams : register(b1)
{
    float4 gParams; // x=是否使用贴图
};

Texture2D    gParticleTex  : register(t0);
SamplerState gParticleSamp : register(s1);

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
    if (gParams.x > 0.5)
    {
        // 黑底粒子贴图：rgb 当亮度、a 当遮罩；配合加法混合
        float4 tex = gParticleTex.Sample(gParticleSamp, input.uv);
        float3 rgb = tex.rgb * input.color.rgb * max(tex.a, 0.0);
        return float4(rgb, 1.0);
    }

    float2 d = input.uv * 2.0 - 1.0;
    float  r2 = dot(d, d);
    if (r2 > 1.0) discard;
    float alpha = input.color.a * pow(saturate(1.0 - r2), 1.5);
    return float4(input.color.rgb, alpha);
}
