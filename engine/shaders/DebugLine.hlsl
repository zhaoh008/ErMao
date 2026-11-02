// ErMao Engine - 调试线绘制（AABB、射线、网格、导航路径、骨骼）
cbuffer DebugParams : register(b0)
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
};

struct VSOutput
{
    float4 clip  : SV_POSITION;
    float4 color : COLOR;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.clip = mul(gViewProj, float4(input.position, 1.0));
    o.color = input.color;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return input.color;
}
