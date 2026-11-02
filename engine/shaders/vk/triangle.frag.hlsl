struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float4 color : COLOR0;
};

float4 PSMain(VSOutput input) : SV_TARGET
{
    return input.color;
}
