cbuffer CameraCB : register(b0)
{
    float4x4 g_view_projection;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    uint texture_index : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    uint texture_index : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), g_view_projection);
    output.normal = input.normal;
    output.texcoord = input.texcoord;
    output.texture_index = input.texture_index;
    return output;
}