cbuffer CameraCB : register(b0)
{
    float4x4 g_view_projection;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), g_view_projection);
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}