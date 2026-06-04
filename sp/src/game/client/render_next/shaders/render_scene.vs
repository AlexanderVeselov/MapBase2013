cbuffer CameraCB : register(b0)
{
    float4x4 g_view_projection;
};

StructuredBuffer<float4x4> g_scene_transforms : register(t1);
struct InstanceData
{
    uint first_vertex;
    uint vertex_count;
    uint material_index;
    uint transform_index;
};
StructuredBuffer<InstanceData> g_scene_instances : register(t2);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float2 lightmap_texcoord : TEXCOORD1;
    uint instance_id : TEXCOORD2;
    float3 color : TEXCOORD3;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float2 lightmap_texcoord : TEXCOORD1;
    uint material_index : TEXCOORD2;
    float3 color : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    InstanceData instance_data = g_scene_instances[input.instance_id];
    float4x4 model = g_scene_transforms[instance_data.transform_index];
    float4 world_position = mul(float4(input.position, 1.0), model);
    output.position = mul(world_position, g_view_projection);
    output.normal = mul(input.normal, (float3x3)model);
    output.texcoord = input.texcoord;
    output.lightmap_texcoord = input.lightmap_texcoord;
    output.material_index = instance_data.material_index;
    output.color = input.color;
    return output;
}

