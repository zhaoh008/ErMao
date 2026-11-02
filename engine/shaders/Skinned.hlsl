// ErMao Engine - 骨骼蒙皮着色（GAMES104 第 8 讲）
//
// 与 PBR.hlsl 的区别只有顶点阶段：
//   1. 顶点带 4 个骨骼索引 + 4 个权重；
//   2. VS 里先做线性混合蒙皮（LBS）：p' = Σ w_i * (P_i * B_i^{-1}) * p
//   3. 之后的法线变换、光照、阴影与 PBR 完全一致。
//
// 常量缓冲：
//   b0 PerFrame   与 PBR 相同
//   b1 PointLights 与 PBR 相同
//   b2 BonePalette 最多 64 根骨骼；gBones[i] 已是 P_i * B_i^{-1}
//   b3 SkinnedObject 每个角色一份：世界矩阵 + 材质

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

cbuffer PointLights : register(b1)
{
    float4 gLightPosRange[8];
    float4 gLightColorIntensity[8];
};

cbuffer BonePalette : register(b2)
{
    float4x4 gBones[64];
};

cbuffer SkinnedObject : register(b3)
{
    float4x4 gWorld;
    float4   gAlbedo;   // rgb=基础色 a=保留
    float4   gParams;   // x=roughness y=metallic z=调试 w=保留
};

Texture2D    gShadowMap  : register(t0);
SamplerState gShadowSamp : register(s0);

static const float PI = 3.14159265359;

struct VSInput
{
    float3 position    : POSITION;
    float3 normal      : NORMAL;
    float2 uv          : TEXCOORD0;
    uint4  boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
};

struct VSOutput
{
    float4 clip     : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;

    // ---- 线性混合蒙皮：权重归一化后再乘，避免美术导出的权重之和不为 1
    float weightSum = input.boneWeights.x + input.boneWeights.y + input.boneWeights.z + input.boneWeights.w;
    if (weightSum < 1e-5) weightSum = 1.0;

    float4x4 skin = (float4x4)0;
    skin += gBones[input.boneIndices.x] * (input.boneWeights.x / weightSum);
    skin += gBones[input.boneIndices.y] * (input.boneWeights.y / weightSum);
    skin += gBones[input.boneIndices.z] * (input.boneWeights.z / weightSum);
    skin += gBones[input.boneIndices.w] * (input.boneWeights.w / weightSum);

    float4 skinnedPos = mul(skin, float4(input.position, 1.0));
    float3 skinnedNrm = mul((float3x3)skin, input.normal);

    float4 worldPos = mul(gWorld, float4(skinnedPos.xyz, 1.0));
    o.worldPos = worldPos.xyz;
    o.normal = normalize(mul((float3x3)gWorld, skinnedNrm));
    o.uv = input.uv;
    o.clip = mul(gViewProj, worldPos);
    return o;
}

float SampleShadow(float3 worldPos, float3 N)
{
    if (gShadowParams.w < 0.5) return 1.0;

    float4 lightClip = mul(gLightViewProj, float4(worldPos + N * gShadowParams.y, 1.0));
    float3 ndc = lightClip.xyz / lightClip.w;
    if (ndc.z <= 0.0 || ndc.z >= 1.0) return 1.0;

    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;

    float bias = gShadowParams.x;
    float texel = gSunColorTexel.w;
    float lit = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float d = gShadowMap.SampleLevel(gShadowSamp, uv + float2(x, y) * texel, 0).r;
            lit += (ndc.z - bias <= d) ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float GeometrySchlick(float NdotV, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 ShadePoint(float3 N, float3 V, float3 L, float3 radiance, float3 baseColor, float roughness, float metallic)
{
    float3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V)) + 1e-4;
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySchlick(NdotV, roughness) * GeometrySchlick(NdotL, roughness);
    float3 F = FresnelSchlick(max(VdotH, 0.0), f0);
    float3 specular = D * G * F / max(4.0 * NdotV * NdotL, 1e-4);

    float3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * baseColor / PI + specular) * radiance * NdotL;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 V = normalize(gCameraPosTime.xyz - input.worldPos);
    float3 baseColor = gAlbedo.rgb;
    float roughness = clamp(gParams.x, 0.04, 1.0);
    float metallic = saturate(gParams.y);

    float3 sunL = normalize(gSunDirIntensity.xyz);
    float shadow = SampleShadow(input.worldPos, N);

    if (gParams.z > 1.5 && gParams.z < 2.5) return float4(N * 0.5 + 0.5, 1.0); // 法线调试
    if (gParams.z > 0.5 && gParams.z < 1.5) return float4(shadow.xxx, 1.0);    // 阴影调试

    float3 color = ShadePoint(N, V, sunL, gSunColorTexel.rgb * gSunDirIntensity.w, baseColor, roughness, metallic) *
                   shadow;

    int count = (int)gAmbientUpCount.w;
    for (int i = 0; i < count; ++i)
    {
        float3 toLight = gLightPosRange[i].xyz - input.worldPos;
        float dist = length(toLight);
        float range = max(gLightPosRange[i].w, 1e-3);
        if (dist > range) continue;
        float3 L = toLight / max(dist, 1e-4);
        float atten = saturate(1.0 - dist / range);
        atten *= atten;
        color += ShadePoint(N, V, L, gLightColorIntensity[i].rgb * gLightColorIntensity[i].w * atten, baseColor,
                            roughness, metallic);
    }

    float hemi = N.y * 0.5 + 0.5;
    float3 ambient = lerp(gAmbientDownExposure.rgb, gAmbientUpCount.rgb, hemi) * gMisc.y;
    color += ambient * baseColor;

    return float4(color, 1.0);
}
