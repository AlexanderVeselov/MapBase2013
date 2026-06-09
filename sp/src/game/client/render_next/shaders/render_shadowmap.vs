cbuffer ShadowCameraCB : register(b0)
{
    float4x4 g_shadow_view_projection;
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
    uint ambient_cube_offset;
    uint bone_offset;
    uint bone_count;
    uint is_visible;
    uint padding;
    uint padding1;
};

StructuredBuffer<InstanceData> g_scene_instances : register(t2);
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
    float2 texcoord : TEXCOORD0;
    uint material_index : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    InstanceData instance_data = g_scene_instances[g_draw_instance_id];

    float3 world_position = 0.0f;
    if (instance_data.bone_count > 0 && instance_data.bone_offset != 0xFFFFFFFFu)
    {
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
            world_position += mul(float4(input.position, 1.0f), bone_matrix).xyz * bone_weight;
        }
    }
    else
    {
        float4x4 model = g_scene_transforms[instance_data.transform_index];
        world_position = mul(float4(input.position, 1.0f), model).xyz;
    }

    output.position = mul(float4(world_position, 1.0f), g_shadow_view_projection);
    output.texcoord = input.texcoord;
    output.material_index = instance_data.material_index;
    return output;
}
