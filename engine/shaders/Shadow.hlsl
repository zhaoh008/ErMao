// ErMao Engine - 阴影深度 Pass（只写深度）
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
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 col0     : TEXCOORD1;
    float4 col1     : TEXCOORD2;
    float4 col2     : TEXCOORD3;
    float4 col3     : TEXCOORD4;
};

struct VSOutput
{
    float4 clip : SV_POSITION;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4x4 W = float4x4(input.col0, input.col1, input.col2, input.col3);
    float4 worldPos = mul(float4(input.position, 1.0), W);
    o.clip = mul(gLightViewProj, worldPos);
    return o;
}

void PSMain(VSOutput input) {}
