// ErMao Engine - 主前向着色（GAMES104 第 5 讲：光照、材质与着色器）
//
// 常量缓冲布局必须与 Renderer.cpp 中的 C++ 结构体逐字节一致。
// 矩阵约定：C++ Mat4 是列主序，与 HLSL 常量缓冲默认 column_major 一致，可直接 memcpy；
// 实例矩阵从顶点属性拿到的 4 个 float4 是「列」，用 float4x4(c0,c1,c2,c3) 组装后转置着用，
// 具体见下面的 ApplyInstanceTransform。

cbuffer PerFrame : register(b0)
{
    float4x4 gViewProj;
    float4x4 gLightViewProj;
    float4x4 gInvViewProj;
    float4   gCameraPosTime;      // xyz=相机位置, w=时间
    float4   gSunDirIntensity;    // xyz=指向太阳的方向, w=强度
    float4   gSunColorTexel;      // rgb=太阳颜色, w=阴影贴图 texel 大小
    float4   gAmbientUpCount;     // rgb=天光, w=点光数量
    float4   gAmbientDownExposure;// rgb=地光, w=曝光
    float4   gShadowParams;       // x=深度偏移 y=法线偏移 z=PCF半径 w=开关
    float4   gMisc;               // x=调试模式 y=环境光强度 zw=保留
};

cbuffer PointLights : register(b1)
{
    float4 gLightPosRange[8];      // xyz=位置, w=半径
    float4 gLightColorIntensity[8];// rgb=颜色, w=强度
};

Texture2D    gShadowMap  : register(t0);
Texture2D    gAlbedoMap  : register(t1);
SamplerState gShadowSamp : register(s0);
SamplerState gLinearSamp : register(s1);

static const float PI = 3.14159265359;

// 用「列」组装的矩阵乘行向量 = W * v
float4 ApplyWorld(float4 p, float4 c0, float4 c1, float4 c2, float4 c3)
{
    float4x4 W = float4x4(c0, c1, c2, c3);
    return mul(p, W);
}

float3 ApplyWorldDir(float3 n, float4 c0, float4 c1, float4 c2, float4 c3)
{
    float4x4 W = float4x4(c0, c1, c2, c3);
    return mul(float4(n, 0.0), W).xyz;
}

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    // 实例数据（每个物体一份，slot 1）
    float4 col0     : TEXCOORD1;  // 世界矩阵 4 列
    float4 col1     : TEXCOORD2;
    float4 col2     : TEXCOORD3;
    float4 col3     : TEXCOORD4;
    float4 nrm0     : TEXCOORD5;  // 逆转置法线矩阵 3 列（非均匀缩放必需）
    float4 nrm1     : TEXCOORD6;
    float4 nrm2     : TEXCOORD7;
    float4 albedo   : TEXCOORD8;
    float4 params   : TEXCOORD9;  // x=roughness y=metallic z=emissive w=uvScale
};

struct VSOutput
{
    float4 clip      : SV_POSITION;
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float4 albedo    : TEXCOORD3;
    float4 params    : TEXCOORD4;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 worldPos = ApplyWorld(float4(input.position, 1.0), input.col0, input.col1, input.col2, input.col3);
    o.worldPos = worldPos.xyz;
    // 法线用逆转置矩阵：同样用「列拼行向量」的技巧，等价于 normalMatrix * n
    float4x4 NM = float4x4(input.nrm0, input.nrm1, input.nrm2, float4(0, 0, 0, 0));
    o.normal = mul(float4(input.normal, 0.0), NM).xyz;
    o.uv = input.uv * input.params.w;
    o.albedo = input.albedo;
    o.params = input.params;
    o.clip = mul(gViewProj, worldPos);
    return o;
}

// ------------------------------------------------------------------ 阴影
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
    const int radius = 1;
    [unroll]
    for (int y = -radius; y <= radius; ++y)
    {
        [unroll]
        for (int x = -radius; x <= radius; ++x)
        {
            float d = gShadowMap.SampleLevel(gShadowSamp, uv + float2(x, y) * texel, 0).r;
            lit += (ndc.z - bias <= d) ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

// ------------------------------------------------------------------ BRDF（GGX）
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

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 ShadePoint(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float roughness, float metallic)
{
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0) return 0.0;

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float NdotV = saturate(dot(N, V)) + 1e-4;
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float  D = DistributionGGX(NdotH, roughness);
    float  G = GeometrySchlick(NdotV, roughness) * GeometrySchlick(NdotL, roughness);
    float3 F = FresnelSchlick(VdotH, F0);

    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 1e-4);
    float3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + specular) * radiance * NdotL;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 V = normalize(gCameraPosTime.xyz - input.worldPos);

    float3 baseColor = input.albedo.rgb;
    if (input.params.w > 0.0)
    {
        float4 tex = gAlbedoMap.Sample(gLinearSamp, input.uv);
        baseColor *= tex.rgb;
    }

    float roughness = clamp(input.params.x, 0.04, 1.0);
    float metallic = saturate(input.params.y);
    float emissive = input.params.z;
    // 注意：albedo 直接按线性色使用（编辑器里选的颜色即线性值），
    // 贴图本身是 _SRGB 格式，采样时硬件已自动转到线性，这里不能再做一次 gamma。

    float3 sunL = normalize(gSunDirIntensity.xyz);
    float shadow = SampleShadow(input.worldPos, N);

    // 调试视图（F3 循环）：0=正常 1=阴影 2=世界法线 3=基础色
    if (gMisc.x > 2.5) return float4(baseColor, 1.0);
    if (gMisc.x > 1.5) return float4(N * 0.5 + 0.5, 1.0);
    if (gMisc.x > 0.5) return float4(shadow.xxx, 1.0);

    float3 color = 0.0;
    color += ShadePoint(N, V, sunL, gSunColorTexel.rgb * gSunDirIntensity.w, baseColor, roughness, metallic) * shadow;

    // 点光源
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
        float3 radiance = gLightColorIntensity[i].rgb * gLightColorIntensity[i].w * atten;
        color += ShadePoint(N, V, L, radiance, baseColor, roughness, metallic);
    }

    // 半球环境光（廉价 GI 近似）
    float hemi = N.y * 0.5 + 0.5;
    float3 ambient = lerp(gAmbientDownExposure.rgb, gAmbientUpCount.rgb, hemi) * gMisc.y;
    color += ambient * baseColor * (1.0 - metallic * 0.7);

    color += baseColor * emissive;

    return float4(color, 1.0);
}
