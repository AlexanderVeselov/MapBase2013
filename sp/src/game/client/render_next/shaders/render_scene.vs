cbuffer CameraCB : register(b0)
{
    float4x4 g_view_projection;
};

cbuffer g_RootConstants : register(b1)
{
    uint g_draw_instance_id;
};

StructuredBuffer<float4x4> g_scene_transforms : register(t1);

struct InstanceData
{
    float4 color;
    uint vertex_offset;
    uint index_offset;
    uint index_count;
    uint material_index;
    uint transform_index;
    uint vertex_color_offset;
    uint padding[2];
};

StructuredBuffer<InstanceData> g_scene_instances : register(t2);
StructuredBuffer<float4> g_scene_vertex_colors : register(t3);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float2 lightmap_texcoord : TEXCOORD1;
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

VSOutput main(VSInput input, uint vertex_id : SV_VertexID)
{
    VSOutput output;
    InstanceData instance_data = g_scene_instances[g_draw_instance_id];

    float4x4 model = g_scene_transforms[instance_data.transform_index];
    float4 world_position = mul(float4(input.position, 1.0), model);
    output.position = mul(world_position, g_view_projection);
    output.normal = mul(input.normal, (float3x3)model);
    output.texcoord = input.texcoord;
    output.lightmap_texcoord = input.lightmap_texcoord;
    output.material_index = instance_data.material_index;
    float3 vertex_color = float3(1.0f, 1.0f, 1.0f);
    if (instance_data.vertex_color_offset != 0xFFFFFFFFu)
    {
        vertex_color = g_scene_vertex_colors[instance_data.vertex_color_offset + vertex_id].rgb;
    }

    output.color = vertex_color * instance_data.color.rgb;
    return output;
}
