cbuffer CameraCB : register(b0)
{
    float4x4 g_view_projection;
};

StructuredBuffer<float4x4> g_scene_transforms : register(t1);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float2 lightmap_texcoord : TEXCOORD1;
    uint texture_index : TEXCOORD2;
    float3 color : TEXCOORD3;
    uint transform_index : TEXCOORD4;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float2 lightmap_texcoord : TEXCOORD1;
    uint texture_index : TEXCOORD2;
    float3 color : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4x4 model = g_scene_transforms[input.transform_index];
    float4 world_position = mul(float4(input.position, 1.0), model);
    output.position = mul(world_position, g_view_projection);
    output.normal = mul(input.normal, (float3x3)model);
    output.texcoord = input.texcoord;
    output.lightmap_texcoord = input.lightmap_texcoord;
    output.texture_index = input.texture_index;
    output.color = input.color;
    return output;
}
