// ErMao Engine - 2D 精灵着色（第 23 讲：2D 渲染）
//
// 正交投影（Camera2D::ViewProjection）把世界 XY 映射到 NDC；
// 每个精灵是一次 draw：4 个顶点 + 纹理采样。
// 像素风要点：采样器用 POINT（不做线性过滤），纹理关闭 mipmap。

cbuffer SpriteParams : register(b0)
{
    float4x4 gViewProj;
};

Texture2D    gSprite : register(t0);
SamplerState gSamp   : register(s0);

struct VSInput
{
    float2 position : POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR;
};

struct VSOutput
{
    float4 clip  : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.clip = mul(gViewProj, float4(input.position, 0.0, 1.0));
    o.uv = input.uv;
    o.color = input.color;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 tex = gSprite.Sample(gSamp, input.uv);
    float4 col = tex * input.color;
    // 混合状态是 SRC_ALPHA / INV_SRC_ALPHA，这里输出非预乘颜色
    return float4(col.rgb, col.a);
}
