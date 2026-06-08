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
    uint bone_offset;
    uint bone_count;
    uint is_visible;
    uint padding;
};

StructuredBuffer<InstanceData> g_scene_instances : register(t2);
StructuredBuffer<float4> g_scene_vertex_colors : register(t3);

StructuredBuffer<float4x4> g_scene_bones : register(t4);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float2 lightmap_texcoord : TEXCOORD1;
    float4 bone_weights : BLENDWEIGHT;
    uint4 bone_indices : BLENDINDICES;
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

    float3 world_position_xyz;
    float3 world_normal;
    if (instance_data.bone_count > 0 && instance_data.bone_offset != 0xFFFFFFFFu)
    {
        world_position_xyz = float3(0.0f, 0.0f, 0.0f);
        world_normal = float3(0.0f, 0.0f, 0.0f);
        [unroll]
        for (uint influence_index = 0; influence_index < 4; ++influence_index)
        {
            float bone_weight = input.bone_weights[influence_index];
            if (bone_weight <= 0.0f)
            {
                continue;
            }

            uint bone_index = input.bone_indices[influence_index];
            if (bone_index >= instance_data.bone_count)
            {
                continue;
            }

            float4x4 bone_matrix = g_scene_bones[instance_data.bone_offset + bone_index];
            world_position_xyz += mul(float4(input.position, 1.0f), bone_matrix).xyz * bone_weight;
            world_normal += mul(input.normal, (float3x3)bone_matrix) * bone_weight;
        }
    }
    else
    {
        float4x4 model = g_scene_transforms[instance_data.transform_index];
        float4 static_world_position = mul(float4(input.position, 1.0f), model);
        world_position_xyz = static_world_position.xyz;
        world_normal = mul(input.normal, (float3x3)model);
    }

    float4 world_position = float4(world_position_xyz, 1.0f);
    output.position = mul(world_position, g_view_projection);
    output.normal = normalize(world_normal);
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
