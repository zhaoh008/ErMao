// ErMao Engine - 程序化天空（GAMES104 第 6 讲：自然景观渲染）
// 全屏三角形 + 用逆 VP 重建视线方向，不依赖天空盒纹理。
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

struct VSOutput
{
    float4 clip : SV_POSITION;
    float2 ndc  : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // 覆盖全屏的大三角形，无需顶点缓冲
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput o;
    o.clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
    o.ndc = o.clip.xy;
    return o;
}

float3 SkyColor(float3 dir, float3 sunDir)
{
    float up = saturate(dir.y * 0.5 + 0.5);
    float3 zenith = gAmbientUpCount.rgb * 0.55;
    float3 horizon = float3(0.55, 0.62, 0.72) * 0.55;
    float3 col = lerp(horizon, zenith, pow(up, 0.65));

    // 太阳盘 + 光晕
    float cosAngle = dot(dir, sunDir);
    float sunDisk = smoothstep(0.9995, 0.99985, cosAngle);
    float glow = pow(saturate(cosAngle), 64.0) * 0.6 + pow(saturate(cosAngle), 8.0) * 0.12;
    col += gSunColorTexel.rgb * (sunDisk * 18.0 + glow) * saturate(gSunDirIntensity.w);

    // 地平线以下压暗，避免“天穿地”
    col *= lerp(0.35, 1.0, saturate(dir.y * 3.0 + 0.5));
    return col;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 world = mul(gInvViewProj, float4(input.ndc, 1.0, 1.0));
    world.xyz /= world.w;
    float3 dir = normalize(world.xyz - gCameraPosTime.xyz);
    return float4(SkyColor(dir, normalize(gSunDirIntensity.xyz)), 1.0);
}
